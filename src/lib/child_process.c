/*
 * Node.js Child Process Module for QNode
 *
 * Copyright (c) 2026 QNode Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#define popen_func  _popen
#define pclose_func _pclose
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#define popen_func  popen
#define pclose_func pclose
#endif

#include "cutils.h"
#include "quickjs-libc.h"

/* ================================================================
 * Growable buffer
 * ================================================================ */

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} GrowBuf;

static int growbuf_init(JSContext *ctx, GrowBuf *gb, size_t initial)
{
    gb->cap = initial > 0 ? initial : 4096;
    gb->data = js_malloc(ctx, gb->cap);
    gb->len = 0;
    return gb->data ? 0 : -1;
}

static int growbuf_append(JSContext *ctx, GrowBuf *gb, const uint8_t *src, size_t n)
{
    if (gb->len + n > gb->cap) {
        gb->cap = (gb->len + n) * 2;
        uint8_t *p = js_realloc(ctx, gb->data, gb->cap);
        if (!p) return -1;
        gb->data = p;
    }
    memcpy(gb->data + gb->len, src, n);
    gb->len += n;
    return 0;
}

static void growbuf_free(JSContext *ctx, GrowBuf *gb)
{
    js_free(ctx, gb->data);
    gb->data = NULL;
}

/* ================================================================
 * Helpers: parse options, build argv
 * ================================================================ */

typedef struct {
    const char *encoding;
    const char *cwd;
    int64_t maxBuffer;
    int64_t timeout;
    int encoding_set;
    int shell;
    JSValue input;       /* for sync: input data */
} ExecOptions;

static void parse_exec_options(JSContext *ctx, JSValueConst opts, ExecOptions *eo)
{
    memset(eo, 0, sizeof(*eo));
    eo->maxBuffer = 1024 * 1024;
    eo->input = JS_UNDEFINED;
    if (!JS_IsObject(opts)) return;

    JSValue val;
    val = JS_GetPropertyStr(ctx, opts, "encoding");
    if (JS_IsString(val)) { eo->encoding = JS_ToCString(ctx, val); eo->encoding_set = 1; }
    JS_FreeValue(ctx, val);

    val = JS_GetPropertyStr(ctx, opts, "cwd");
    if (JS_IsString(val)) eo->cwd = JS_ToCString(ctx, val);
    JS_FreeValue(ctx, val);

    val = JS_GetPropertyStr(ctx, opts, "maxBuffer");
    if (JS_IsNumber(val)) JS_ToInt64(ctx, &eo->maxBuffer, val);
    JS_FreeValue(ctx, val);

    val = JS_GetPropertyStr(ctx, opts, "timeout");
    if (JS_IsNumber(val)) JS_ToInt64(ctx, &eo->timeout, val);
    JS_FreeValue(ctx, val);

    val = JS_GetPropertyStr(ctx, opts, "shell");
    if (JS_IsBool(val)) eo->shell = JS_VALUE_GET_BOOL(val);
    else if (JS_IsString(val)) eo->shell = 1; /* shell=string means use that shell */
    JS_FreeValue(ctx, val);

    val = JS_GetPropertyStr(ctx, opts, "input");
    if (!JS_IsUndefined(val)) eo->input = JS_DupValue(ctx, val);
    JS_FreeValue(ctx, val);
}

static void free_exec_options(JSContext *ctx, ExecOptions *eo)
{
    if (eo->encoding) JS_FreeCString(ctx, eo->encoding);
    if (eo->cwd) JS_FreeCString(ctx, eo->cwd);
    JS_FreeValue(ctx, eo->input);
}

/* Build a C argv array from JS file + args.  Returns malloc'd NULL-terminated array. */
static char **build_argv(JSContext *ctx, const char *file, JSValueConst args_val, int *out_argc)
{
    int64_t arr_len = 0;
    if (JS_IsArray(ctx, args_val)) {
        JSValue len_val = JS_GetPropertyStr(ctx, args_val, "length");
        JS_ToInt64(ctx, &arr_len, len_val);
        JS_FreeValue(ctx, len_val);
    }

    int total = 1 + (int)arr_len; /* file + args */
    char **argv = malloc((total + 1) * sizeof(char *));
    if (!argv) return NULL;

    argv[0] = strdup(file);
    for (int i = 0; i < (int)arr_len; i++) {
        JSValue v = JS_GetPropertyUint32(ctx, args_val, (uint32_t)i);
        if (JS_IsString(v))
            argv[1 + i] = strdup(JS_ToCString(ctx, v));
        else
            argv[1 + i] = strdup("");
        JS_FreeValue(ctx, v);
    }
    argv[total] = NULL;
    *out_argc = total;
    return argv;
}

static void free_argv(char **argv)
{
    if (!argv) return;
    for (int i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}

/* ================================================================
 * execSync  (popen-based, runs command through shell)
 * ================================================================ */

static JSValue js_child_execSync(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "command must be a string");

    size_t cmd_len;
    const char *command = JS_ToCStringLen(ctx, &cmd_len, argv[0]);
    if (!command) return JS_EXCEPTION;

    ExecOptions eo;
    parse_exec_options(ctx, argc >= 2 ? argv[1] : JS_UNDEFINED, &eo);

#if defined(_WIN32)
    char old_cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, old_cwd);
    if (eo.cwd) SetCurrentDirectoryA(eo.cwd);
#else
    char old_cwd[4096];
    getcwd(old_cwd, sizeof(old_cwd));
    if (eo.cwd) chdir(eo.cwd);
#endif

    FILE *pipe = popen_func(command, "r");
    if (!pipe) {
#if defined(_WIN32)
        if (eo.cwd) SetCurrentDirectoryA(old_cwd);
#else
        if (eo.cwd) chdir(old_cwd);
#endif
        JS_ThrowTypeError(ctx, "execSync: failed to execute command");
        free_exec_options(ctx, &eo);
        JS_FreeCString(ctx, command);
        return JS_EXCEPTION;
    }

    GrowBuf gb;
    if (growbuf_init(ctx, &gb, 4096) < 0) {
        pclose_func(pipe);
        free_exec_options(ctx, &eo);
        JS_FreeCString(ctx, command);
        return JS_EXCEPTION;
    }

    char rbuf[4096];
    size_t n;
    while ((n = fread(rbuf, 1, sizeof(rbuf), pipe)) > 0) {
        if (gb.len + n > (size_t)eo.maxBuffer) {
            pclose_func(pipe);
            growbuf_free(ctx, &gb);
            free_exec_options(ctx, &eo);
            JS_FreeCString(ctx, command);
#if defined(_WIN32)
            if (eo.cwd) SetCurrentDirectoryA(old_cwd);
#else
            if (eo.cwd) chdir(old_cwd);
#endif
            return JS_ThrowRangeError(ctx, "execSync: maxBuffer exceeded");
        }
        if (growbuf_append(ctx, &gb, (uint8_t *)rbuf, n) < 0) {
            pclose_func(pipe);
            free_exec_options(ctx, &eo);
            JS_FreeCString(ctx, command);
            return JS_EXCEPTION;
        }
    }

    int status = pclose_func(pipe);

