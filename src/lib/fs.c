/*
 * Node.js File System Module for QNode
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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define access_func(path, mode) _access(path, mode)
#if !defined(S_ISDIR)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#if !defined(S_ISREG)
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#define S_ISLNK(mode) (0)
#define lstat stat
#else
#include <unistd.h>
#define access_func(path, mode) access(path, mode)
#endif

#if defined(_WIN32)
#define fsync_fd(fd) _commit(fd)
#else
#define fsync_fd(fd) fsync(fd)
#endif

#include "cutils.h"
#include "quickjs-libc.h"

#if !defined(PATH_MAX)
#define PATH_MAX 4096
#endif

/* ---- Error helper ---- */

static JSValue js_fs_throw_error(JSContext *ctx, int err, const char *path,
                                 const char *syscall)
{
    JSValue obj;
    char buf[1024];
    const char *code;

    switch (err) {
    case ENOENT:  code = "ENOENT"; break;
    case EACCES:  code = "EACCES"; break;
    case EEXIST:  code = "EEXIST"; break;
    case EISDIR:  code = "EISDIR"; break;
    case ENOTDIR: code = "ENOTDIR"; break;
    case EPERM:   code = "EPERM"; break;
    case ENOSPC:  code = "ENOSPC"; break;
    case EBUSY:   code = "EBUSY"; break;
    case EIO:     code = "EIO"; break;
    default:      code = "UNKNOWN"; break;
    }

    obj = JS_NewError(ctx);
    if (path)
        snprintf(buf, sizeof(buf), "%s: %s, %s '%s'",
                 syscall, strerror(err), code, path);
    else
        snprintf(buf, sizeof(buf), "%s: %s, %s",
                 syscall, strerror(err), code);

    JS_SetPropertyStr(ctx, obj, "message", JS_NewString(ctx, buf));
    JS_SetPropertyStr(ctx, obj, "errno", JS_NewInt32(ctx, err < 0 ? -err : err));
    JS_SetPropertyStr(ctx, obj, "code", JS_NewString(ctx, code));
    if (path)
        JS_SetPropertyStr(ctx, obj, "path", JS_NewString(ctx, path));
    if (syscall)
        JS_SetPropertyStr(ctx, obj, "syscall", JS_NewString(ctx, syscall));
    return JS_Throw(ctx, obj);
}

/* ---- Encoding check ---- */

static int js_fs_is_text_encoding(const char *encoding)
{
    if (!encoding) return 0;
    return (strcmp(encoding, "utf8") == 0 ||
            strcmp(encoding, "utf-8") == 0 ||
            strcmp(encoding, "ascii") == 0 ||
            strcmp(encoding, "latin1") == 0 ||
            strcmp(encoding, "binary") == 0);
}

/* ---- ArrayBuffer helper ---- */

static void js_fs_ab_free(JSRuntime *rt, void *opaque, void *ptr)
{
    js_free_rt(rt, ptr);
}

static JSValue js_fs_new_buffer(JSContext *ctx, size_t size)
{
    uint8_t *buf = js_mallocz(ctx, size);
    if (!buf) return JS_EXCEPTION;
    return JS_NewArrayBuffer(ctx, buf, size, js_fs_ab_free, NULL, FALSE);
}

/* ---- Open flags parser ---- */

static int js_fs_parse_open_flags(const char *flags)
{
    if (!flags || flags[0] == '\0') return O_RDONLY;
    if (flags[0] == 'r') {
        if (flags[1] == '+') return O_RDWR;
        return O_RDONLY;
    }
    if (flags[0] == 'w') {
        if (flags[1] == 'x') return O_WRONLY | O_CREAT | O_TRUNC | O_EXCL;
        if (flags[1] == '+') return O_RDWR | O_CREAT | O_TRUNC;
        return O_WRONLY | O_CREAT | O_TRUNC;
    }
    if (flags[0] == 'a') {
        if (flags[1] == 'x') return O_WRONLY | O_CREAT | O_APPEND | O_EXCL;
        if (flags[1] == '+') return O_RDWR | O_CREAT | O_APPEND;
        return O_WRONLY | O_CREAT | O_APPEND;
    }
    return O_RDONLY;
}

/* ---- Stats object helpers ---- */

static JSValue js_fs_stats_isFile(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSValue mode_val = JS_GetPropertyStr(ctx, this_val, "_mode");
    int32_t mode = 0;
    JS_ToInt32(ctx, &mode, mode_val);
    JS_FreeValue(ctx, mode_val);
    return JS_NewBool(ctx, S_ISREG(mode));
}

static JSValue js_fs_stats_isDirectory(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSValue mode_val = JS_GetPropertyStr(ctx, this_val, "_mode");
    int32_t mode = 0;
    JS_ToInt32(ctx, &mode, mode_val);
    JS_FreeValue(ctx, mode_val);
    return JS_NewBool(ctx, S_ISDIR(mode));
}

static JSValue js_fs_stats_isSymbolicLink(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    JSValue mode_val = JS_GetPropertyStr(ctx, this_val, "_mode");
    int32_t mode = 0;
    JS_ToInt32(ctx, &mode, mode_val);
    JS_FreeValue(ctx, mode_val);
    return JS_NewBool(ctx, S_ISLNK(mode));
}

static JSValue js_fs_build_stats(JSContext *ctx, const struct stat *st)
{
    JSValue obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "dev", JS_NewInt64(ctx, st->st_dev));
    JS_SetPropertyStr(ctx, obj, "ino", JS_NewInt64(ctx, st->st_ino));
    JS_SetPropertyStr(ctx, obj, "mode", JS_NewInt32(ctx, st->st_mode));
    JS_SetPropertyStr(ctx, obj, "nlink", JS_NewInt64(ctx, st->st_nlink));
    JS_SetPropertyStr(ctx, obj, "uid", JS_NewInt64(ctx, st->st_uid));
    JS_SetPropertyStr(ctx, obj, "gid", JS_NewInt64(ctx, st->st_gid));
    JS_SetPropertyStr(ctx, obj, "rdev", JS_NewInt64(ctx, st->st_rdev));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewInt64(ctx, st->st_size));

#if defined(_WIN32)
    JS_SetPropertyStr(ctx, obj, "blksize", JS_NewInt32(ctx, -1));
    JS_SetPropertyStr(ctx, obj, "blocks", JS_NewInt32(ctx, -1));
#else
    JS_SetPropertyStr(ctx, obj, "blksize", JS_NewInt64(ctx, st->st_blksize));
    JS_SetPropertyStr(ctx, obj, "blocks", JS_NewInt64(ctx, st->st_blocks));
#endif

    /* Timestamps in milliseconds */
#if defined(_WIN32)
    {
        int64_t atime_ms = (int64_t)st->st_atime * 1000;
        int64_t mtime_ms = (int64_t)st->st_mtime * 1000;
        int64_t ctime_ms = (int64_t)st->st_ctime * 1000;
        int64_t birthtime_ms = mtime_ms;
        JS_SetPropertyStr(ctx, obj, "atimeMs", JS_NewFloat64(ctx, (double)atime_ms));
        JS_SetPropertyStr(ctx, obj, "mtimeMs", JS_NewFloat64(ctx, (double)mtime_ms));
        JS_SetPropertyStr(ctx, obj, "ctimeMs", JS_NewFloat64(ctx, (double)ctime_ms));
        JS_SetPropertyStr(ctx, obj, "birthtimeMs", JS_NewFloat64(ctx, (double)birthtime_ms));
        JS_SetPropertyStr(ctx, obj, "atime", JS_NewFloat64(ctx, (double)atime_ms));
        JS_SetPropertyStr(ctx, obj, "mtime", JS_NewFloat64(ctx, (double)mtime_ms));
        JS_SetPropertyStr(ctx, obj, "ctime", JS_NewFloat64(ctx, (double)ctime_ms));
        JS_SetPropertyStr(ctx, obj, "birthtime", JS_NewFloat64(ctx, (double)birthtime_ms));
    }
