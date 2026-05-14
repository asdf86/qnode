/*
 * Node.js Timers Module for QNode
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

#include "cutils.h"
#include "quickjs-libc.h"

static int js_timers_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue global = JS_GetGlobalObject(ctx);

    /* Re-export the global timer functions that were injected in qnode.c */
    JS_SetModuleExport(ctx, m, "default",
                       JS_GetPropertyStr(ctx, global, "setTimeout"));
    JS_SetModuleExport(ctx, m, "setTimeout",
                       JS_GetPropertyStr(ctx, global, "setTimeout"));
    JS_SetModuleExport(ctx, m, "clearTimeout",
                       JS_GetPropertyStr(ctx, global, "clearTimeout"));
    JS_SetModuleExport(ctx, m, "setInterval",
                       JS_GetPropertyStr(ctx, global, "setInterval"));
    JS_SetModuleExport(ctx, m, "clearInterval",
                       JS_GetPropertyStr(ctx, global, "clearInterval"));
    JS_SetModuleExport(ctx, m, "setImmediate",
                       JS_GetPropertyStr(ctx, global, "setImmediate"));
    JS_SetModuleExport(ctx, m, "clearImmediate",
                       JS_GetPropertyStr(ctx, global, "clearImmediate"));

    JS_FreeValue(ctx, global);
    return 0;
}

JSModuleDef *js_init_module_timers(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m = JS_NewCModule(ctx, module_name, js_timers_init);
    if (!m) return NULL;
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "setTimeout");
    JS_AddModuleExport(ctx, m, "clearTimeout");
    JS_AddModuleExport(ctx, m, "setInterval");
    JS_AddModuleExport(ctx, m, "clearInterval");
    JS_AddModuleExport(ctx, m, "setImmediate");
    JS_AddModuleExport(ctx, m, "clearImmediate");
    return m;
}