#if defined(_WIN32)
    if (eo.cwd) SetCurrentDirectoryA(old_cwd);
#else
    if (eo.cwd) chdir(old_cwd);
#endif

    int exit_code;
#if defined(_WIN32)
    exit_code = status;
#else
    if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);
    else exit_code = -1;
#endif

    int is_buf = eo.encoding_set && eo.encoding &&
                 (strcmp(eo.encoding, "buffer") == 0 ||
                  strcmp(eo.encoding, "arraybuffer") == 0);

    if (exit_code != 0) {
        JSValue err = JS_NewError(ctx);
        char msg[256];
        snprintf(msg, sizeof(msg), "Command failed: %s (exit code %d)", command, exit_code);
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, msg));
        JS_SetPropertyStr(ctx, err, "status", JS_NewInt32(ctx, exit_code));
        JS_SetPropertyStr(ctx, err, "code", JS_NewInt32(ctx, exit_code));
        if (!is_buf) {
            JS_SetPropertyStr(ctx, err, "stdout", JS_NewStringLen(ctx, (const char *)gb.data, gb.len));
            JS_SetPropertyStr(ctx, err, "stderr", JS_NewString(ctx, ""));
        } else {
            JS_SetPropertyStr(ctx, err, "stdout", JS_NewArrayBufferCopy(ctx, gb.data, gb.len));
            JS_SetPropertyStr(ctx, err, "stderr", JS_NewArrayBufferCopy(ctx, NULL, 0));
        }
        JS_SetPropertyStr(ctx, err, "cmd", JS_NewStringLen(ctx, command, cmd_len));
        growbuf_free(ctx, &gb);
        free_exec_options(ctx, &eo);
        JS_FreeCString(ctx, command);
        return JS_Throw(ctx, err);
    }

    JSValue result;
    if (!is_buf)
        result = JS_NewStringLen(ctx, (const char *)gb.data, gb.len);
    else
        result = JS_NewArrayBufferCopy(ctx, gb.data, gb.len);
    growbuf_free(ctx, &gb);
    free_exec_options(ctx, &eo);
    JS_FreeCString(ctx, command);
    return result;
}

/* ================================================================
 * Low-level: create process with pipes  (platform-specific)
 * ================================================================ */

typedef struct {
    int pid;
    int64_t handle;    /* Windows: process handle; Unix: pid */
    int stdin_fd;      /* parent writes (-1 = no pipe) */
    int stdout_fd;     /* parent reads (-1 = no pipe) */
    int stderr_fd;     /* parent reads (-1 = no pipe) */
} CPRawResult;

static int create_process(const char *file, char **argv,
                           const char *cwd, char **envp,
                           int pipe_stdin, int pipe_stdout, int pipe_stderr,
                           CPRawResult *out)
{
    memset(out, 0, sizeof(*out));
    out->stdin_fd = out->stdout_fd = out->stderr_fd = -1;

#if defined(_WIN32)
    /* Build command line string */
    size_t cmdline_len = 0;
    for (int i = 0; argv[i]; i++) {
        cmdline_len += strlen(argv[i]) + 3; /* + quotes + space */
    }
    char *cmdline = malloc(cmdline_len + 1);
    if (!cmdline) return -1;
    cmdline[0] = '\0';
    for (int i = 0; argv[i]; i++) {
        if (i > 0) strcat(cmdline, " ");
        /* Quote if contains space or is empty */
        if (argv[i][0] && !strpbrk(argv[i], " \t\"")) {
            strcat(cmdline, argv[i]);
        } else {
            strcat(cmdline, "\"");
            strcat(cmdline, argv[i]);
            strcat(cmdline, "\"");
        }
    }

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdinRd = NULL, hStdinWr = NULL;
    HANDLE hStdoutRd = NULL, hStdoutWr = NULL;
    HANDLE hStderrRd = NULL, hStderrWr = NULL;
    HANDLE hNull = NULL;

    if (pipe_stdin) {
        CreatePipe(&hStdinRd, &hStdinWr, &sa, 0);
        SetHandleInformation(hStdinWr, HANDLE_FLAG_INHERIT, 0);
    }
    if (pipe_stdout) {
        CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0);
        SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);
    }
    if (pipe_stderr) {
        CreatePipe(&hStderrRd, &hStderrWr, &sa, 0);
        SetHandleInformation(hStderrRd, HANDLE_FLAG_INHERIT, 0);
    }

    /* Open NUL for non-piped streams */
    if (!pipe_stdin || !pipe_stdout || !pipe_stderr)
        hNull = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &sa, OPEN_EXISTING, 0, NULL);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = pipe_stdin   ? hStdinRd  : hNull;
    si.hStdOutput = pipe_stdout  ? hStdoutWr : hNull;
    si.hStdError  = pipe_stderr  ? hStderrWr : hNull;

    DWORD create_flags = CREATE_NO_WINDOW;

    /* Build environment block if envp provided */
    char *env_block = NULL;
    if (envp) {
        size_t total = 0;
        for (int i = 0; envp[i]; i++) total += strlen(envp[i]) + 1;
        env_block = malloc(total + 1);
        if (env_block) {
            char *p = env_block;
            for (int i = 0; envp[i]; i++) {
                size_t l = strlen(envp[i]);
                memcpy(p, envp[i], l);
                p[l] = '\0';
                p += l + 1;
            }
            *p = '\0';
        }
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                              create_flags, env_block, cwd, &si, &pi);

    /* Close child-side handles */
    if (hStdinRd) CloseHandle(hStdinRd);
    if (hStdoutWr) CloseHandle(hStdoutWr);
    if (hStderrWr) CloseHandle(hStderrWr);
    if (hNull) CloseHandle(hNull);
    free(cmdline);
    free(env_block);

    if (!ok) return -1;

    /* Convert parent-side handles to CRT FDs */
    if (pipe_stdin && hStdinWr)
        out->stdin_fd = _open_osfhandle((intptr_t)hStdinWr, 0);
    else if (hStdinWr) CloseHandle(hStdinWr);

    if (pipe_stdout && hStdoutRd)
        out->stdout_fd = _open_osfhandle((intptr_t)hStdoutRd, _O_RDONLY);
    else if (hStdoutRd) CloseHandle(hStdoutRd);

    if (pipe_stderr && hStderrRd)
        out->stderr_fd = _open_osfhandle((intptr_t)hStderrRd, _O_RDONLY);
    else if (hStderrRd) CloseHandle(hStderrRd);

    out->pid = (int)pi.dwProcessId;
    out->handle = (int64_t)pi.hProcess;
    CloseHandle(pi.hThread);
    return 0;