#elif defined(__APPLE__)
    {
        int64_t atime_ms = (int64_t)st->st_atimespec.tv_sec * 1000 + st->st_atimespec.tv_nsec / 1000000;
        int64_t mtime_ms = (int64_t)st->st_mtimespec.tv_sec * 1000 + st->st_mtimespec.tv_nsec / 1000000;
        int64_t ctime_ms = (int64_t)st->st_ctimespec.tv_sec * 1000 + st->st_ctimespec.tv_nsec / 1000000;
        int64_t birthtime_ms = mtime_ms;
        JS_SetPropertyStr(ctx, obj, "atimeMs", JS_NewFloat64(ctx, (double)atime_ms));
        JS_SetPropertyStr(ctx, obj, "mtimeMs", JS_NewFloat64(ctx, (double)mtime_ms));
        JS_SetPropertyStr(ctx, obj, "ctimeMs", JS_NewFloat64(ctx, (double)ctime_ms));
        JS_SetPropertyStr(ctx, obj, "birthtimeMs", JS_NewFloat64(ctx, (double)birthtime_ms));
        JS_SetPropertyStr(ctx, obj, "atime", JS_NewFloat64(ctx, (double)atime_ms));
        JS_SetPropertyStr(ctx, obj, "mtime", JS_NewFloat64(ctx, (double)mtime_ms));
        JS_SetPropertyStr(ctx, obj, "ctime", JS_NewFloat64(ctx, (double)ctime_ms));
        JS_SetPropertyStr(ctx, obj, "birthtime", JS_NewFloat64(ctx, (double)birthtime_ms));
    }
#else
    {
        int64_t atime_ms = (int64_t)st->st_atim.tv_sec * 1000 + st->st_atim.tv_nsec / 1000000;
        int64_t mtime_ms = (int64_t)st->st_mtim.tv_sec * 1000 + st->st_mtim.tv_nsec / 1000000;
        int64_t ctime_ms = (int64_t)st->st_ctim.tv_sec * 1000 + st->st_ctim.tv_nsec / 1000000;
        int64_t birthtime_ms = mtime_ms;
        JS_SetPropertyStr(ctx, obj, "atimeMs", JS_NewFloat64(ctx, (double)atime_ms));
        JS_SetPropertyStr(ctx, obj, "mtimeMs", JS_NewFloat64(ctx, (double)mtime_ms));
        JS_SetPropertyStr(ctx, obj, "ctimeMs", JS_NewFloat64(ctx, (double)ctime_ms));
        JS_SetPropertyStr(ctx, obj, "birthtimeMs", JS_NewFloat64(ctx, (double)birthtime_ms));
        JS_SetPropertyStr(ctx, obj, "atime", JS_NewFloat64(ctx, (double)atime_ms));
        JS_SetPropertyStr(ctx, obj, "mtime", JS_NewFloat64(ctx, (double)mtime_ms));
        JS_SetPropertyStr(ctx, obj, "ctime", JS_NewFloat64(ctx, (double)ctime_ms));
        JS_SetPropertyStr(ctx, obj, "birthtime", JS_NewFloat64(ctx, (double)birthtime_ms));
    }
#endif

    /* Store mode for isXXX() methods */
    JS_SetPropertyStr(ctx, obj, "_mode", JS_NewInt32(ctx, st->st_mode));

    JS_SetPropertyStr(ctx, obj, "isFile",
                      JS_NewCFunction(ctx, js_fs_stats_isFile, "isFile", 0));
    JS_SetPropertyStr(ctx, obj, "isDirectory",
                      JS_NewCFunction(ctx, js_fs_stats_isDirectory, "isDirectory", 0));
    JS_SetPropertyStr(ctx, obj, "isSymbolicLink",
                      JS_NewCFunction(ctx, js_fs_stats_isSymbolicLink, "isSymbolicLink", 0));

    return obj;
}

/* ---- Recursive mkdir helper ---- */

static int mkdir_recursive(const char *path, int mode)
{
    char tmp[PATH_MAX];
    char *p;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
        tmp[--len] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
#if defined(_WIN32)
            _mkdir(tmp);
#else
            mkdir(tmp, mode);
#endif
            *p = '/';
        }
    }
#if defined(_WIN32)
    return _mkdir(tmp);
#else
    return mkdir(tmp, mode);
#endif
}

/* ---- Recursive rm helper ---- */

static int rm_recursive(const char *path)
{
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *entry;
    char child[PATH_MAX];
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(child, &st) == 0) {
            if (S_ISDIR(st.st_mode))
                rm_recursive(child);
            else
                unlink(child);
        }
    }
    closedir(d);
    return rmdir(path);
}

/* ---- readFileSync(path[, options]) ---- */

static JSValue js_fs_readFileSync(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    const char *path, *encoding = NULL;
    FILE *f;
    long file_size;
    uint8_t *buf;
    JSValue ret;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    /* Parse options */
    if (argc >= 2) {
        if (JS_IsString(argv[1])) {
            encoding = JS_ToCString(ctx, argv[1]);
        } else if (JS_IsObject(argv[1])) {
            JSValue enc = JS_GetPropertyStr(ctx, argv[1], "encoding");
            if (JS_IsString(enc))
                encoding = JS_ToCString(ctx, enc);
            JS_FreeValue(ctx, enc);
        }
    }

    f = fopen(path, "rb");
    if (!f) {
        int err = errno;
        if (encoding) JS_FreeCString(ctx, encoding);
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "open");
    }

    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = (uint8_t *)js_malloc(ctx, file_size + 1);
    if (!buf) {
        fclose(f);
        if (encoding) JS_FreeCString(ctx, encoding);
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    if (file_size > 0) {
        size_t nread = fread(buf, 1, file_size, f);
        if ((long)nread != file_size) {
            int err = errno;
            js_free(ctx, buf);
            fclose(f);
            if (encoding) JS_FreeCString(ctx, encoding);
            JS_FreeCString(ctx, path);
            return js_fs_throw_error(ctx, err, path, "read");
        }
    }
    fclose(f);

    if (encoding && js_fs_is_text_encoding(encoding)) {
        ret = JS_NewStringLen(ctx, (const char *)buf, file_size);
    } else {
        ret = JS_NewArrayBufferCopy(ctx, buf, file_size);
    }

    js_free(ctx, buf);
    if (encoding) JS_FreeCString(ctx, encoding);
    JS_FreeCString(ctx, path);
    return ret;
}

/* ---- writeFileSync(file, data[, options]) ---- */

