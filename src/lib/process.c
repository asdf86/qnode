/*
 * Node.js Process Module for QNode
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
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#if defined(_WIN32)
#include <windows.h>
#include <conio.h>
#include <utime.h>
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
#else
#include <dlfcn.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#if defined(__FreeBSD__)
extern char **environ;
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
typedef sig_t sighandler_t;
#endif

#if defined(__APPLE__)
#if !defined(environ)
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#endif
#endif /* __APPLE__ */

#endif
#include "cutils.h"
#include "list.h"
#include "quickjs-libc.h"

#if !defined(PATH_MAX)
#define PATH_MAX 4096
#endif

/* ---- Platform & Arch defines ---- */

#if defined(_WIN32)
#define PLATFORM "win32"
#elif defined(__APPLE__)
#define PLATFORM "darwin"
#elif defined(__linux__)
#define PLATFORM "linux"
#elif defined(__FreeBSD__)
#define PLATFORM "freebsd"
#else
#define PLATFORM "unknown"
#endif

#if defined(__x86_64__)
#define ARCH   "x64"
#elif defined(__i386__)
#define ARCH   "ia32"
#elif defined(__aarch64__)
#define ARCH   "arm64"
#elif defined(__arm__)
#define ARCH   "arm"
#else
#define ARCH   "unknown"
#endif

/* ---- Global state set from main() ---- */

static const char *_process_exec_path;
static int _process_argc;
static char **_process_argv;
static char **_process_exec_argv;   /* qnode-specific flags (before script) */
static int _process_exec_argc;

void js_process_set_argv(JSContext *ctx, const char *exec_path,
                         int argc, char **argv)
{
    _process_exec_path = exec_path;
    _process_argc = argc;
    _process_argv = argv;
}

void js_process_set_exec_argv(int argc, char **argv)
{
    _process_exec_argc = argc;
    _process_exec_argv = argv;
}

/* ---- errno ---- */

static const JSCFunctionListEntry js_process_errno_props[] = {
#define DEF(x) JS_PROP_INT32_DEF(#x, x, JS_PROP_CONFIGURABLE )
    DEF(EINVAL),
    DEF(EIO),
    DEF(EACCES),
    DEF(EEXIST),
    DEF(ENOSPC),
    DEF(ENOSYS),
    DEF(EBUSY),
    DEF(ENOENT),
    DEF(EPERM),
    DEF(EPIPE),
    DEF(EBADF),
#undef DEF
};

/* ---- Helper: build argv array ---- */

static JSValue js_process_build_argv(JSContext *ctx)
{
    JSValue arr = JS_NewArray(ctx);
    int i, idx = 0;
    const char *exec = _process_exec_path ? _process_exec_path : "qnode";

    JS_SetPropertyUint32(ctx, arr, idx++, JS_NewString(ctx, exec));
    for (i = 0; i < _process_argc; i++)
        JS_SetPropertyUint32(ctx, arr, idx++, JS_NewString(ctx, _process_argv[i]));
    return arr;
}

/* ---- Helper: build env object ---- */

static JSValue js_process_build_env(JSContext *ctx)
{
    JSValue obj = JS_NewObject(ctx);
#if defined(_WIN32)
    /* Windows: use GetEnvironmentStrings */
    char *env_block = GetEnvironmentStrings();
    if (env_block) {
        char *p = env_block;
        while (*p) {
            /* skip entries starting with '=' (internal Windows vars) */
            if (*p != '=') {
                char *eq = strchr(p, '=');
                if (eq) {
                    JSValue key = JS_NewStringLen(ctx, p, eq - p);
                    JSValue val = JS_NewString(ctx, eq + 1);
                    JS_SetProperty(ctx, obj,
                                   JS_ValueToAtom(ctx, key), val);
                    JS_FreeValue(ctx, key);
                }
            }
            p += strlen(p) + 1;
        }
        FreeEnvironmentStrings(env_block);
    }
#else
    /* Unix: iterate environ */
    char **env = environ;
    if (env) {
        for (; *env; env++) {
            char *eq = strchr(*env, '=');
            if (eq) {
                JSValue key = JS_NewStringLen(ctx, *env, eq - *env);
                JSValue val = JS_NewString(ctx, eq + 1);
                JS_SetProperty(ctx, obj,
                               JS_ValueToAtom(ctx, key), val);
                JS_FreeValue(ctx, key);
            }
        }
    }
#endif
    return obj;
}

/* ---- process.exit([code]) ---- */

static JSValue js_process_exit(JSContext *ctx, JSValue this_val,
                               int argc, JSValue *argv)
{
    int code = 0;
    if (argc > 0 && !JS_IsUndefined(argv[0]))
        JS_ToInt32(ctx, &code, argv[0]);
    exit(code);
    return JS_UNDEFINED; /* unreachable */
}

/* ---- process.cwd() ---- */

static JSValue js_process_cwd(JSContext *ctx, JSValue this_val,
                               int argc, JSValue *argv)
{
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf)))
        return JS_ThrowTypeError(ctx, "could not get current directory");
    return JS_NewString(ctx, buf);
}