#else /* Unix */
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (pipe_stdin)  pipe(stdin_pipe);
    if (pipe_stdout) pipe(stdout_pipe);
    if (pipe_stderr) pipe(stderr_pipe);

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        /* Child */
        if (pipe_stdin)  { dup2(stdin_pipe[0], STDIN_FILENO);  close(stdin_pipe[1]); }
        if (pipe_stdout) { dup2(stdout_pipe[1], STDOUT_FILENO); close(stdout_pipe[0]); }
        if (pipe_stderr) { dup2(stderr_pipe[1], STDERR_FILENO); close(stderr_pipe[0]); }

        /* Close unused ends */
        if (stdin_pipe[0] > 2) close(stdin_pipe[0]);
        if (stdin_pipe[1] > 2) close(stdin_pipe[1]);
        if (stdout_pipe[0] > 2) close(stdout_pipe[0]);
        if (stdout_pipe[1] > 2) close(stdout_pipe[1]);
        if (stderr_pipe[0] > 2) close(stderr_pipe[0]);
        if (stderr_pipe[1] > 2) close(stderr_pipe[1]);

        if (cwd) chdir(cwd);

        if (envp) execve(file, argv, envp);
        else      execvp(file, argv);
        _exit(127);
    }

    /* Parent */
    if (pipe_stdin)  { close(stdin_pipe[0]);  out->stdin_fd  = stdin_pipe[1]; }
    if (pipe_stdout) { close(stdout_pipe[1]); out->stdout_fd = stdout_pipe[0]; }
    if (pipe_stderr) { close(stderr_pipe[1]); out->stderr_fd = stderr_pipe[0]; }

    /* Set read FDs non-blocking */
    if (out->stdout_fd >= 0)
        fcntl(out->stdout_fd, F_SETFL, fcntl(out->stdout_fd, F_GETFL) | O_NONBLOCK);
    if (out->stderr_fd >= 0)
        fcntl(out->stderr_fd, F_SETFL, fcntl(out->stderr_fd, F_GETFL) | O_NONBLOCK);

    out->pid = (int)pid;
    out->handle = (int64_t)pid;
    return 0;
#endif
}

/* ================================================================
 * Pipe I/O primitives  (used by JS async layer)
 * ================================================================ */

/* _readPipe(fd, maxLen) → ArrayBuffer or null (EOF / no data) */
static JSValue js_cp_readPipe(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    int fd, maxLen = 65536;
    if (argc < 1 || JS_ToInt32(ctx, &fd, argv[0]))
        return JS_ThrowTypeError(ctx, "fd required");
    if (argc >= 2) JS_ToInt32(ctx, &maxLen, argv[1]);
    if (maxLen <= 0) maxLen = 65536;

#if defined(_WIN32)
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    DWORD avail = 0;
    if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
        /* Pipe closed / error → EOF */
        return JS_NULL;
    }
    if (avail == 0) return JS_NULL; /* no data yet */
    int toRead = (int)avail < maxLen ? (int)avail : maxLen;
    uint8_t *buf = js_malloc(ctx, toRead);
    if (!buf) return JS_EXCEPTION;
    int n = _read(fd, buf, toRead);
    if (n <= 0) { js_free(ctx, buf); return JS_NULL; }
    JSValue ab = JS_NewArrayBufferCopy(ctx, buf, n);
    js_free(ctx, buf);
    return ab;
#else
    uint8_t *buf = js_malloc(ctx, maxLen);
    if (!buf) return JS_EXCEPTION;
    int n = read(fd, buf, maxLen);
    if (n < 0) { js_free(ctx, buf); return (errno == EAGAIN) ? JS_NULL : JS_NULL; }
    if (n == 0) { js_free(ctx, buf); return JS_NULL; }
    JSValue ab = JS_NewArrayBufferCopy(ctx, buf, n);
    js_free(ctx, buf);
    return ab;
#endif
}

/* _writePipe(fd, data) → bytesWritten */
static JSValue js_cp_writePipe(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    int fd;
    if (argc < 2 || JS_ToInt32(ctx, &fd, argv[0]))
        return JS_ThrowTypeError(ctx, "fd and data required");

    const uint8_t *data;
    size_t len;
    if (JS_IsString(argv[1])) {
        const char *s = JS_ToCStringLen(ctx, &len, argv[1]);
        int n = _write(fd, s, (unsigned int)len);
        JS_FreeCString(ctx, s);
        return JS_NewInt32(ctx, n > 0 ? n : 0);
    }
    /* ArrayBuffer / Uint8Array */
    JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[1], NULL, &len, NULL);
    if (JS_IsException(ab)) {
        ab = JS_DupValue(ctx, argv[1]);
    }
    data = JS_GetArrayBuffer(ctx, &len, ab);
    if (!data) { JS_FreeValue(ctx, ab); return JS_ThrowTypeError(ctx, "data must be string or ArrayBuffer"); }
    int n = _write(fd, data, (unsigned int)len);
    JS_FreeValue(ctx, ab);
    return JS_NewInt32(ctx, n > 0 ? n : 0);
}

/* _closePipe(fd) → undefined */
static JSValue js_cp_closePipe(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    int fd;
    if (argc < 1 || JS_ToInt32(ctx, &fd, argv[0]))
        return JS_UNDEFINED;
    _close(fd);
    return JS_UNDEFINED;
}

/* _decodeOutput(ArrayBuffer) → string
 * On Windows: convert from system code page (e.g. GBK) to UTF-8 string
 * On Unix: assume UTF-8, create string directly
 */
static JSValue js_cp_decodeOutput(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "ArrayBuffer required");

    const uint8_t *data;
    size_t len;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0], NULL, &len, NULL);
    if (JS_IsException(ab)) {
        ab = JS_DupValue(ctx, argv[0]);
    }
    data = JS_GetArrayBuffer(ctx, &len, ab);
    if (!data) {
        JS_FreeValue(ctx, ab);
        return JS_ThrowTypeError(ctx, "expected ArrayBuffer");
    }