static JSValue js_fs_writeFileSync(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    const char *path;
    const char *flag_str = "w";
    const char *encoding = NULL;
    const char *flag_alloc = NULL;
    FILE *f;
    const uint8_t *data;
    size_t data_len;
    int free_cstr = 0;

    if (argc < 2)
        return JS_ThrowTypeError(ctx, "need path and data arguments");
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    /* Parse options */
    if (argc >= 3) {
        if (JS_IsString(argv[2])) {
            encoding = JS_ToCString(ctx, argv[2]);
        } else if (JS_IsObject(argv[2])) {
            JSValue enc = JS_GetPropertyStr(ctx, argv[2], "encoding");
            JSValue fl  = JS_GetPropertyStr(ctx, argv[2], "flag");
            if (JS_IsString(enc))
                encoding = JS_ToCString(ctx, enc);
            if (JS_IsString(fl))
                flag_alloc = flag_str = JS_ToCString(ctx, fl);
            JS_FreeValue(ctx, enc);
            JS_FreeValue(ctx, fl);
        }
    }

    /* Get data */
    if (JS_IsString(argv[1])) {
        size_t len;
        const char *str = JS_ToCStringLen(ctx, &len, argv[1]);
        data = (const uint8_t *)str;
        data_len = len;
        free_cstr = 1;
    } else {
        size_t buf_size;
        uint8_t *abuf = JS_GetArrayBuffer(ctx, &buf_size, argv[1]);
        if (!abuf) {
            JS_FreeCString(ctx, path);
            if (encoding) JS_FreeCString(ctx, encoding);
            if (flag_alloc) JS_FreeCString(ctx, flag_alloc);
            return JS_ThrowTypeError(ctx, "data must be a string or ArrayBuffer");
        }
        data = abuf;
        data_len = buf_size;
    }

    /* Map flag to fopen mode */
    const char *fmode;
    if (strcmp(flag_str, "a") == 0 || strcmp(flag_str, "ax") == 0)
        fmode = "ab";
    else if (strcmp(flag_str, "a+") == 0)
        fmode = "ab+";
    else if (strcmp(flag_str, "r+") == 0)
        fmode = "rb+";
    else
        fmode = "wb";

    f = fopen(path, fmode);
    if (!f) {
        int err = errno;
        if (free_cstr) JS_FreeCString(ctx, (const char *)data);
        JS_FreeCString(ctx, path);
        if (encoding) JS_FreeCString(ctx, encoding);
        if (flag_alloc) JS_FreeCString(ctx, flag_alloc);
        return js_fs_throw_error(ctx, err, path, "open");
    }

    if (data_len > 0) {
        size_t nwritten = fwrite(data, 1, data_len, f);
        if (nwritten != data_len) {
            int err = errno;
            fclose(f);
            if (free_cstr) JS_FreeCString(ctx, (const char *)data);
            JS_FreeCString(ctx, path);
            if (encoding) JS_FreeCString(ctx, encoding);
            if (flag_alloc) JS_FreeCString(ctx, flag_alloc);
            return js_fs_throw_error(ctx, err, path, "write");
        }
    }
    fclose(f);

    if (free_cstr) JS_FreeCString(ctx, (const char *)data);
    JS_FreeCString(ctx, path);
    if (encoding) JS_FreeCString(ctx, encoding);
    if (flag_alloc) JS_FreeCString(ctx, flag_alloc);
    return JS_UNDEFINED;
}

/* ---- appendFileSync(path, data[, options]) ---- */

static JSValue js_fs_appendFileSync(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    const char *path;
    const char *encoding = NULL;
    FILE *f;
    const uint8_t *data;
    size_t data_len;
    int free_cstr = 0;

    if (argc < 2)
        return JS_ThrowTypeError(ctx, "need path and data arguments");
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    /* Parse options */
    if (argc >= 3) {
        if (JS_IsString(argv[2])) {
            encoding = JS_ToCString(ctx, argv[2]);
        } else if (JS_IsObject(argv[2])) {
            JSValue enc = JS_GetPropertyStr(ctx, argv[2], "encoding");
            if (JS_IsString(enc))
                encoding = JS_ToCString(ctx, enc);
            JS_FreeValue(ctx, enc);
        }
    }

    /* Get data */
    if (JS_IsString(argv[1])) {
        size_t len;
        const char *str = JS_ToCStringLen(ctx, &len, argv[1]);
        data = (const uint8_t *)str;
        data_len = len;
        free_cstr = 1;
    } else {
        size_t buf_size;
        uint8_t *abuf = JS_GetArrayBuffer(ctx, &buf_size, argv[1]);
        if (!abuf) {
            JS_FreeCString(ctx, path);
            if (encoding) JS_FreeCString(ctx, encoding);
            return JS_ThrowTypeError(ctx, "data must be a string or ArrayBuffer");
        }
        data = abuf;
        data_len = buf_size;
    }

    f = fopen(path, "ab");
    if (!f) {
        int err = errno;
        if (free_cstr) JS_FreeCString(ctx, (const char *)data);
        JS_FreeCString(ctx, path);
        if (encoding) JS_FreeCString(ctx, encoding);
        return js_fs_throw_error(ctx, err, path, "open");
    }

    if (data_len > 0) {
        size_t nwritten = fwrite(data, 1, data_len, f);
        if (nwritten != data_len) {
            int err = errno;
            fclose(f);
            if (free_cstr) JS_FreeCString(ctx, (const char *)data);
            JS_FreeCString(ctx, path);
            if (encoding) JS_FreeCString(ctx, encoding);
            return js_fs_throw_error(ctx, err, path, "write");
        }
    }
    fclose(f);

    if (free_cstr) JS_FreeCString(ctx, (const char *)data);
    JS_FreeCString(ctx, path);
    if (encoding) JS_FreeCString(ctx, encoding);
    return JS_UNDEFINED;
}

/* ---- existsSync(path) ---- */

static JSValue js_fs_existsSync(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    const char *path;
    struct stat st;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    int ret = stat(path, &st);
    JS_FreeCString(ctx, path);
    return JS_NewBool(ctx, ret == 0);
}

/* ---- mkdirSync(path[, options]) ---- */

static JSValue js_fs_mkdirSync(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    const char *path;
    int recursive = 0;
    int mode = 0777;
    int ret;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue rec = JS_GetPropertyStr(ctx, argv[1], "recursive");
        if (JS_IsBool(rec))
            recursive = JS_ToBool(ctx, rec);
        JS_FreeValue(ctx, rec);

        JSValue mode_val = JS_GetPropertyStr(ctx, argv[1], "mode");
        if (!JS_IsUndefined(mode_val))
            JS_ToInt32(ctx, &mode, mode_val);
        JS_FreeValue(ctx, mode_val);
    }

    if (recursive) {
        ret = mkdir_recursive(path, mode);
    } else {
#if defined(_WIN32)
        ret = _mkdir(path);
#else
        ret = mkdir(path, mode);
#endif
    }

    if (ret != 0) {
        int err = errno;
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "mkdir");
    }
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
}

/* ---- readdirSync(path) ---- */

static JSValue js_fs_readdirSync(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    const char *path;
    DIR *d;
    struct dirent *entry;
    JSValue arr;
    uint32_t idx = 0;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    d = opendir(path);
    if (!d) {
        int err = errno;
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "scandir");
    }

    arr = JS_NewArray(ctx);
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        JS_DefinePropertyValueUint32(ctx, arr, idx++,
                                     JS_NewString(ctx, entry->d_name),
                                     JS_PROP_C_W_E);
    }
    closedir(d);
    JS_FreeCString(ctx, path);
    return arr;
}

/* ---- statSync(path) / lstatSync(path) ---- */

static JSValue js_fs_stat_internal(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv, int is_lstat)
{
    const char *path;
    struct stat st;
    int ret;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

#if defined(_WIN32)
    ret = stat(path, &st);
#else
    if (is_lstat)
        ret = lstat(path, &st);
    else
        ret = stat(path, &st);
#endif

    if (ret != 0) {
        int err = errno;
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "stat");
    }
    JS_FreeCString(ctx, path);
    return js_fs_build_stats(ctx, &st);
}