/* ---- process.chdir(directory) ---- */

static JSValue js_process_chdir(JSContext *ctx, JSValue this_val,
                                int argc, JSValue *argv)
{
    const char *dir;
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "directory argument must be a string");
    dir = JS_ToCString(ctx, argv[0]);
    if (!dir)
        return JS_EXCEPTION;
    if (chdir(dir) != 0) {
        JS_ThrowTypeError(ctx, "could not change directory to '%s'", dir);
        JS_FreeCString(ctx, dir);
        return JS_EXCEPTION;
    }
    JS_FreeCString(ctx, dir);
    return JS_UNDEFINED;
}

/* ---- process.getgid() ---- */

static JSValue js_process_getgid(JSContext *ctx, JSValue this_val,
                                 int argc, JSValue *argv)
{
#if defined(_WIN32)
    return JS_ThrowTypeError(ctx, "process.getgid() is not supported on Windows");
#else
    return JS_NewInt32(ctx, (int32_t)getgid());
#endif
}

/* ---- process.now() ---- */

static JSValue js_os_now(JSContext *ctx, JSValue this_val,
                         int argc, JSValue *argv)
{
    return JS_NewFloat64(ctx, (double)10 / 1e6);
}

/* ---- Module init ---- */

static int js_process_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue proc_obj, val;

    /* Build process object (default export) */
    proc_obj = JS_NewObject(ctx);

    /* string properties */
    JS_SetPropertyStr(ctx, proc_obj, "arch", JS_NewString(ctx, ARCH));
    JS_SetPropertyStr(ctx, proc_obj, "platform", JS_NewString(ctx, PLATFORM));
    JS_SetPropertyStr(ctx, proc_obj, "argv", js_process_build_argv(ctx));
    JS_SetPropertyStr(ctx, proc_obj, "argv0",
                      _process_exec_path
                          ? JS_NewString(ctx, _process_exec_path)
                          : JS_UNDEFINED);
    JS_SetPropertyStr(ctx, proc_obj, "execPath",
                      _process_exec_path
                          ? JS_NewString(ctx, _process_exec_path)
                          : JS_NewString(ctx, "qnode"));

    /* pid */
    JS_SetPropertyStr(ctx, proc_obj, "pid", JS_NewInt32(ctx, (int32_t)getpid()));

    /* exitCode — writable, default undefined */
    JS_SetPropertyStr(ctx, proc_obj, "exitCode", JS_UNDEFINED);

    /* env */
    JS_SetPropertyStr(ctx, proc_obj, "env", js_process_build_env(ctx));

    /* execArgv */
    {
        JSValue exec_argv_arr = JS_NewArray(ctx);
        int i;
        for (i = 0; i < _process_exec_argc; i++)
            JS_SetPropertyUint32(ctx, exec_argv_arr, i,
                                 JS_NewString(ctx, _process_exec_argv[i]));
        JS_SetPropertyStr(ctx, proc_obj, "execArgv", exec_argv_arr);
    }

    /* functions */
    JS_SetPropertyStr(ctx, proc_obj, "exit",
                      JS_NewCFunction(ctx, js_process_exit, "exit", 1));
    JS_SetPropertyStr(ctx, proc_obj, "cwd",
                      JS_NewCFunction(ctx, js_process_cwd, "cwd", 0));
    JS_SetPropertyStr(ctx, proc_obj, "chdir",
                      JS_NewCFunction(ctx, js_process_chdir, "chdir", 1));
    JS_SetPropertyStr(ctx, proc_obj, "getgid",
                      JS_NewCFunction(ctx, js_process_getgid, "getgid", 0));
    JS_SetPropertyStr(ctx, proc_obj, "now",
                      JS_NewCFunction(ctx, js_os_now, "now", 0));

    /* errno sub-object */
    val = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, val, js_process_errno_props,
                               countof(js_process_errno_props));
    JS_SetPropertyStr(ctx, proc_obj, "errno", val);

    /* default export: process object */
    if (JS_SetModuleExport(ctx, m, "default", proc_obj) < 0)
        return -1;
    /* named exports */
    if (JS_SetModuleExport(ctx, m, "arch", JS_NewString(ctx, ARCH)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "argv", js_process_build_argv(ctx)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "argv0",
                           _process_exec_path
                               ? JS_NewString(ctx, _process_exec_path)
                               : JS_UNDEFINED) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "execPath",
                           _process_exec_path
                               ? JS_NewString(ctx, _process_exec_path)
                               : JS_NewString(ctx, "qnode")) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "platform", JS_NewString(ctx, PLATFORM)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "pid",
                           JS_NewInt32(ctx, (int32_t)getpid())) < 0)
        return -1;

    return 0;
}

JSModuleDef *js_init_module_process(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_process_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "arch");
    JS_AddModuleExport(ctx, m, "argv");
    JS_AddModuleExport(ctx, m, "argv0");
    JS_AddModuleExport(ctx, m, "execPath");
    JS_AddModuleExport(ctx, m, "platform");
    JS_AddModuleExport(ctx, m, "pid");
    return m;
}
