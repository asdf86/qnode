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


static JSClassID js_process_class_id;
/* Global argv array reference */

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




static const JSCFunctionListEntry js_process_proto_funcs[] = {
    JS_PROP_STRING_DEF("arch", ARCH, 0 ),
    /* setvbuf, ...  */
};

static int js_process_init(JSContext *ctx, JSModuleDef *m)
{



    return JS_SetModuleExportList(ctx, m, js_process_proto_funcs,
                                  countof(js_process_proto_funcs));
}

JSModuleDef *js_init_module_process(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_process_init);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, js_process_proto_funcs, countof(js_process_proto_funcs));

    return m;
}