static JSValue js_fs_statSync(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    return js_fs_stat_internal(ctx, this_val, argc, argv, 0);
}

static JSValue js_fs_lstatSync(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    return js_fs_stat_internal(ctx, this_val, argc, argv, 1);
}

/* ---- unlinkSync(path) ---- */

static JSValue js_fs_unlinkSync(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    const char *path;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    if (unlink(path) != 0) {
        int err = errno;
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "unlink");
    }
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
}

/* ---- renameSync(oldPath, newPath) ---- */

static JSValue js_fs_renameSync(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    const char *oldpath, *newpath;

    if (argc < 2)
        return JS_ThrowTypeError(ctx, "need oldPath and newPath");
    if (!JS_IsString(argv[0]) || !JS_IsString(argv[1]))
        return JS_ThrowTypeError(ctx, "paths must be strings");

    oldpath = JS_ToCString(ctx, argv[0]);
    if (!oldpath) return JS_EXCEPTION;
    newpath = JS_ToCString(ctx, argv[1]);
    if (!newpath) {
        JS_FreeCString(ctx, oldpath);
        return JS_EXCEPTION;
    }

    if (rename(oldpath, newpath) != 0) {
        int err = errno;
        JS_FreeCString(ctx, oldpath);
        JS_FreeCString(ctx, newpath);
        return js_fs_throw_error(ctx, err, oldpath, "rename");
    }
    JS_FreeCString(ctx, oldpath);
    JS_FreeCString(ctx, newpath);
    return JS_UNDEFINED;
}

/* ---- rmdirSync(path) ---- */

static JSValue js_fs_rmdirSync(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    const char *path;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    if (rmdir(path) != 0) {
        int err = errno;
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "rmdir");
    }
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
}

/* ---- rmSync(path[, options]) ---- */

static JSValue js_fs_rmSync(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    const char *path;
    int recursive = 0, force = 0;
    struct stat st;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue rec = JS_GetPropertyStr(ctx, argv[1], "recursive");
        JSValue frc = JS_GetPropertyStr(ctx, argv[1], "force");
        if (JS_IsBool(rec)) recursive = JS_ToBool(ctx, rec);
        if (JS_IsBool(frc)) force = JS_ToBool(ctx, frc);
        JS_FreeValue(ctx, rec);
        JS_FreeValue(ctx, frc);
    }

    if (stat(path, &st) != 0) {
        int err = errno;
        if (force && err == ENOENT) {
            JS_FreeCString(ctx, path);
            return JS_UNDEFINED;
        }
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "stat");
    }

    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            JS_FreeCString(ctx, path);
            return js_fs_throw_error(ctx, EISDIR, path, "rm");
        }
        if (rm_recursive(path) != 0) {
            int err = errno;
            JS_FreeCString(ctx, path);
            return js_fs_throw_error(ctx, err, path, "rm");
        }
    } else {
        if (unlink(path) != 0) {
            int err = errno;
            JS_FreeCString(ctx, path);
            return js_fs_throw_error(ctx, err, path, "unlink");
        }
    }
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
}

/* ---- copyFileSync(src, dest) ---- */

#define FS_COPY_BUF_SIZE 8192

static JSValue js_fs_copyFileSync(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    const char *src, *dest;
    FILE *fsrc, *fdest;
    char buf[FS_COPY_BUF_SIZE];
    size_t n;

    if (argc < 2)
        return JS_ThrowTypeError(ctx, "need src and dest");
    if (!JS_IsString(argv[0]) || !JS_IsString(argv[1]))
        return JS_ThrowTypeError(ctx, "paths must be strings");

    src = JS_ToCString(ctx, argv[0]);
    if (!src) return JS_EXCEPTION;
    dest = JS_ToCString(ctx, argv[1]);
    if (!dest) {
        JS_FreeCString(ctx, src);
        return JS_EXCEPTION;
    }

    fsrc = fopen(src, "rb");
    if (!fsrc) {
        int err = errno;
        JS_FreeCString(ctx, src);
        JS_FreeCString(ctx, dest);
        return js_fs_throw_error(ctx, err, src, "open");
    }

    fdest = fopen(dest, "wb");
    if (!fdest) {
        int err = errno;
        fclose(fsrc);
        JS_FreeCString(ctx, src);
        JS_FreeCString(ctx, dest);
        return js_fs_throw_error(ctx, err, dest, "open");
    }

    while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
        if (fwrite(buf, 1, n, fdest) != n) {
            int err = errno;
            fclose(fsrc);
            fclose(fdest);
            JS_FreeCString(ctx, src);
            JS_FreeCString(ctx, dest);
            return js_fs_throw_error(ctx, err, dest, "write");
        }
    }
    fclose(fsrc);
    fclose(fdest);
    JS_FreeCString(ctx, src);
    JS_FreeCString(ctx, dest);
    return JS_UNDEFINED;
}

/* ---- realpathSync(path) ---- */

static JSValue js_fs_realpathSync(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    const char *path;
    char resolved[PATH_MAX];
    char *ret;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

#if defined(_WIN32)
    ret = _fullpath(resolved, path, sizeof(resolved));
#else
    ret = realpath(path, resolved);
#endif
    if (!ret) {
        int err = errno;
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "realpath");
    }
    JS_FreeCString(ctx, path);
    return JS_NewString(ctx, resolved);
}

/* ---- accessSync(path[, mode]) ---- */

static JSValue js_fs_accessSync(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    const char *path;
    int mode = 0; /* F_OK */

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt32(ctx, &mode, argv[1])) {
            JS_FreeCString(ctx, path);
            return JS_EXCEPTION;
        }
    }

    if (access_func(path, mode) != 0) {
        int err = errno;
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "access");
    }
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
}

/* ---- chmodSync(path, mode) ---- */

static JSValue js_fs_chmodSync(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
#if defined(_WIN32)
    return JS_ThrowTypeError(ctx, "chmodSync is not supported on Windows");
#else
    const char *path;
    int mode;

    if (argc < 2)
        return JS_ThrowTypeError(ctx, "need path and mode");
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");

    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &mode, argv[1])) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    if (chmod(path, mode) != 0) {
        int err = errno;
        JS_FreeCString(ctx, path);
        return js_fs_throw_error(ctx, err, path, "chmod");
    }
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
#endif
}

/* ---- fs.constants ---- */

#define FS_CONST(x) JS_PROP_INT32_DEF(#x, x, JS_PROP_CONFIGURABLE)