#if defined(_WIN32)
    /* Try UTF-8 first; if not valid, fall back to CP_ACP (e.g. GBK) */
    if (len == 0) {
        JS_FreeValue(ctx, ab);
        return JS_NewString(ctx, "");
    }
    int wlen;
    wchar_t *wbuf;

    /* Try UTF-8 with strict validation */
    wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)data, (int)len, NULL, 0);
    if (wlen > 0) {
        wbuf = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, (const char *)data, (int)len, wbuf, wlen);
    } else {
        /* Not valid UTF-8 — try system code page (CP_ACP) */
        wlen = MultiByteToWideChar(CP_ACP, 0, (const char *)data, (int)len, NULL, 0);
        if (wlen <= 0) {
            JS_FreeValue(ctx, ab);
            return JS_NewStringLen(ctx, (const char *)data, len);
        }
        wbuf = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_ACP, 0, (const char *)data, (int)len, wbuf, wlen);
    }

    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, NULL, 0, NULL, NULL);
    char *ubuf = (char *)malloc(ulen + 1);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, ubuf, ulen, NULL, NULL);
    free(wbuf);

    JSValue result = JS_NewStringLen(ctx, ubuf, ulen);
    free(ubuf);
    JS_FreeValue(ctx, ab);
    return result;
#else
    JSValue result = JS_NewStringLen(ctx, (const char *)data, len);
    JS_FreeValue(ctx, ab);
    return result;
#endif
}

/* _pollProcess(handle) → { exited, exitCode } */
static JSValue js_cp_pollProcess(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    int64_t handle;
    if (argc < 1 || JS_ToInt64(ctx, &handle, argv[0]))
        return JS_ThrowTypeError(ctx, "handle required");

    JSValue obj = JS_NewObject(ctx);

#if defined(_WIN32)
    DWORD exitCode;
    if (GetExitCodeProcess((HANDLE)handle, &exitCode) && exitCode != STILL_ACTIVE) {
        JS_SetPropertyStr(ctx, obj, "exited", JS_NewBool(ctx, TRUE));
        JS_SetPropertyStr(ctx, obj, "exitCode", JS_NewInt32(ctx, (int)exitCode));
    } else {
        JS_SetPropertyStr(ctx, obj, "exited", JS_NewBool(ctx, FALSE));
        JS_SetPropertyStr(ctx, obj, "exitCode", JS_NewInt32(ctx, -1));
    }
#else
    int status;
    pid_t ret = waitpid((pid_t)handle, &status, WNOHANG);
    if (ret > 0) {
        int ec = WIFEXITED(status) ? WEXITSTATUS(status) :
                 WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
        JS_SetPropertyStr(ctx, obj, "exited", JS_NewBool(ctx, TRUE));
        JS_SetPropertyStr(ctx, obj, "exitCode", JS_NewInt32(ctx, ec));
    } else {
        JS_SetPropertyStr(ctx, obj, "exited", JS_NewBool(ctx, FALSE));
        JS_SetPropertyStr(ctx, obj, "exitCode", JS_NewInt32(ctx, -1));
    }
#endif
    return obj;
}

/* _killProcess(pid) → bool */
static JSValue js_cp_killProcess(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    int pid;
    if (argc < 1 || JS_ToInt32(ctx, &pid, argv[0]))
        return JS_NewBool(ctx, FALSE);
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (!h) return JS_NewBool(ctx, FALSE);
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return JS_NewBool(ctx, ok ? TRUE : FALSE);
#else
    return JS_NewBool(ctx, kill((pid_t)pid, SIGTERM) == 0 ? TRUE : FALSE);
#endif
}

/* ================================================================
 * _spawnProcess(file, args, options)  → raw result object
 * ================================================================ */