static const JSCFunctionListEntry js_fs_constants[] = {
    /* access constants */
    FS_CONST(F_OK),
    FS_CONST(R_OK),
    FS_CONST(W_OK),
    FS_CONST(X_OK),
    /* file open constants */
    FS_CONST(O_RDONLY),
    FS_CONST(O_WRONLY),
    FS_CONST(O_RDWR),
    FS_CONST(O_CREAT),
    FS_CONST(O_EXCL),
    FS_CONST(O_TRUNC),
    FS_CONST(O_APPEND),
#if !defined(_WIN32)
    FS_CONST(O_SYNC),
#endif
    /* S_IFMT and friends */
    FS_CONST(S_IFMT),
    FS_CONST(S_IFREG),
    FS_CONST(S_IFDIR),
    FS_CONST(S_IFCHR),
#if !defined(_WIN32)
    FS_CONST(S_IFBLK),
    FS_CONST(S_IFIFO),
    FS_CONST(S_IFLNK),
    FS_CONST(S_IFSOCK),
    /* permission bits */
    FS_CONST(S_IRWXU),
    FS_CONST(S_IRUSR),
    FS_CONST(S_IWUSR),
    FS_CONST(S_IXUSR),
    FS_CONST(S_IRWXG),
    FS_CONST(S_IRGRP),
    FS_CONST(S_IWGRP),
    FS_CONST(S_IXGRP),
    FS_CONST(S_IRWXO),
    FS_CONST(S_IROTH),
    FS_CONST(S_IWOTH),
    FS_CONST(S_IXOTH),
#endif
    /* copyFile constants */
    JS_PROP_INT32_DEF("COPYFILE_EXCL", 1, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("COPYFILE_FICLONE", 2, JS_PROP_CONFIGURABLE),
};
#undef FS_CONST

/* ---- FileHandle ---- */

static JSValue js_fs_fh_close(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv);
static JSValue js_fs_fh_read(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv);
static JSValue js_fs_fh_write(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv);
static JSValue js_fs_fh_sync(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv);

static JSValue js_fs_new_filehandle(JSContext *ctx, int fd, const char *path)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "fd", JS_NewInt32(ctx, fd));
    JS_SetPropertyStr(ctx, obj, "_fd", JS_NewInt32(ctx, fd));
    if (path)
        JS_SetPropertyStr(ctx, obj, "_path", JS_NewString(ctx, path));
    JS_SetPropertyStr(ctx, obj, "close",
                      JS_NewCFunction(ctx, js_fs_fh_close, "close", 0));
    JS_SetPropertyStr(ctx, obj, "read",
                      JS_NewCFunction(ctx, js_fs_fh_read, "read", 4));
    JS_SetPropertyStr(ctx, obj, "write",
                      JS_NewCFunction(ctx, js_fs_fh_write, "write", 4));
    JS_SetPropertyStr(ctx, obj, "sync",
                      JS_NewCFunction(ctx, js_fs_fh_sync, "sync", 0));
    return obj;
}

static int js_fs_fh_get_fd(JSContext *ctx, JSValueConst this_val)
{
    JSValue fd_val = JS_GetPropertyStr(ctx, this_val, "_fd");
    int32_t fd = -1;
    if (!JS_IsUndefined(fd_val) && !JS_IsException(fd_val))
        JS_ToInt32(ctx, &fd, fd_val);
    JS_FreeValue(ctx, fd_val);
    if (fd < 0)
        JS_ThrowTypeError(ctx, "filehandle is closed");
    return fd;
}

/* ---- FileHandle.close() -> Promise<void> ---- */

static JSValue js_fs_fh_close(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValue promise, resolving_funcs[2], ret;
    int fd = js_fs_fh_get_fd(ctx, this_val);
    if (fd < 0) return JS_EXCEPTION;

    promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) return JS_EXCEPTION;

    if (close(fd) != 0) {
        JSValue err = JS_NewError(ctx);
        char buf[256];
        snprintf(buf, sizeof(buf), "close: %s", strerror(errno));
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, buf));
        JS_SetPropertyStr(ctx, err, "errno", JS_NewInt32(ctx, errno));
        ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, err);
    } else {
        JS_SetPropertyStr(ctx, this_val, "_fd", JS_NewInt32(ctx, -1));
        JSValue undef = JS_UNDEFINED;
        ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &undef);
        JS_FreeValue(ctx, ret);
    }
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/* ---- FileHandle.read() -> Promise<{bytesRead, buffer}> ---- */

static JSValue js_fs_fh_read(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    JSValue promise, resolving_funcs[2], ret;
    JSValue buffer_val = JS_UNDEFINED;
    int fd;
    size_t buf_size = 0;
    uint8_t *buf = NULL;
    int64_t offset = 0, length = -1, position = -1;
    ssize_t nread;

    fd = js_fs_fh_get_fd(ctx, this_val);
    if (fd < 0) return JS_EXCEPTION;

    /* Parse args: read(buffer, offset, length, position)
                   read({ buffer, offset, length, position })
                   read()  -- default 16KB buffer */
    if (argc < 1 || JS_IsUndefined(argv[0])) {
        buffer_val = js_fs_new_buffer(ctx, 16384);
        if (JS_IsException(buffer_val)) return JS_EXCEPTION;
        buf = JS_GetArrayBuffer(ctx, &buf_size, buffer_val);
        length = buf_size;
    } else {
        size_t ab_size;
        uint8_t *ab = JS_GetArrayBuffer(ctx, &ab_size, argv[0]);
        if (ab) {
            /* Positional form */
            buffer_val = JS_DupValue(ctx, argv[0]);
            buf = ab;
            buf_size = ab_size;
            if (argc >= 2 && JS_IsNumber(argv[1]))
                JS_ToInt64(ctx, &offset, argv[1]);
            if (argc >= 3 && JS_IsNumber(argv[2]))
                JS_ToInt64(ctx, &length, argv[2]);
            else
                length = (int64_t)(buf_size - offset);
            if (argc >= 4 && JS_IsNumber(argv[3]))
                JS_ToInt64(ctx, &position, argv[3]);
        } else if (JS_IsObject(argv[0])) {
            /* Options form */
            JSValue bv = JS_GetPropertyStr(ctx, argv[0], "buffer");
            if (!JS_IsUndefined(bv)) {
                buffer_val = JS_DupValue(ctx, bv);
                buf = JS_GetArrayBuffer(ctx, &buf_size, bv);
            } else {
                buffer_val = js_fs_new_buffer(ctx, 16384);
                if (JS_IsException(buffer_val)) {
                    JS_FreeValue(ctx, bv);
                    return JS_EXCEPTION;
                }
                buf = JS_GetArrayBuffer(ctx, &buf_size, buffer_val);
            }
            JS_FreeValue(ctx, bv);
            if (!buf) {
                JS_FreeValue(ctx, buffer_val);
                return JS_ThrowTypeError(ctx, "invalid buffer");
            }
            JSValue v;
            v = JS_GetPropertyStr(ctx, argv[0], "offset");
            if (JS_IsNumber(v)) JS_ToInt64(ctx, &offset, v);
            JS_FreeValue(ctx, v);
            v = JS_GetPropertyStr(ctx, argv[0], "length");
            if (JS_IsNumber(v)) JS_ToInt64(ctx, &length, v);
            else length = (int64_t)(buf_size - offset);
            JS_FreeValue(ctx, v);
            v = JS_GetPropertyStr(ctx, argv[0], "position");
            if (JS_IsNumber(v)) JS_ToInt64(ctx, &position, v);
            JS_FreeValue(ctx, v);
        } else {
            return JS_ThrowTypeError(ctx, "argument must be ArrayBuffer or options");
        }
    }

    if (offset < 0 || length < 0 || (size_t)(offset + length) > buf_size) {
        JS_FreeValue(ctx, buffer_val);
        return JS_ThrowRangeError(ctx, "invalid offset/length");
    }

    promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, buffer_val);
        return JS_EXCEPTION;
    }

    if (position >= 0)
        lseek(fd, (off_t)position, SEEK_SET);

    nread = read(fd, buf + offset, (size_t)length);

    if (nread < 0) {
        JSValue err = JS_NewError(ctx);
        char ebuf[256];
        snprintf(ebuf, sizeof(ebuf), "read: %s", strerror(errno));
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, ebuf));
        JS_SetPropertyStr(ctx, err, "errno", JS_NewInt32(ctx, errno));
        ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, err);
        JS_FreeValue(ctx, buffer_val);
    } else {
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "bytesRead", JS_NewInt64(ctx, nread));
        JS_SetPropertyStr(ctx, result, "buffer", buffer_val);
        ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &result);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, result);
    }
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/* ---- FileHandle.write() -> Promise<{bytesWritten, buffer}> ---- */

static JSValue js_fs_fh_write(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValue promise, resolving_funcs[2], ret;
    int fd;
    int64_t offset = 0, length = -1, position = -1;

    fd = js_fs_fh_get_fd(ctx, this_val);
    if (fd < 0) return JS_EXCEPTION;

    if (argc < 1)
        return JS_ThrowTypeError(ctx, "need buffer or string argument");

    if (JS_IsString(argv[0])) {
        /* write(string[, position[, encoding]]) */
        size_t str_len;
        const char *str = JS_ToCStringLen(ctx, &str_len, argv[0]);
        if (!str) return JS_EXCEPTION;

        if (argc >= 2 && JS_IsNumber(argv[1]))
            JS_ToInt64(ctx, &position, argv[1]);

        promise = JS_NewPromiseCapability(ctx, resolving_funcs);
        if (JS_IsException(promise)) {
            JS_FreeCString(ctx, str);
            return JS_EXCEPTION;
        }

        if (position >= 0)
            lseek(fd, (off_t)position, SEEK_SET);

        ssize_t nwritten = write(fd, str, str_len);
        JS_FreeCString(ctx, str);

        if (nwritten < 0) {
            JSValue err = JS_NewError(ctx);
            char ebuf[256];
            snprintf(ebuf, sizeof(ebuf), "write: %s", strerror(errno));
            JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, ebuf));
            JS_SetPropertyStr(ctx, err, "errno", JS_NewInt32(ctx, errno));
            ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, err);
        } else {
            JSValue result = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, result, "bytesWritten", JS_NewInt64(ctx, nwritten));
            JS_SetPropertyStr(ctx, result, "buffer", JS_DupValue(ctx, argv[0]));
            ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &result);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, result);
        }
    } else {
        /* write(buffer, offset[, length[, position]])
           write(buffer, options) */
        size_t buf_size;
        uint8_t *buf = JS_GetArrayBuffer(ctx, &buf_size, argv[0]);
        if (!buf)
            return JS_ThrowTypeError(ctx, "argument must be string or ArrayBuffer");

        if (argc >= 2 && JS_IsObject(argv[1]) && JS_GetArrayBuffer(ctx, NULL, argv[1]) == NULL) {
            /* Options form */
            JSValue v;
            v = JS_GetPropertyStr(ctx, argv[1], "offset");
            if (JS_IsNumber(v)) JS_ToInt64(ctx, &offset, v);
            JS_FreeValue(ctx, v);
            v = JS_GetPropertyStr(ctx, argv[1], "length");
            if (JS_IsNumber(v)) JS_ToInt64(ctx, &length, v);
            JS_FreeValue(ctx, v);
            v = JS_GetPropertyStr(ctx, argv[1], "position");
            if (JS_IsNumber(v)) JS_ToInt64(ctx, &position, v);
            JS_FreeValue(ctx, v);
        } else {
            /* Positional form */
            if (argc >= 2 && JS_IsNumber(argv[1]))
                JS_ToInt64(ctx, &offset, argv[1]);
            if (argc >= 3 && JS_IsNumber(argv[2]))
                JS_ToInt64(ctx, &length, argv[2]);
            if (argc >= 4 && JS_IsNumber(argv[3]))
                JS_ToInt64(ctx, &position, argv[3]);
        }

        if (length < 0) length = (int64_t)(buf_size - offset);
        if (offset < 0 || length < 0 || (size_t)(offset + length) > buf_size)
            return JS_ThrowRangeError(ctx, "invalid offset/length");

        promise = JS_NewPromiseCapability(ctx, resolving_funcs);
        if (JS_IsException(promise)) return JS_EXCEPTION;

        if (position >= 0)
            lseek(fd, (off_t)position, SEEK_SET);

        ssize_t nwritten = write(fd, buf + offset, (size_t)length);

        if (nwritten < 0) {
            JSValue err = JS_NewError(ctx);
            char ebuf[256];
            snprintf(ebuf, sizeof(ebuf), "write: %s", strerror(errno));
            JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, ebuf));
            JS_SetPropertyStr(ctx, err, "errno", JS_NewInt32(ctx, errno));
            ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, err);
        } else {
            JSValue result = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, result, "bytesWritten", JS_NewInt64(ctx, nwritten));
            JS_SetPropertyStr(ctx, result, "buffer", JS_DupValue(ctx, argv[0]));
            ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &result);
            JS_FreeValue(ctx, ret);
            JS_FreeValue(ctx, result);
        }
    }

    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/* ---- FileHandle.sync() -> Promise<void> ---- */

static JSValue js_fs_fh_sync(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    JSValue promise, resolving_funcs[2], ret;
    int fd = js_fs_fh_get_fd(ctx, this_val);
    if (fd < 0) return JS_EXCEPTION;

    promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) return JS_EXCEPTION;

    if (fsync_fd(fd) != 0) {
        JSValue err = JS_NewError(ctx);
        char buf[256];
        snprintf(buf, sizeof(buf), "sync: %s", strerror(errno));
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, buf));
        JS_SetPropertyStr(ctx, err, "errno", JS_NewInt32(ctx, errno));
        ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, err);
    } else {
        JSValue undef = JS_UNDEFINED;
        ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &undef);
        JS_FreeValue(ctx, ret);
    }
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/* ---- Promise wrappers ---- */
/* Each wraps the sync version in a Promise (resolves on next microtask) */

#define DEFINE_FS_PROMISE(name, sync_fn, nargs) \
static JSValue js_fs_promise_##name(JSContext *ctx, JSValueConst this_val, \
                                    int argc, JSValueConst *argv) \
{ \
    JSValue promise, resolving_funcs[2], result, ret; \
    promise = JS_NewPromiseCapability(ctx, resolving_funcs); \
    if (JS_IsException(promise)) \
        return JS_EXCEPTION; \
    result = sync_fn(ctx, JS_UNDEFINED, argc, argv); \
    if (JS_IsException(result)) { \
        JSValue err = JS_GetException(ctx); \
        ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err); \
        JS_FreeValue(ctx, ret); \
        JS_FreeValue(ctx, err); \
    } else { \
        ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &result); \
        JS_FreeValue(ctx, ret); \
        JS_FreeValue(ctx, result); \
    } \
    JS_FreeValue(ctx, resolving_funcs[0]); \
    JS_FreeValue(ctx, resolving_funcs[1]); \
    return promise; \
}

DEFINE_FS_PROMISE(readFile,     js_fs_readFileSync,     2)
DEFINE_FS_PROMISE(writeFile,    js_fs_writeFileSync,     3)
DEFINE_FS_PROMISE(appendFile,   js_fs_appendFileSync,    3)
DEFINE_FS_PROMISE(mkdir,        js_fs_mkdirSync,         2)
DEFINE_FS_PROMISE(readdir,      js_fs_readdirSync,       1)
DEFINE_FS_PROMISE(stat,         js_fs_statSync,          1)
DEFINE_FS_PROMISE(lstat,        js_fs_lstatSync,         1)
DEFINE_FS_PROMISE(unlink,       js_fs_unlinkSync,        1)
DEFINE_FS_PROMISE(rename,       js_fs_renameSync,        2)
DEFINE_FS_PROMISE(rmdir,        js_fs_rmdirSync,         1)
DEFINE_FS_PROMISE(rm,           js_fs_rmSync,            2)
DEFINE_FS_PROMISE(copyFile,     js_fs_copyFileSync,      2)
DEFINE_FS_PROMISE(realpath,     js_fs_realpathSync,      1)
DEFINE_FS_PROMISE(access,       js_fs_accessSync,        2)
DEFINE_FS_PROMISE(chmod,        js_fs_chmodSync,         2)