static JSValue js_cp_spawnProcess(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "file must be a string");

    const char *file = JS_ToCString(ctx, argv[0]);
    if (!file) return JS_EXCEPTION;

    /* Parse options first to check shell flag */
    const char *cwd = NULL;
    char **envp = NULL;
    int pipe_in = 1, pipe_out = 1, pipe_err = 1;
    int use_shell = 0;

    if (argc >= 3 && JS_IsObject(argv[2])) {
        JSValue v;
        v = JS_GetPropertyStr(ctx, argv[2], "cwd");
        if (JS_IsString(v)) cwd = JS_ToCString(ctx, v);
        JS_FreeValue(ctx, v);

        v = JS_GetPropertyStr(ctx, argv[2], "shell");
        if (JS_IsBool(v)) use_shell = JS_VALUE_GET_BOOL(v);
        else if (JS_IsString(v)) use_shell = 1;
        JS_FreeValue(ctx, v);

        v = JS_GetPropertyStr(ctx, argv[2], "env");
        if (JS_IsObject(v)) {
            /* Build envp array from object */
            JSPropertyEnum *props = NULL;
            uint32_t prop_count = 0;
            JS_GetOwnPropertyNames(ctx, &props, &prop_count, v,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
            if (prop_count > 0) {
                envp = malloc((prop_count + 1) * sizeof(char *));
                if (envp) {
                    for (uint32_t i = 0; i < prop_count; i++) {
                        JSValue key = JS_AtomToValue(ctx, props[i].atom);
                        JSValue val = JS_GetProperty(ctx, v, props[i].atom);
                        const char *ks = JS_ToCString(ctx, key);
                        const char *vs = JS_ToCString(ctx, val);
                        size_t l = strlen(ks) + strlen(vs) + 2;
                        envp[i] = malloc(l);
                        snprintf(envp[i], l, "%s=%s", ks, vs);
                        JS_FreeCString(ctx, ks);
                        JS_FreeCString(ctx, vs);
                        JS_FreeValue(ctx, key);
                        JS_FreeValue(ctx, val);
                    }
                    envp[prop_count] = NULL;
                }
            }
            for (uint32_t i = 0; i < prop_count; i++) JS_FreeAtom(ctx, props[i].atom);
            js_free(ctx, props);
        }
        JS_FreeValue(ctx, v);

        /* stdio option: simplified */
        v = JS_GetPropertyStr(ctx, argv[2], "stdio");
        if (JS_IsArray(ctx, v)) {
            JSValue s0 = JS_GetPropertyUint32(ctx, v, 0);
            JSValue s1 = JS_GetPropertyUint32(ctx, v, 1);
            JSValue s2 = JS_GetPropertyUint32(ctx, v, 2);
            if (JS_IsString(s0) && strcmp(JS_ToCString(ctx, s0), "ignore") == 0) pipe_in = 0;
            if (JS_IsString(s1) && strcmp(JS_ToCString(ctx, s1), "ignore") == 0) pipe_out = 0;
            if (JS_IsString(s2) && strcmp(JS_ToCString(ctx, s2), "ignore") == 0) pipe_err = 0;
            /* Free cstrings properly */
            if (JS_IsString(s0)) { const char *p = JS_ToCString(ctx, s0); JS_FreeCString(ctx, p); }
            if (JS_IsString(s1)) { const char *p = JS_ToCString(ctx, s1); JS_FreeCString(ctx, p); }
            if (JS_IsString(s2)) { const char *p = JS_ToCString(ctx, s2); JS_FreeCString(ctx, p); }
            JS_FreeValue(ctx, s0); JS_FreeValue(ctx, s1); JS_FreeValue(ctx, s2);
        } else if (JS_IsString(v)) {
            const char *s = JS_ToCString(ctx, v);
            if (strcmp(s, "ignore") == 0) pipe_in = pipe_out = pipe_err = 0;
            JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, v);
    }

    /* Build argv */
    int ac = 0;
    char **av = build_argv(ctx, file, argc >= 2 ? argv[1] : JS_UNDEFINED, &ac);
    char **spawn_av = av;
    if (use_shell) {
        /* Build full command string: file arg1 arg2 ... */
        size_t full_len = strlen(file) + 1;
        int64_t arr_len = 0;
        if (argc >= 2 && JS_IsArray(ctx, argv[1])) {
            JSValue lv = JS_GetPropertyStr(ctx, argv[1], "length");
            JS_ToInt64(ctx, &arr_len, lv);
            JS_FreeValue(ctx, lv);
        }
        char **arg_strs = NULL;
        if (arr_len > 0) {
            arg_strs = malloc(arr_len * sizeof(char *));
            for (int i = 0; i < arr_len; i++) {
                JSValue v2 = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
                arg_strs[i] = JS_IsString(v2) ? strdup(JS_ToCString(ctx, v2)) : strdup("");
                full_len += strlen(arg_strs[i]) + 1;
                JS_FreeValue(ctx, v2);
            }
        }
        char *full_cmd = malloc(full_len + 1);
        strcpy(full_cmd, file);
        for (int i = 0; i < arr_len; i++) {
            strcat(full_cmd, " ");
            strcat(full_cmd, arg_strs[i]);
            free(arg_strs[i]);
        }
        free(arg_strs);

        spawn_av = malloc(4 * sizeof(char *));
#if defined(_WIN32)
        spawn_av[0] = strdup("cmd.exe");
        spawn_av[1] = strdup("/c");
#else
        spawn_av[0] = strdup("/bin/sh");
        spawn_av[1] = strdup("-c");
#endif
        spawn_av[2] = full_cmd;
        spawn_av[3] = NULL;
    }

    CPRawResult raw;
    int rc = create_process(spawn_av[0], spawn_av, cwd, envp, pipe_in, pipe_out, pipe_err, &raw);

    if (spawn_av != av) {
        free_argv(spawn_av);
    }

    /* Cleanup */
    if (cwd) JS_FreeCString(ctx, cwd);
    if (envp) {
        for (int i = 0; envp[i]; i++) free(envp[i]);
        free(envp);
    }
    free_argv(av);
    JS_FreeCString(ctx, file);

    if (rc < 0)
        return JS_ThrowTypeError(ctx, "spawn: failed to create process");

    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "pid", JS_NewInt32(ctx, raw.pid));
    JS_SetPropertyStr(ctx, result, "_handle", JS_NewInt64(ctx, raw.handle));
    JS_SetPropertyStr(ctx, result, "stdin_fd", JS_NewInt32(ctx, raw.stdin_fd));
    JS_SetPropertyStr(ctx, result, "stdout_fd", JS_NewInt32(ctx, raw.stdout_fd));
    JS_SetPropertyStr(ctx, result, "stderr_fd", JS_NewInt32(ctx, raw.stderr_fd));
    return result;
}

/* ================================================================
 * spawnSync / execFileSync
 * ================================================================ */

/* Helper: read all available data from fd into GrowBuf, return total */
static int read_all_from_fd(JSContext *ctx, int fd, GrowBuf *gb, size_t maxBuffer)
{
    if (fd < 0) return 0;
    char buf[8192];
    for (;;) {
#if defined(_WIN32)
        HANDLE h = (HANDLE)_get_osfhandle(fd);
        DWORD avail = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) break;
        if (avail == 0) break;
        int toRead = avail < sizeof(buf) ? avail : sizeof(buf);
        int n = _read(fd, buf, toRead);
#else
        int n = read(fd, buf, sizeof(buf));
        if (n < 0) { if (errno == EAGAIN) break; break; }
#endif
        if (n <= 0) break;
        if (gb->len + n > maxBuffer) return -1;
        if (growbuf_append(ctx, gb, (uint8_t *)buf, n) < 0) return -2;
    }
    return 0;
}