#undef DEFINE_FS_PROMISE

/* ---- open() -> Promise<FileHandle> ---- */

static JSValue js_fs_promise_open(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    const char *path;
    int flags, mode = 0666;
    JSValue promise, resolving_funcs[2], ret;

    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "path must be a string");
    path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    /* Parse flags */
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_IsString(argv[1])) {
            const char *flag_str = JS_ToCString(ctx, argv[1]);
            flags = js_fs_parse_open_flags(flag_str);
            JS_FreeCString(ctx, flag_str);
        } else if (JS_IsNumber(argv[1])) {
            JS_ToInt32(ctx, &flags, argv[1]);
        } else {
            flags = O_RDONLY;
        }
    } else {
        flags = O_RDONLY;
    }

    if (argc >= 3 && !JS_IsUndefined(argv[2]))
        JS_ToInt32(ctx, &mode, argv[2]);

#if defined(_WIN32)
    if (!(flags & O_TEXT))
        flags |= O_BINARY;
#endif

    promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    int fd = open(path, flags, mode);
    if (fd < 0) {
        int err = errno;
        JSValue err_obj = JS_NewError(ctx);
        char buf[256];
        snprintf(buf, sizeof(buf), "open: %s, '%s'", strerror(err), path);
        JS_SetPropertyStr(ctx, err_obj, "message", JS_NewString(ctx, buf));
        JS_SetPropertyStr(ctx, err_obj, "errno", JS_NewInt32(ctx, err));
        JS_SetPropertyStr(ctx, err_obj, "code",
                          JS_NewString(ctx, err == ENOENT ? "ENOENT" : "UNKNOWN"));
        JS_SetPropertyStr(ctx, err_obj, "path", JS_NewString(ctx, path));
        ret = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err_obj);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, err_obj);
    } else {
        JSValue fh = js_fs_new_filehandle(ctx, fd, path);
        ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &fh);
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, fh);
    }
    JS_FreeCString(ctx, path);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/* Helper: build the fs.promises sub-object */
static JSValue js_fs_build_promises(JSContext *ctx)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "readFile",
                      JS_NewCFunction(ctx, js_fs_promise_readFile, "readFile", 2));
    JS_SetPropertyStr(ctx, obj, "writeFile",
                      JS_NewCFunction(ctx, js_fs_promise_writeFile, "writeFile", 3));
    JS_SetPropertyStr(ctx, obj, "appendFile",
                      JS_NewCFunction(ctx, js_fs_promise_appendFile, "appendFile", 3));
    JS_SetPropertyStr(ctx, obj, "mkdir",
                      JS_NewCFunction(ctx, js_fs_promise_mkdir, "mkdir", 2));
    JS_SetPropertyStr(ctx, obj, "readdir",
                      JS_NewCFunction(ctx, js_fs_promise_readdir, "readdir", 1));
    JS_SetPropertyStr(ctx, obj, "stat",
                      JS_NewCFunction(ctx, js_fs_promise_stat, "stat", 1));
    JS_SetPropertyStr(ctx, obj, "lstat",
                      JS_NewCFunction(ctx, js_fs_promise_lstat, "lstat", 1));
    JS_SetPropertyStr(ctx, obj, "unlink",
                      JS_NewCFunction(ctx, js_fs_promise_unlink, "unlink", 1));
    JS_SetPropertyStr(ctx, obj, "rename",
                      JS_NewCFunction(ctx, js_fs_promise_rename, "rename", 2));
    JS_SetPropertyStr(ctx, obj, "rmdir",
                      JS_NewCFunction(ctx, js_fs_promise_rmdir, "rmdir", 1));
    JS_SetPropertyStr(ctx, obj, "rm",
                      JS_NewCFunction(ctx, js_fs_promise_rm, "rm", 2));
    JS_SetPropertyStr(ctx, obj, "copyFile",
                      JS_NewCFunction(ctx, js_fs_promise_copyFile, "copyFile", 2));
    JS_SetPropertyStr(ctx, obj, "realpath",
                      JS_NewCFunction(ctx, js_fs_promise_realpath, "realpath", 1));
    JS_SetPropertyStr(ctx, obj, "access",
                      JS_NewCFunction(ctx, js_fs_promise_access, "access", 2));
    JS_SetPropertyStr(ctx, obj, "chmod",
                      JS_NewCFunction(ctx, js_fs_promise_chmod, "chmod", 2));
    JS_SetPropertyStr(ctx, obj, "open",
                      JS_NewCFunction(ctx, js_fs_promise_open, "open", 3));
    /* constants on promises too */
    {
        JSValue c = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, c, js_fs_constants, countof(js_fs_constants));
        JS_SetPropertyStr(ctx, obj, "constants", c);
    }
    return obj;
}

/* ---- Module init ---- */

static int js_fs_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue fs_obj, constants;

    fs_obj = JS_NewObject(ctx);

    /* Functions */
    JS_SetPropertyStr(ctx, fs_obj, "readFileSync",
                      JS_NewCFunction(ctx, js_fs_readFileSync, "readFileSync", 2));
    JS_SetPropertyStr(ctx, fs_obj, "writeFileSync",
                      JS_NewCFunction(ctx, js_fs_writeFileSync, "writeFileSync", 3));
    JS_SetPropertyStr(ctx, fs_obj, "appendFileSync",
                      JS_NewCFunction(ctx, js_fs_appendFileSync, "appendFileSync", 3));
    JS_SetPropertyStr(ctx, fs_obj, "existsSync",
                      JS_NewCFunction(ctx, js_fs_existsSync, "existsSync", 1));
    JS_SetPropertyStr(ctx, fs_obj, "mkdirSync",
                      JS_NewCFunction(ctx, js_fs_mkdirSync, "mkdirSync", 2));
    JS_SetPropertyStr(ctx, fs_obj, "readdirSync",
                      JS_NewCFunction(ctx, js_fs_readdirSync, "readdirSync", 1));
    JS_SetPropertyStr(ctx, fs_obj, "statSync",
                      JS_NewCFunction(ctx, js_fs_statSync, "statSync", 1));
    JS_SetPropertyStr(ctx, fs_obj, "lstatSync",
                      JS_NewCFunction(ctx, js_fs_lstatSync, "lstatSync", 1));
    JS_SetPropertyStr(ctx, fs_obj, "unlinkSync",
                      JS_NewCFunction(ctx, js_fs_unlinkSync, "unlinkSync", 1));
    JS_SetPropertyStr(ctx, fs_obj, "renameSync",
                      JS_NewCFunction(ctx, js_fs_renameSync, "renameSync", 2));
    JS_SetPropertyStr(ctx, fs_obj, "rmdirSync",
                      JS_NewCFunction(ctx, js_fs_rmdirSync, "rmdirSync", 1));
    JS_SetPropertyStr(ctx, fs_obj, "rmSync",
                      JS_NewCFunction(ctx, js_fs_rmSync, "rmSync", 2));
    JS_SetPropertyStr(ctx, fs_obj, "copyFileSync",
                      JS_NewCFunction(ctx, js_fs_copyFileSync, "copyFileSync", 2));
    JS_SetPropertyStr(ctx, fs_obj, "realpathSync",
                      JS_NewCFunction(ctx, js_fs_realpathSync, "realpathSync", 1));
    JS_SetPropertyStr(ctx, fs_obj, "accessSync",
                      JS_NewCFunction(ctx, js_fs_accessSync, "accessSync", 2));
    JS_SetPropertyStr(ctx, fs_obj, "chmodSync",
                      JS_NewCFunction(ctx, js_fs_chmodSync, "chmodSync", 2));

    /* fs.constants */
    constants = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, constants, js_fs_constants,
                               countof(js_fs_constants));
    JS_SetPropertyStr(ctx, fs_obj, "constants", constants);

    /* fs.promises */
    JS_SetPropertyStr(ctx, fs_obj, "promises", js_fs_build_promises(ctx));

    /* default export */
    if (JS_SetModuleExport(ctx, m, "default", fs_obj) < 0)
        return -1;

    /* named exports */
    if (JS_SetModuleExport(ctx, m, "readFileSync",
                           JS_NewCFunction(ctx, js_fs_readFileSync, "readFileSync", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "writeFileSync",
                           JS_NewCFunction(ctx, js_fs_writeFileSync, "writeFileSync", 3)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "appendFileSync",
                           JS_NewCFunction(ctx, js_fs_appendFileSync, "appendFileSync", 3)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "existsSync",
                           JS_NewCFunction(ctx, js_fs_existsSync, "existsSync", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "mkdirSync",
                           JS_NewCFunction(ctx, js_fs_mkdirSync, "mkdirSync", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "readdirSync",
                           JS_NewCFunction(ctx, js_fs_readdirSync, "readdirSync", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "statSync",
                           JS_NewCFunction(ctx, js_fs_statSync, "statSync", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "lstatSync",
                           JS_NewCFunction(ctx, js_fs_lstatSync, "lstatSync", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "unlinkSync",
                           JS_NewCFunction(ctx, js_fs_unlinkSync, "unlinkSync", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "renameSync",
                           JS_NewCFunction(ctx, js_fs_renameSync, "renameSync", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "rmdirSync",
                           JS_NewCFunction(ctx, js_fs_rmdirSync, "rmdirSync", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "rmSync",
                           JS_NewCFunction(ctx, js_fs_rmSync, "rmSync", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "copyFileSync",
                           JS_NewCFunction(ctx, js_fs_copyFileSync, "copyFileSync", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "realpathSync",
                           JS_NewCFunction(ctx, js_fs_realpathSync, "realpathSync", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "accessSync",
                           JS_NewCFunction(ctx, js_fs_accessSync, "accessSync", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "chmodSync",
                           JS_NewCFunction(ctx, js_fs_chmodSync, "chmodSync", 2)) < 0)
        return -1;

    /* constants named export (separate object) */
    {
        JSValue c2 = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, c2, js_fs_constants,
                                   countof(js_fs_constants));
        if (JS_SetModuleExport(ctx, m, "constants", c2) < 0)
            return -1;
    }

    return 0;
}

JSModuleDef *js_init_module_fs(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_fs_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "readFileSync");
    JS_AddModuleExport(ctx, m, "writeFileSync");
    JS_AddModuleExport(ctx, m, "appendFileSync");
    JS_AddModuleExport(ctx, m, "existsSync");
    JS_AddModuleExport(ctx, m, "mkdirSync");
    JS_AddModuleExport(ctx, m, "readdirSync");
    JS_AddModuleExport(ctx, m, "statSync");
    JS_AddModuleExport(ctx, m, "lstatSync");
    JS_AddModuleExport(ctx, m, "unlinkSync");
    JS_AddModuleExport(ctx, m, "renameSync");
    JS_AddModuleExport(ctx, m, "rmdirSync");
    JS_AddModuleExport(ctx, m, "rmSync");
    JS_AddModuleExport(ctx, m, "copyFileSync");
    JS_AddModuleExport(ctx, m, "realpathSync");
    JS_AddModuleExport(ctx, m, "accessSync");
    JS_AddModuleExport(ctx, m, "chmodSync");
    JS_AddModuleExport(ctx, m, "constants");
    return m;
}

/* ---- node:fs/promises module ---- */

static int js_fs_promises_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue promises_obj = js_fs_build_promises(ctx);

    /* default export */
    if (JS_SetModuleExport(ctx, m, "default", promises_obj) < 0)
        return -1;

    /* named exports */
    if (JS_SetModuleExport(ctx, m, "readFile",
                           JS_NewCFunction(ctx, js_fs_promise_readFile, "readFile", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "writeFile",
                           JS_NewCFunction(ctx, js_fs_promise_writeFile, "writeFile", 3)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "appendFile",
                           JS_NewCFunction(ctx, js_fs_promise_appendFile, "appendFile", 3)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "mkdir",
                           JS_NewCFunction(ctx, js_fs_promise_mkdir, "mkdir", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "readdir",
                           JS_NewCFunction(ctx, js_fs_promise_readdir, "readdir", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "stat",
                           JS_NewCFunction(ctx, js_fs_promise_stat, "stat", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "lstat",
                           JS_NewCFunction(ctx, js_fs_promise_lstat, "lstat", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "unlink",
                           JS_NewCFunction(ctx, js_fs_promise_unlink, "unlink", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "rename",
                           JS_NewCFunction(ctx, js_fs_promise_rename, "rename", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "rmdir",
                           JS_NewCFunction(ctx, js_fs_promise_rmdir, "rmdir", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "rm",
                           JS_NewCFunction(ctx, js_fs_promise_rm, "rm", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "copyFile",
                           JS_NewCFunction(ctx, js_fs_promise_copyFile, "copyFile", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "realpath",
                           JS_NewCFunction(ctx, js_fs_promise_realpath, "realpath", 1)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "access",
                           JS_NewCFunction(ctx, js_fs_promise_access, "access", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "chmod",
                           JS_NewCFunction(ctx, js_fs_promise_chmod, "chmod", 2)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "open",
                           JS_NewCFunction(ctx, js_fs_promise_open, "open", 3)) < 0)
        return -1;

    /* constants named export */
    return 0;
}

JSModuleDef *js_init_module_fs_promises(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_fs_promises_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "readFile");
    JS_AddModuleExport(ctx, m, "writeFile");
    JS_AddModuleExport(ctx, m, "appendFile");
    JS_AddModuleExport(ctx, m, "mkdir");
    JS_AddModuleExport(ctx, m, "readdir");
    JS_AddModuleExport(ctx, m, "stat");
    JS_AddModuleExport(ctx, m, "lstat");
    JS_AddModuleExport(ctx, m, "unlink");
    JS_AddModuleExport(ctx, m, "rename");
    JS_AddModuleExport(ctx, m, "rmdir");
    JS_AddModuleExport(ctx, m, "rm");
    JS_AddModuleExport(ctx, m, "copyFile");
    JS_AddModuleExport(ctx, m, "realpath");
    JS_AddModuleExport(ctx, m, "access");
    JS_AddModuleExport(ctx, m, "chmod");
    JS_AddModuleExport(ctx, m, "open");
    JS_AddModuleExport(ctx, m, "constants");
    return m;
}