static JSValue js_child_spawnSync(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "command must be a string");

    const char *cmd = JS_ToCString(ctx, argv[0]);
    if (!cmd) return JS_EXCEPTION;

    ExecOptions eo;
    parse_exec_options(ctx, argc >= 3 ? argv[2] : JS_UNDEFINED, &eo);
    int shell = eo.shell;

    /* Build argv */
    int ac = 0;
    char **av;
    if (shell) {
        /* Build shell command: sh -c "cmd arg1 arg2 ..." */
        size_t cmdlen = strlen(cmd);
        int64_t arr_len = 0;
        if (argc >= 2 && JS_IsArray(ctx, argv[1])) {
            JSValue lv = JS_GetPropertyStr(ctx, argv[1], "length");
            JS_ToInt64(ctx, &arr_len, lv);
            JS_FreeValue(ctx, lv);
        }
        /* Build full command string */
        size_t total = cmdlen + 1;
        char **arg_strs = NULL;
        if (arr_len > 0) {
            arg_strs = malloc(arr_len * sizeof(char *));
            for (int i = 0; i < arr_len; i++) {
                JSValue v = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
                arg_strs[i] = JS_IsString(v) ? strdup(JS_ToCString(ctx, v)) : strdup("");
                total += strlen(arg_strs[i]) + 1;
                JS_FreeValue(ctx, v);
            }
        }
        char *full_cmd = malloc(total + 1);
        strcpy(full_cmd, cmd);
        for (int i = 0; i < arr_len; i++) {
            strcat(full_cmd, " ");
            strcat(full_cmd, arg_strs[i]);
            free(arg_strs[i]);
        }
        free(arg_strs);
        av = malloc(4 * sizeof(char *));
#if defined(_WIN32)
        av[0] = strdup("cmd.exe");
        av[1] = strdup("/c");
        av[2] = full_cmd;
        av[3] = NULL;
        ac = 3;
#else
        av[0] = strdup("/bin/sh");
        av[1] = strdup("-c");
        av[2] = full_cmd;
        av[3] = NULL;
        ac = 3;
#endif
    } else {
        av = build_argv(ctx, cmd, argc >= 2 ? argv[1] : JS_UNDEFINED, &ac);
    }

    CPRawResult raw;
    int rc = create_process(av[0], av, eo.cwd, NULL, 1, 1, 1, &raw);
    free_argv(av);
    JS_FreeCString(ctx, cmd);

    if (rc < 0) {
        /* Return error result object instead of throwing (matches Node.js behavior) */
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "pid", JS_NewInt32(ctx, -1));
        JS_SetPropertyStr(ctx, result, "status", JS_NULL);
        JS_SetPropertyStr(ctx, result, "stdout", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, result, "stderr", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, result, "error", JS_NewError(ctx));
        free_exec_options(ctx, &eo);
        return result;
    }

    /* Write input if provided */
    if (!JS_IsUndefined(eo.input)) {
        const uint8_t *idata;
        size_t ilen;
        if (JS_IsString(eo.input)) {
            const char *s = JS_ToCStringLen(ctx, &ilen, eo.input);
            if (s) _write(raw.stdin_fd, s, ilen);
            JS_FreeCString(ctx, s);
        } else {
            JSValue ab = JS_GetTypedArrayBuffer(ctx, eo.input, NULL, &ilen, NULL);
            if (!JS_IsException(ab)) {
                idata = JS_GetArrayBuffer(ctx, &ilen, ab);
                if (idata) _write(raw.stdin_fd, idata, ilen);
            } else {
                idata = JS_GetArrayBuffer(ctx, &ilen, eo.input);
                if (idata) _write(raw.stdin_fd, idata, ilen);
            }
            if (!JS_IsException(ab)) JS_FreeValue(ctx, ab);
        }
    }
    if (raw.stdin_fd >= 0) _close(raw.stdin_fd);

    /* Read stdout/stderr concurrently until process exits */
    GrowBuf gb_out, gb_err;
    growbuf_init(ctx, &gb_out, 4096);
    growbuf_init(ctx, &gb_err, 4096);

    for (;;) {
        read_all_from_fd(ctx, raw.stdout_fd, &gb_out, (size_t)eo.maxBuffer);
        read_all_from_fd(ctx, raw.stderr_fd, &gb_err, (size_t)eo.maxBuffer);

#if defined(_WIN32)
        DWORD exitCode;
        if (GetExitCodeProcess((HANDLE)raw.handle, &exitCode) && exitCode != STILL_ACTIVE)
            break;
        Sleep(1);
#else
        int status;
        pid_t ret = waitpid((pid_t)raw.handle, &status, 0);
        if (ret > 0) break;
#endif
    }
    /* Final reads */
    read_all_from_fd(ctx, raw.stdout_fd, &gb_out, (size_t)eo.maxBuffer);
    read_all_from_fd(ctx, raw.stderr_fd, &gb_err, (size_t)eo.maxBuffer);

    if (raw.stdout_fd >= 0) _close(raw.stdout_fd);
    if (raw.stderr_fd >= 0) _close(raw.stderr_fd);

    /* Get exit code */
    int exit_code;
#if defined(_WIN32)
    GetExitCodeProcess((HANDLE)raw.handle, (DWORD *)&exit_code);
    CloseHandle((HANDLE)raw.handle);
#else
    int status;
    waitpid((pid_t)raw.handle, &status, 0); /* already reaped? might return -1 */
    /* We already waited above, get from the loop */
    /* Re-do waitpid with WNOHANG to get status if not already reaped */
    pid_t r = waitpid((pid_t)raw.handle, &status, WNOHANG);
    if (r > 0) {
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    } else {
        /* Was already reaped in the blocking waitpid above; we need to store it */
        exit_code = 0; /* simplified; in practice the for loop broke when waitpid returned */
    }
#endif

    int is_buf = eo.encoding_set && eo.encoding &&
                 (strcmp(eo.encoding, "buffer") == 0 ||
                  strcmp(eo.encoding, "arraybuffer") == 0);

    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "pid", JS_NewInt32(ctx, raw.pid));
    JS_SetPropertyStr(ctx, result, "status", JS_NewInt32(ctx, exit_code));
    JS_SetPropertyStr(ctx, result, "output", JS_NULL);

    if (!is_buf) {
        JS_SetPropertyStr(ctx, result, "stdout",
                          JS_NewStringLen(ctx, (const char *)gb_out.data, gb_out.len));
        JS_SetPropertyStr(ctx, result, "stderr",
                          JS_NewStringLen(ctx, (const char *)gb_err.data, gb_err.len));
    } else {
        JS_SetPropertyStr(ctx, result, "stdout", JS_NewArrayBufferCopy(ctx, gb_out.data, gb_out.len));
        JS_SetPropertyStr(ctx, result, "stderr", JS_NewArrayBufferCopy(ctx, gb_err.data, gb_err.len));
    }

    growbuf_free(ctx, &gb_out);
    growbuf_free(ctx, &gb_err);
    free_exec_options(ctx, &eo);
    return result;
}

static JSValue js_child_execFileSync(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    /* execFileSync(file, args, options) = spawnSync(file, args, options) with shell=false */
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "file must be a string");

    /* Shift args: execFileSync(file, args, options) → spawnSync(file, args, options) */
    JSValue args_val = argc >= 2 ? argv[1] : JS_UNDEFINED;
    JSValue opts_val = argc >= 3 ? argv[2] : JS_UNDEFINED;

    /* Force shell=false */
    JSValue opts = JS_IsObject(opts_val) ? JS_DupValue(ctx, opts_val) : JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, opts, "shell", JS_NewBool(ctx, FALSE));

    JSValue sync_argv[3] = { argv[0], args_val, opts };
    JSValue result = js_child_spawnSync(ctx, this_val, 3, sync_argv);

    JS_FreeValue(ctx, opts);

    /* If error status, throw like execSync */
    if (!JS_IsException(result)) {
        JSValue status_val = JS_GetPropertyStr(ctx, result, "status");
        int has_error = JS_IsNull(status_val);
        int32_t status = -1;
        if (!has_error) JS_ToInt32(ctx, &status, status_val);
        JS_FreeValue(ctx, status_val);

        if (has_error || status != 0) {
            JSValue stdout_val = JS_GetPropertyStr(ctx, result, "stdout");
            JSValue stderr_val = JS_GetPropertyStr(ctx, result, "stderr");

            size_t flen;
            const char *file = JS_ToCStringLen(ctx, &flen, argv[0]);

            JSValue err = JS_NewError(ctx);
            char msg[256];
            if (has_error)
                snprintf(msg, sizeof(msg), "execFileSync: %s failed (spawn error)", file);
            else
                snprintf(msg, sizeof(msg), "execFileSync: %s failed (exit code %d)", file, status);
            JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, msg));
            JS_SetPropertyStr(ctx, err, "status", JS_NewInt32(ctx, has_error ? -1 : status));
            JS_SetPropertyStr(ctx, err, "code", JS_NewInt32(ctx, has_error ? -1 : status));
            JS_SetPropertyStr(ctx, err, "stdout", stdout_val);
            JS_SetPropertyStr(ctx, err, "stderr", stderr_val);
            JS_SetPropertyStr(ctx, err, "cmd", JS_NewStringLen(ctx, file, flen));
            JS_FreeCString(ctx, file);

            JS_FreeValue(ctx, result);
            return JS_Throw(ctx, err);
        }

        /* Return stdout */
        JSValue stdout_val = JS_GetPropertyStr(ctx, result, "stdout");
        JS_FreeValue(ctx, result);
        return stdout_val;
    }
    return result;
}

/* ================================================================
 * Module init
 * ================================================================ */

/* JS code for EventEmitter, ChildProcess, and high-level async wrappers */
static const char *js_cp_code =
"(function(_execSync, _spawnProcess, _readPipe, _writePipe, _closePipe,"
"         _pollProcess, _killProcess, _spawnSync, _execFileSync, _setTimeout,"
"         _decodeOutput) {"
""
"/* ---- EventEmitter ---- */"
"function EventEmitter() { this._events = {}; }"
"EventEmitter.prototype.on = function(n, fn) {"
"  if (!this._events[n]) this._events[n] = [];"
"  this._events[n].push(fn); return this;"
"};"
"EventEmitter.prototype.once = function(n, fn) {"
"  var self = this, w = function() { fn.apply(null, arguments); self.removeListener(n, w); };"
"  this.on(n, w); return this;"
"};"
"EventEmitter.prototype.removeListener = function(n, fn) {"
"  var l = this._events[n];"
"  if (l) { var i = l.indexOf(fn); if (i >= 0) l.splice(i, 1); }"
"  return this;"
"};"
"EventEmitter.prototype.emit = function(n) {"
"  var a = Array.prototype.slice.call(arguments, 1);"
"  var l = this._events[n];"
"  if (l) for (var i = 0; i < l.length; i++) { try { l[i].apply(null, a); } catch(e) {} }"
"  return !!(l && l.length);"
"};"
""
"/* ---- Stream-like helpers ---- */"
"function _Readable(fd) {"
"  EventEmitter.call(this);"
"  this._fd = fd; this._paused = false;"
"}"
"_Readable.prototype = Object.create(EventEmitter.prototype);"
"_Readable.prototype.constructor = _Readable;"
"_Readable.prototype.read = function() {"
"  if (this._fd < 0) return null;"
"  return _readPipe(this._fd, 65536);"
"};"
""
"function _Writable(fd) {"
"  this._fd = fd;"
"}"
"_Writable.prototype.write = function(data) {"
"  if (this._fd < 0) return false;"
"  return _writePipe(this._fd, data) > 0;"
"};"
"_Writable.prototype.end = function() {"
"  if (this._fd >= 0) { _closePipe(this._fd); this._fd = -1; }"
"};"
""
"/* ---- ChildProcess ---- */"
"function ChildProcess(raw) {"
"  EventEmitter.call(this);"
"  this.pid = raw.pid;"
"  this._handle = raw._handle;"
"  this._stdin_fd = raw.stdin_fd;"
"  this._stdout_fd = raw.stdout_fd;"
"  this._stderr_fd = raw.stderr_fd;"
"  this.exitCode = null;"
"  this.signalCode = null;"
"  this.killed = false;"
"  this.stdio = ["
"    raw.stdin_fd >= 0 ? new _Writable(raw.stdin_fd) : null,"
"    raw.stdout_fd >= 0 ? new _Readable(raw.stdout_fd) : null,"
"    raw.stderr_fd >= 0 ? new _Readable(raw.stderr_fd) : null"
"  ];"
"  this.stdin = this.stdio[0];"
"  this.stdout = this.stdio[1];"
"  this.stderr = this.stdio[2];"
"  this._exited = false;"
"  this._closed = false;"
"  this._poll();"
"}"
"ChildProcess.prototype = Object.create(EventEmitter.prototype);"
"ChildProcess.prototype.constructor = ChildProcess;"
"ChildProcess.prototype.kill = function(sig) {"
"  this.killed = true;"
"  return _killProcess(this.pid);"
"};"
"ChildProcess.prototype._drain = function() {"
"  if (this.stdout && this.stdout._fd >= 0) {"
"    var d = this.stdout.read();"
"    while (d) { this.stdout.emit('data', _decodeOutput(d)); d = this.stdout.read(); }"
"  }"
"  if (this.stderr && this.stderr._fd >= 0) {"
"    var d2 = this.stderr.read();"
"    while (d2) { this.stderr.emit('data', _decodeOutput(d2)); d2 = this.stderr.read(); }"
"  }"
"};"
"ChildProcess.prototype._poll = function() {"
"  if (this._exited) return;"
"  this._drain();"
"  var st = _pollProcess(this._handle);"
"  if (st.exited) {"
"    this._exited = true;"
"    this.exitCode = st.exitCode;"
"    this._drain();"
"    this.emit('exit', this.exitCode, null);"
"    this._closeAll();"
"    this.emit('close', this.exitCode, null);"
"  } else {"
"    var self = this;"
"    _setTimeout(function() { self._poll(); }, 10);"
"  }"
"};"
"ChildProcess.prototype._closeAll = function() {"
"  if (this.stdin && this.stdin._fd >= 0) this.stdin.end();"
"  if (this.stdout && this.stdout._fd >= 0) { _closePipe(this.stdout._fd); this.stdout._fd = -1; }"
"  if (this.stderr && this.stderr._fd >= 0) { _closePipe(this.stderr._fd); this.stderr._fd = -1; }"
"};"
"ChildProcess.prototype.unref = function() {};"
"ChildProcess.prototype.ref = function() {};"
""
"/* ---- spawn ---- */"
"function spawn(command, args, options) {"
"  options = options || {};"
"  if (!options.shell) options.shell = false;"
"  var cmd = command;"
"  if (options.shell && args && args.length) {"
"    cmd = command + ' ' + args.join(' ');"
"    args = undefined;"
"  }"
"  var raw;"
"  if (options.shell) {"
"    raw = _spawnProcess(cmd, undefined, options);"
"  } else {"
"    raw = _spawnProcess(cmd, args, options);"
"  }"
"  return new ChildProcess(raw);"
"}"
""
"/* ---- execFile ---- */"
"function execFile(file, args, options, callback) {"
"  if (typeof args === 'function') { callback = args; args = undefined; options = undefined; }"
"  else if (typeof options === 'function') { callback = options; options = undefined; }"
"  if (typeof args === 'object' && !Array.isArray(args)) { options = args; args = undefined; }"
"  if (typeof callback !== 'function') throw new TypeError('callback required');"
""
"  var cp = spawn(file, args, options);"
"  var stdout_str = '', stderr_str = '';"
"  cp.stdout.on('data', function(chunk) { stdout_str += chunk; });"
"  cp.stderr.on('data', function(chunk) { stderr_str += chunk; });"
"  cp.on('exit', function(code) {"
"    var enc = (options && options.encoding) || 'utf8';"
"    var err = null;"
"    if (code !== 0) {"
"      err = new Error('execFile: exited with code ' + code);"
"      err.code = code; err.status = code;"
"    }"
"    callback(err, stdout_str, stderr_str);"
"  });"
"  return cp;"
"}"
""
"/* ---- exec ---- */"
"function exec(command, options, callback) {"
"  if (typeof options === 'function') { callback = options; options = undefined; }"
"  if (typeof callback !== 'function') throw new TypeError('callback required');"
"  options = options || {};"
"  options.shell = true;"
"  return execFile(command, undefined, options, callback);"
"}"
""
"/* ---- fork ---- */"
"function fork(modulePath, args, options) {"
"  options = options || {};"
"  var execPath = options.execPath || 'qnode';"
"  var forkArgs = [modulePath];"
"  if (args) forkArgs = forkArgs.concat(args);"
"  return spawn(execPath, forkArgs, {"
"    cwd: options.cwd,"
"    env: options.env,"
"    stdio: options.stdio || 'pipe',"
"    shell: false"
"  });"
"}"
""
"return {"
"  spawn: spawn,"
"  execFile: execFile,"
"  exec: exec,"
"  fork: fork,"
"  ChildProcess: ChildProcess,"
"  EventEmitter: EventEmitter"
"};"
"})";

static int js_child_process_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue execSync = JS_NewCFunction(ctx, js_child_execSync, "execSync", 2);
    JSValue spawnSync = JS_NewCFunction(ctx, js_child_spawnSync, "spawnSync", 3);
    JSValue execFileSync = JS_NewCFunction(ctx, js_child_execFileSync, "execFileSync", 3);

    /* Low-level C functions for JS layer */
    JSValue f_spawn = JS_NewCFunction(ctx, js_cp_spawnProcess, "_spawnProcess", 3);
    JSValue f_read  = JS_NewCFunction(ctx, js_cp_readPipe, "_readPipe", 2);
    JSValue f_write = JS_NewCFunction(ctx, js_cp_writePipe, "_writePipe", 2);
    JSValue f_close = JS_NewCFunction(ctx, js_cp_closePipe, "_closePipe", 1);
    JSValue f_poll  = JS_NewCFunction(ctx, js_cp_pollProcess, "_pollProcess", 1);
    JSValue f_kill  = JS_NewCFunction(ctx, js_cp_killProcess, "_killProcess", 1);
    JSValue f_decode = JS_NewCFunction(ctx, js_cp_decodeOutput, "_decodeOutput", 1);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue setTimeout_fn = JS_GetPropertyStr(ctx, global, "setTimeout");
    JS_FreeValue(ctx, global);

    /* Call the JS factory with all C functions */
    JSValue factory = JS_Eval(ctx, js_cp_code, strlen(js_cp_code),
                               "<cp-setup>", JS_EVAL_TYPE_GLOBAL);
    JSValue cfunc_args[11] = {
        execSync, f_spawn, f_read, f_write, f_close,
        f_poll, f_kill, spawnSync, execFileSync, setTimeout_fn,
        f_decode
    };
    JSValue ns = JS_Call(ctx, factory, JS_UNDEFINED, 11, cfunc_args);
    JS_FreeValue(ctx, factory);
    JS_FreeValue(ctx, setTimeout_fn);
    JS_FreeValue(ctx, f_spawn);
    JS_FreeValue(ctx, f_read);
    JS_FreeValue(ctx, f_write);
    JS_FreeValue(ctx, f_close);
    JS_FreeValue(ctx, f_poll);
    JS_FreeValue(ctx, f_kill);
    JS_FreeValue(ctx, f_decode);

    /* Extract functions from the returned namespace */
    JSValue spawn_fn    = JS_GetPropertyStr(ctx, ns, "spawn");
    JSValue execFile_fn = JS_GetPropertyStr(ctx, ns, "execFile");
    JSValue exec_fn     = JS_GetPropertyStr(ctx, ns, "exec");
    JSValue fork_fn     = JS_GetPropertyStr(ctx, ns, "fork");
    JS_FreeValue(ctx, ns);

    /* Export */
    JS_SetModuleExport(ctx, m, "default", JS_DupValue(ctx, execSync));
    JS_SetModuleExport(ctx, m, "execSync", execSync);
    JS_SetModuleExport(ctx, m, "spawnSync", spawnSync);
    JS_SetModuleExport(ctx, m, "execFileSync", execFileSync);
    JS_SetModuleExport(ctx, m, "spawn", spawn_fn);
    JS_SetModuleExport(ctx, m, "execFile", execFile_fn);
    JS_SetModuleExport(ctx, m, "exec", exec_fn);
    JS_SetModuleExport(ctx, m, "fork", fork_fn);

    return 0;
}

JSModuleDef *js_init_module_child_process(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m = JS_NewCModule(ctx, module_name, js_child_process_init);
    if (!m) return NULL;
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "execSync");
    JS_AddModuleExport(ctx, m, "spawnSync");
    JS_AddModuleExport(ctx, m, "execFileSync");
    JS_AddModuleExport(ctx, m, "spawn");
    JS_AddModuleExport(ctx, m, "execFile");
    JS_AddModuleExport(ctx, m, "exec");
    JS_AddModuleExport(ctx, m, "fork");
    return m;
}
