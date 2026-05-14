/*
 * Node.js Buffer Module for QNode
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
#include <ctype.h>
#include <stdint.h>

#include "cutils.h"
#include "quickjs-libc.h"

/* ---- Encoding identifiers ---- */

enum {
    ENC_UTF8,
    ENC_ASCII,
    ENC_LATIN1,
    ENC_HEX,
    ENC_BASE64,
    ENC_BASE64URL,
    ENC_UNKNOWN
};

static int get_encoding_id(const char *enc)
{
    if (!enc) return ENC_UTF8;
    if (strcmp(enc, "utf8") == 0 || strcmp(enc, "utf-8") == 0 ||
        strcmp(enc, "UTF8") == 0 || strcmp(enc, "UTF-8") == 0)
        return ENC_UTF8;
    if (strcmp(enc, "ascii") == 0 || strcmp(enc, "ASCII") == 0)
        return ENC_ASCII;
    if (strcmp(enc, "latin1") == 0 || strcmp(enc, "binary") == 0 ||
        strcmp(enc, "Latin1") == 0 || strcmp(enc, "BINARY") == 0)
        return ENC_LATIN1;
    if (strcmp(enc, "hex") == 0 || strcmp(enc, "HEX") == 0)
        return ENC_HEX;
    if (strcmp(enc, "base64") == 0 || strcmp(enc, "BASE64") == 0)
        return ENC_BASE64;
    if (strcmp(enc, "base64url") == 0 || strcmp(enc, "BASE64URL") == 0)
        return ENC_BASE64URL;
    return ENC_UNKNOWN;
}

/* ---- Base64 ---- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char b64url_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64_decode_char(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static JSValue js_buffer_base64_encode(JSContext *ctx, const uint8_t *src,
                                       size_t len, int url_safe)
{
    const char *table = url_safe ? b64url_table : b64_table;
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = js_malloc(ctx, out_len + 1);
    if (!out) return JS_EXCEPTION;
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t a = src[i];
        uint32_t b = (i + 1 < len) ? src[i + 1] : 0;
        uint32_t c = (i + 2 < len) ? src[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = table[(triple >> 18) & 0x3F];
        out[j++] = table[(triple >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    JSValue ret = JS_NewString(ctx, out);
    js_free(ctx, out);
    return ret;
}

static uint8_t *js_buffer_base64_decode(JSContext *ctx, const char *src,
                                        size_t len, size_t *out_len)
{
    /* strip trailing '=' */
    while (len > 0 && src[len - 1] == '=') len--;
    size_t alloc_size = (len * 3 + 3) / 4;
    uint8_t *out = js_malloc(ctx, alloc_size);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        int a = b64_decode_char(src[i]);
        int b = (i + 1 < len) ? b64_decode_char(src[i + 1]) : 0;
        int c = (i + 2 < len) ? b64_decode_char(src[i + 2]) : -1;
        int d = (i + 3 < len) ? b64_decode_char(src[i + 3]) : -1;
        if (a < 0 || b < 0) { js_free(ctx, out); return NULL; }
        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                          ((c >= 0 ? (uint32_t)c : 0) << 6) |
                          (d >= 0 ? (uint32_t)d : 0);
        out[j++] = (triple >> 16) & 0xFF;
        if (c >= 0) out[j++] = (triple >> 8) & 0xFF;
        if (d >= 0) out[j++] = triple & 0xFF;
    }
    *out_len = j;
    return out;
}

/* ---- Hex ---- */

static JSValue js_buffer_hex_encode(JSContext *ctx, const uint8_t *src, size_t len)
{
    char *out = js_malloc(ctx, len * 2 + 1);
    if (!out) return JS_EXCEPTION;
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex_chars[(src[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex_chars[src[i] & 0xF];
    }
    out[len * 2] = '\0';
    JSValue ret = JS_NewString(ctx, out);
    js_free(ctx, out);
    return ret;
}

static uint8_t *js_buffer_hex_decode(JSContext *ctx, const char *src,
                                     size_t len, size_t *out_len)
{
    if (len % 2 != 0) return NULL;
    size_t n = len / 2;
    uint8_t *out = js_malloc(ctx, n);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned int hi, lo;
        char ch = src[i * 2];
        if (ch >= '0' && ch <= '9')      hi = ch - '0';
        else if (ch >= 'a' && ch <= 'f') hi = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') hi = ch - 'A' + 10;
        else { js_free(ctx, out); return NULL; }
        ch = src[i * 2 + 1];
        if (ch >= '0' && ch <= '9')      lo = ch - '0';
        else if (ch >= 'a' && ch <= 'f') lo = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') lo = ch - 'A' + 10;
        else { js_free(ctx, out); return NULL; }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_len = n;
    return out;
}

/* ---- String ↔ bytes with encoding ---- */

static uint8_t *js_buffer_string_to_bytes(JSContext *ctx, const char *str,
                                          size_t str_len, int encoding,
                                          size_t *out_len)
{
    switch (encoding) {
    case ENC_UTF8:
    case ENC_ASCII:
    case ENC_LATIN1: {
        uint8_t *buf = js_malloc(ctx, str_len);
        if (!buf) return NULL;
        memcpy(buf, str, str_len);
        *out_len = str_len;
        return buf;
    }
    case ENC_HEX:
        return js_buffer_hex_decode(ctx, str, str_len, out_len);
    case ENC_BASE64:
    case ENC_BASE64URL:
        return js_buffer_base64_decode(ctx, str, str_len, out_len);
    default:
        return NULL;
    }
}

static JSValue js_buffer_bytes_to_string(JSContext *ctx, const uint8_t *buf,
                                         size_t len, int encoding)
{
    switch (encoding) {
    case ENC_UTF8:
    case ENC_ASCII:
    case ENC_LATIN1:
        return JS_NewStringLen(ctx, (const char *)buf, len);
    case ENC_HEX:
        return js_buffer_hex_encode(ctx, buf, len);
    case ENC_BASE64:
        return js_buffer_base64_encode(ctx, buf, len, 0);
    case ENC_BASE64URL:
        return js_buffer_base64_encode(ctx, buf, len, 1);
    default:
        return JS_ThrowTypeError(ctx, "unsupported encoding");
    }
}

/* ---- Buffer prototype: stored on global object to avoid static leak ---- */

#define BUFFER_PROTO_KEY "\x01qnode_buffer_proto"

static JSValue js_get_buffer_proto(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue proto = JS_GetPropertyStr(ctx, global, BUFFER_PROTO_KEY);
    JS_FreeValue(ctx, global);
    return proto;
}

/* ---- Helper: wrap a Uint8Array as a Buffer ---- */

static JSValue js_buffer_wrap(JSContext *ctx, JSValue u8array)
{
    JSValue proto = js_get_buffer_proto(ctx);
    if (JS_IsObject(proto)) {
        JS_SetPrototype(ctx, u8array, proto);
        JS_FreeValue(ctx, proto);
    }
    return u8array;
}

/* ---- Helper: get the Uint8Array data pointer and length ---- */

static uint8_t *js_buffer_get_bytes(JSContext *ctx, JSValueConst buf, size_t *len)
{
    size_t offset, el_size;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, buf, &offset, len, &el_size);
    if (JS_IsException(ab)) return NULL;
    size_t ab_len;
    uint8_t *data = JS_GetArrayBuffer(ctx, &ab_len, ab);
    JS_FreeValue(ctx, ab);
    if (!data) return NULL;
    return data + offset;
}

/* ---- Helper: create a Buffer of given size ---- */

static JSValue js_buffer_create(JSContext *ctx, size_t size)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue u8_ctor = JS_GetPropertyStr(ctx, global, "Uint8Array");
    JS_FreeValue(ctx, global);
    if (JS_IsException(u8_ctor)) return u8_ctor;

    JSValue size_val = JS_NewInt64(ctx, (int64_t)size);
    JSValue u8 = JS_CallConstructor(ctx, u8_ctor, 1, &size_val);
    JS_FreeValue(ctx, u8_ctor);
    JS_FreeValue(ctx, size_val);
    if (JS_IsException(u8)) return u8;

    return js_buffer_wrap(ctx, u8);
}

/* ---- Buffer.from() ---- */

static JSValue js_buffer_from(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Buffer.from() requires an argument");

    /* Buffer.from(string[, encoding]) */
    if (JS_IsString(argv[0])) {
        const char *enc_str = NULL;
        int encoding = ENC_UTF8;
        if (argc >= 2 && JS_IsString(argv[1])) {
            enc_str = JS_ToCString(ctx, argv[1]);
            encoding = get_encoding_id(enc_str);
        }
        size_t str_len;
        const char *str = JS_ToCStringLen(ctx, &str_len, argv[0]);
        if (!str) { if (enc_str) JS_FreeCString(ctx, enc_str); return JS_EXCEPTION; }

        size_t byte_len;
        uint8_t *bytes = js_buffer_string_to_bytes(ctx, str, str_len, encoding, &byte_len);
        JS_FreeCString(ctx, str);
        if (enc_str) JS_FreeCString(ctx, enc_str);
        if (!bytes)
            return JS_ThrowTypeError(ctx, "failed to decode string");

        JSValue buf = js_buffer_create(ctx, byte_len);
        if (JS_IsException(buf)) { js_free(ctx, bytes); return buf; }
        uint8_t *dst = js_buffer_get_bytes(ctx, buf, &(size_t){0});
        if (dst) memcpy(dst, bytes, byte_len);
        js_free(ctx, bytes);
        return buf;
    }

    /* Buffer.from(arrayBuffer[, byteOffset[, length]]) */
    {
        size_t ab_len;
        uint8_t *ab_data = JS_GetArrayBuffer(ctx, &ab_len, argv[0]);
        if (ab_data) {
            int64_t offset = 0, length = (int64_t)ab_len;
            if (argc >= 2 && !JS_IsUndefined(argv[1]))
                JS_ToInt64(ctx, &offset, argv[1]);
            if (argc >= 3 && !JS_IsUndefined(argv[2]))
                JS_ToInt64(ctx, &length, argv[2]);
            if (offset < 0 || length < 0 || (size_t)(offset + length) > ab_len)
                return JS_ThrowRangeError(ctx, "invalid offset/length");

            /* Create Uint8Array view of the ArrayBuffer */
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue u8_ctor = JS_GetPropertyStr(ctx, global, "Uint8Array");
            JS_FreeValue(ctx, global);
            JSValue args[3] = { JS_DupValue(ctx, argv[0]),
                                JS_NewInt64(ctx, offset),
                                JS_NewInt64(ctx, length) };
            JSValue u8 = JS_CallConstructor(ctx, u8_ctor, 3, args);
            JS_FreeValue(ctx, u8_ctor);
            JS_FreeValue(ctx, args[0]);
            JS_FreeValue(ctx, args[1]);
            JS_FreeValue(ctx, args[2]);
            if (JS_IsException(u8)) return u8;
            return js_buffer_wrap(ctx, u8);
        }
    }

    /* Buffer.from(buffer) / Buffer.from(Uint8Array) */
    {
        size_t len;
        uint8_t *src = js_buffer_get_bytes(ctx, argv[0], &len);
        if (src) {
            JSValue buf = js_buffer_create(ctx, len);
            if (JS_IsException(buf)) return buf;
            uint8_t *dst = js_buffer_get_bytes(ctx, buf, &(size_t){0});
            if (dst) memcpy(dst, src, len);
            return buf;
        }
    }

    /* Buffer.from(array) */
    if (JS_IsObject(argv[0])) {
        JSValue length_val = JS_GetPropertyStr(ctx, argv[0], "length");
        if (JS_IsNumber(length_val)) {
            int64_t arr_len;
            JS_ToInt64(ctx, &arr_len, length_val);
            JS_FreeValue(ctx, length_val);
            JSValue buf = js_buffer_create(ctx, (size_t)arr_len);
            if (JS_IsException(buf)) return buf;
            uint8_t *dst = js_buffer_get_bytes(ctx, buf, &(size_t){0});
            if (dst) {
                for (int64_t i = 0; i < arr_len; i++) {
                    JSValue v = JS_GetPropertyUint32(ctx, argv[0], (uint32_t)i);
                    int32_t byte;
                    if (!JS_IsUndefined(v) && JS_ToInt32(ctx, &byte, v) == 0)
                        dst[i] = (uint8_t)byte;
                    JS_FreeValue(ctx, v);
                }
            }
            return buf;
        }
        JS_FreeValue(ctx, length_val);
    }

    return JS_ThrowTypeError(ctx, "Buffer.from(): invalid argument type");
}

/* ---- Buffer.alloc(size[, fill]) ---- */

static JSValue js_buffer_alloc(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    int64_t size;
    if (argc < 1 || JS_ToInt64(ctx, &size, argv[0]))
        return JS_ThrowTypeError(ctx, "size must be a number");
    if (size < 0) return JS_ThrowRangeError(ctx, "invalid size");

    JSValue buf = js_buffer_create(ctx, (size_t)size);
    if (JS_IsException(buf)) return buf;

    /* Fill */
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        uint8_t fill_byte = 0;
        if (JS_IsString(argv[1])) {
            size_t str_len;
            const char *s = JS_ToCStringLen(ctx, &str_len, argv[1]);
            if (s && str_len > 0) fill_byte = (uint8_t)s[0];
            if (s) JS_FreeCString(ctx, s);
        } else {
            int32_t v;
            if (JS_ToInt32(ctx, &v, argv[1]) == 0)
                fill_byte = (uint8_t)(v & 0xFF);
        }
        size_t len;
        uint8_t *dst = js_buffer_get_bytes(ctx, buf, &len);
        if (dst) memset(dst, fill_byte, len);
    } else {
        size_t len;
        uint8_t *dst = js_buffer_get_bytes(ctx, buf, &len);
        if (dst) memset(dst, 0, len);
    }
    return buf;
}

/* ---- Buffer.allocUnsafe(size) ---- */

static JSValue js_buffer_allocUnsafe(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    int64_t size;
    if (argc < 1 || JS_ToInt64(ctx, &size, argv[0]))
        return JS_ThrowTypeError(ctx, "size must be a number");
    if (size < 0) return JS_ThrowRangeError(ctx, "invalid size");
    return js_buffer_create(ctx, (size_t)size);
}

/* ---- Buffer.isBuffer(obj) ---- */

static JSValue js_buffer_isBuffer(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    if (argc < 1 || !JS_IsObject(argv[0])) return JS_NewBool(ctx, FALSE);
    JSValue expected = js_get_buffer_proto(ctx);
    if (!JS_IsObject(expected)) return JS_NewBool(ctx, FALSE);
    JSValue proto = JS_GetPrototype(ctx, argv[0]);
    BOOL result = FALSE;
    for (int depth = 0; depth < 10 && JS_IsObject(proto); depth++) {
        if (JS_VALUE_GET_PTR(proto) == JS_VALUE_GET_PTR(expected)) {
            result = TRUE;
            break;
        }
        JSValue next = JS_GetPrototype(ctx, proto);
        JS_FreeValue(ctx, proto);
        proto = next;
    }
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, expected);
    return JS_NewBool(ctx, result);
}

/* ---- Buffer.concat(list[, totalLength]) ---- */

static JSValue js_buffer_concat(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "list required");
    JSValue list = argv[0];
    JSValue len_val = JS_GetPropertyStr(ctx, list, "length");
    int64_t count;
    JS_ToInt64(ctx, &count, len_val);
    JS_FreeValue(ctx, len_val);
    if (count <= 0) return js_buffer_create(ctx, 0);

    /* Calculate total length */
    int64_t total = -1;
    if (argc >= 2 && !JS_IsUndefined(argv[1]))
        JS_ToInt64(ctx, &total, argv[1]);

    if (total < 0) {
        total = 0;
        for (int64_t i = 0; i < count; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, list, (uint32_t)i);
            size_t len;
            uint8_t *p = js_buffer_get_bytes(ctx, item, &len);
            if (p) total += (int64_t)len;
            JS_FreeValue(ctx, item);
        }
    }

    JSValue result = js_buffer_create(ctx, (size_t)total);
    if (JS_IsException(result)) return result;
    size_t offset = 0;
    uint8_t *dst = js_buffer_get_bytes(ctx, result, &(size_t){0});

    for (int64_t i = 0; i < count && offset < (size_t)total; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, list, (uint32_t)i);
        size_t len;
        uint8_t *src = js_buffer_get_bytes(ctx, item, &len);
        if (src && dst) {
            size_t to_copy = offset + len <= (size_t)total ? len : (size_t)total - offset;
            memcpy(dst + offset, src, to_copy);
            offset += to_copy;
        }
        JS_FreeValue(ctx, item);
    }
    return result;
}

/* ---- Buffer.byteLength(string[, encoding]) ---- */

static JSValue js_buffer_byteLength(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "argument required");

    /* If it's already a Buffer/Uint8Array */
    {
        size_t len;
        uint8_t *p = js_buffer_get_bytes(ctx, argv[0], &len);
        if (p) return JS_NewInt64(ctx, (int64_t)len);
    }

    /* ArrayBuffer */
    {
        size_t ab_len;
        uint8_t *p = JS_GetArrayBuffer(ctx, &ab_len, argv[0]);
        if (p) return JS_NewInt64(ctx, (int64_t)ab_len);
    }

    /* String */
    if (JS_IsString(argv[0])) {
        int encoding = ENC_UTF8;
        if (argc >= 2 && JS_IsString(argv[1])) {
            const char *enc = JS_ToCString(ctx, argv[1]);
            encoding = get_encoding_id(enc);
            JS_FreeCString(ctx, enc);
        }
        size_t str_len;
        const char *str = JS_ToCStringLen(ctx, &str_len, argv[0]);
        if (!str) return JS_EXCEPTION;
        if (encoding == ENC_UTF8 || encoding == ENC_ASCII || encoding == ENC_LATIN1) {
            JS_FreeCString(ctx, str);
            return JS_NewInt64(ctx, (int64_t)str_len);
        }
        size_t out_len;
        uint8_t *bytes = js_buffer_string_to_bytes(ctx, str, str_len, encoding, &out_len);
        JS_FreeCString(ctx, str);
        if (!bytes) return JS_ThrowTypeError(ctx, "failed to compute byte length");
        js_free(ctx, bytes);
        return JS_NewInt64(ctx, (int64_t)out_len);
    }
    return JS_ThrowTypeError(ctx, "cannot determine byte length");
}

/* ---- Buffer.compare(buf1, buf2) ---- */

static JSValue js_buffer_compare_static(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    if (argc < 2) return JS_ThrowTypeError(ctx, "need two buffers");
    size_t len1, len2;
    uint8_t *p1 = js_buffer_get_bytes(ctx, argv[0], &len1);
    uint8_t *p2 = js_buffer_get_bytes(ctx, argv[1], &len2);
    if (!p1 || !p2) return JS_ThrowTypeError(ctx, "arguments must be Buffers");
    size_t min_len = len1 < len2 ? len1 : len2;
    int cmp = memcmp(p1, p2, min_len);
    if (cmp == 0) cmp = (len1 > len2) - (len1 < len2);
    return JS_NewInt32(ctx, cmp);
}

/* ---- Instance: toString([encoding[, start[, end]]]) ---- */

static JSValue js_buffer_toString(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    int encoding = ENC_UTF8;
    if (argc >= 1 && JS_IsString(argv[0])) {
        const char *enc = JS_ToCString(ctx, argv[0]);
        encoding = get_encoding_id(enc);
        JS_FreeCString(ctx, enc);
    }
    size_t len;
    uint8_t *p = js_buffer_get_bytes(ctx, this_val, &len);
    if (!p) return JS_ThrowTypeError(ctx, "not a Buffer");

    int64_t start = 0, end = (int64_t)len;
    if (argc >= 2 && !JS_IsUndefined(argv[1]))
        JS_ToInt64(ctx, &start, argv[1]);
    if (argc >= 3 && !JS_IsUndefined(argv[2]))
        JS_ToInt64(ctx, &end, argv[2]);
    if (start < 0) start = 0;
    if (end > (int64_t)len) end = (int64_t)len;
    if (start >= end) return JS_NewStringLen(ctx, "", 0);

    return js_buffer_bytes_to_string(ctx, p + start, (size_t)(end - start), encoding);
}

/* ---- Instance: slice([start[, end]]) ---- */

static JSValue js_buffer_slice(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    size_t len;
    uint8_t *src = js_buffer_get_bytes(ctx, this_val, &len);
    if (!src) return JS_ThrowTypeError(ctx, "not a Buffer");

    int64_t start = 0, end = (int64_t)len;
    if (argc >= 1 && !JS_IsUndefined(argv[1]))
        JS_ToInt64(ctx, &start, argv[0]);
    if (argc >= 2 && !JS_IsUndefined(argv[1]))
        JS_ToInt64(ctx, &end, argv[1]);
    if (start < 0) start += (int64_t)len;
    if (end < 0) end += (int64_t)len;
    if (start < 0) start = 0;
    if (end > (int64_t)len) end = (int64_t)len;
    if (start >= end) return js_buffer_create(ctx, 0);

    size_t slice_len = (size_t)(end - start);
    JSValue buf = js_buffer_create(ctx, slice_len);
    if (JS_IsException(buf)) return buf;
    uint8_t *dst = js_buffer_get_bytes(ctx, buf, &(size_t){0});
    if (dst) memcpy(dst, src + start, slice_len);
    return buf;
}

/* ---- Instance: subarray([start[, end]]) ---- */

static JSValue js_buffer_subarray(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    size_t buf_len, offset;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, this_val, &offset, &buf_len, &(size_t){0});
    if (JS_IsException(ab)) return JS_ThrowTypeError(ctx, "not a Buffer");

    int64_t start = 0, end = (int64_t)buf_len;
    if (argc >= 1 && !JS_IsUndefined(argv[0]))
        JS_ToInt64(ctx, &start, argv[0]);
    if (argc >= 2 && !JS_IsUndefined(argv[1]))
        JS_ToInt64(ctx, &end, argv[1]);
    if (start < 0) start += (int64_t)buf_len;
    if (end < 0) end += (int64_t)buf_len;
    if (start < 0) start = 0;
    if (end > (int64_t)buf_len) end = (int64_t)buf_len;
    if (start >= end) {
        JS_FreeValue(ctx, ab);
        return js_buffer_create(ctx, 0);
    }

    size_t sub_len = (size_t)(end - start);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue u8_ctor = JS_GetPropertyStr(ctx, global, "Uint8Array");
    JS_FreeValue(ctx, global);
    JSValue args[3] = { ab, JS_NewInt64(ctx, (int64_t)(offset + start)),
                        JS_NewInt64(ctx, (int64_t)sub_len) };
    JSValue u8 = JS_CallConstructor(ctx, u8_ctor, 3, args);
    JS_FreeValue(ctx, u8_ctor);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    if (JS_IsException(u8)) return u8;
    return js_buffer_wrap(ctx, u8);
}

/* ---- Instance: write(string[, offset[, length[, encoding]]]) ---- */

static JSValue js_buffer_write(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    if (argc < 1 || !JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "string required");

    int encoding = ENC_UTF8;
    if (argc >= 4 && JS_IsString(argv[3])) {
        const char *enc = JS_ToCString(ctx, argv[3]);
        encoding = get_encoding_id(enc);
        JS_FreeCString(ctx, enc);
    }

    size_t str_len;
    const char *str = JS_ToCStringLen(ctx, &str_len, argv[0]);
    if (!str) return JS_EXCEPTION;

    size_t byte_len;
    uint8_t *bytes = js_buffer_string_to_bytes(ctx, str, str_len, encoding, &byte_len);
    JS_FreeCString(ctx, str);
    if (!bytes) return JS_ThrowTypeError(ctx, "encoding failed");

    size_t buf_len;
    uint8_t *dst = js_buffer_get_bytes(ctx, this_val, &buf_len);
    if (!dst) { js_free(ctx, bytes); return JS_ThrowTypeError(ctx, "not a Buffer"); }

    int64_t offset = 0, length = (int64_t)byte_len;
    if (argc >= 2 && !JS_IsUndefined(argv[1]))
        JS_ToInt64(ctx, &offset, argv[1]);
    if (argc >= 3 && !JS_IsUndefined(argv[2]))
        JS_ToInt64(ctx, &length, argv[2]);

    if (offset < 0 || (size_t)offset >= buf_len) { js_free(ctx, bytes); return JS_NewInt32(ctx, 0); }
    size_t to_write = (size_t)length < byte_len ? (size_t)length : byte_len;
    if (offset + to_write > buf_len) to_write = buf_len - (size_t)offset;
    memcpy(dst + offset, bytes, to_write);
    js_free(ctx, bytes);
    return JS_NewInt32(ctx, (int32_t)to_write);
}

/* ---- Instance: fill(value[, offset[, end]][, encoding]) ---- */

static JSValue js_buffer_fill(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "value required");

    size_t buf_len;
    uint8_t *dst = js_buffer_get_bytes(ctx, this_val, &buf_len);
    if (!dst) return JS_ThrowTypeError(ctx, "not a Buffer");

    uint8_t fill_byte = 0;
    if (JS_IsString(argv[0])) {
        const char *s = JS_ToCString(ctx, argv[0]);
        fill_byte = (s && s[0]) ? (uint8_t)s[0] : 0;
        if (s) JS_FreeCString(ctx, s);
    } else if (JS_IsNumber(argv[0])) {
        int32_t v;
        JS_ToInt32(ctx, &v, argv[0]);
        fill_byte = (uint8_t)(v & 0xFF);
    }

    int64_t start = 0, end = (int64_t)buf_len;
    if (argc >= 2 && JS_IsNumber(argv[1]))
        JS_ToInt64(ctx, &start, argv[1]);
    if (argc >= 3 && JS_IsNumber(argv[2]))
        JS_ToInt64(ctx, &end, argv[2]);
    if (start < 0) start = 0;
    if (end > (int64_t)buf_len) end = (int64_t)buf_len;
    if (start < end) memset(dst + start, fill_byte, (size_t)(end - start));

    return JS_DupValue(ctx, this_val);
}

/* ---- Instance: copy(target[, targetStart[, sourceStart[, sourceEnd]]]) ---- */

static JSValue js_buffer_copy(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "target required");
    size_t src_len, tgt_len;
    uint8_t *src = js_buffer_get_bytes(ctx, this_val, &src_len);
    uint8_t *tgt = js_buffer_get_bytes(ctx, argv[0], &tgt_len);
    if (!src || !tgt) return JS_ThrowTypeError(ctx, "arguments must be Buffers");

    int64_t tgt_start = 0, src_start = 0, src_end = (int64_t)src_len;
    if (argc >= 2 && !JS_IsUndefined(argv[1]))
        JS_ToInt64(ctx, &tgt_start, argv[1]);
    if (argc >= 3 && !JS_IsUndefined(argv[2]))
        JS_ToInt64(ctx, &src_start, argv[2]);
    if (argc >= 4 && !JS_IsUndefined(argv[3]))
        JS_ToInt64(ctx, &src_end, argv[3]);

    if (src_start < 0) src_start = 0;
    if (src_end > (int64_t)src_len) src_end = (int64_t)src_len;
    if (tgt_start < 0) tgt_start = 0;
    size_t copy_len = (size_t)(src_end - src_start);
    if (tgt_start + copy_len > tgt_len) copy_len = tgt_len - (size_t)tgt_start;
    if (copy_len > 0) memmove(tgt + tgt_start, src + src_start, copy_len);
    return JS_NewInt32(ctx, (int32_t)copy_len);
}

/* ---- Instance: equals(otherBuffer) ---- */

static JSValue js_buffer_equals(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_NewBool(ctx, FALSE);
    size_t len1, len2;
    uint8_t *p1 = js_buffer_get_bytes(ctx, this_val, &len1);
    uint8_t *p2 = js_buffer_get_bytes(ctx, argv[0], &len2);
    if (!p1 || !p2) return JS_NewBool(ctx, FALSE);
    return JS_NewBool(ctx, len1 == len2 && memcmp(p1, p2, len1) == 0);
}

/* ---- Instance: compare(otherBuffer) ---- */

static JSValue js_buffer_compare_inst(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_ThrowTypeError(ctx, "argument required");
    size_t len1, len2;
    uint8_t *p1 = js_buffer_get_bytes(ctx, this_val, &len1);
    uint8_t *p2 = js_buffer_get_bytes(ctx, argv[0], &len2);
    if (!p1 || !p2) return JS_ThrowTypeError(ctx, "arguments must be Buffers");
    size_t min_len = len1 < len2 ? len1 : len2;
    int cmp = memcmp(p1, p2, min_len);
    if (cmp == 0) cmp = (len1 > len2) - (len1 < len2);
    return JS_NewInt32(ctx, cmp);
}

/* ---- Instance: indexOf(value[, byteOffset][, encoding]) ---- */

static JSValue js_buffer_indexOf(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_NewInt32(ctx, -1);
    size_t buf_len;
    uint8_t *buf = js_buffer_get_bytes(ctx, this_val, &buf_len);
    if (!buf) return JS_NewInt32(ctx, -1);

    int64_t offset = 0;
    if (argc >= 2 && JS_IsNumber(argv[1]))
        JS_ToInt64(ctx, &offset, argv[1]);
    if (offset < 0) offset = 0;

    /* Number search (single byte) */
    if (JS_IsNumber(argv[0])) {
        int32_t val;
        JS_ToInt32(ctx, &val, argv[0]);
        uint8_t byte = (uint8_t)(val & 0xFF);
        for (int64_t i = offset; i < (int64_t)buf_len; i++) {
            if (buf[i] == byte) return JS_NewInt64(ctx, i);
        }
        return JS_NewInt32(ctx, -1);
    }

    /* String or Buffer search */
    const uint8_t *needle;
    size_t needle_len;
    uint8_t *needle_alloc = NULL;

    if (JS_IsString(argv[0])) {
        int encoding = ENC_UTF8;
        if (argc >= 3 && JS_IsString(argv[2])) {
            const char *enc = JS_ToCString(ctx, argv[2]);
            encoding = get_encoding_id(enc);
            JS_FreeCString(ctx, enc);
        }
        size_t str_len;
        const char *str = JS_ToCStringLen(ctx, &str_len, argv[0]);
        needle = js_buffer_string_to_bytes(ctx, str, str_len, encoding, &needle_len);
        needle_alloc = (uint8_t *)needle;
        JS_FreeCString(ctx, str);
        if (!needle) return JS_NewInt32(ctx, -1);
    } else {
        needle = js_buffer_get_bytes(ctx, argv[0], &needle_len);
        if (!needle) return JS_NewInt32(ctx, -1);
    }

    int64_t result = -1;
    if (needle_len == 0) {
        result = offset;
    } else if (needle_len <= buf_len) {
        for (int64_t i = offset; i + needle_len <= buf_len; i++) {
            if (buf[i] == needle[0] && memcmp(buf + i, needle, needle_len) == 0) {
                result = i;
                break;
            }
        }
    }
    if (needle_alloc) js_free(ctx, needle_alloc);
    return JS_NewInt64(ctx, result);
}

/* ---- Instance: includes(value[, byteOffset][, encoding]) ---- */

static JSValue js_buffer_includes(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSValue idx = js_buffer_indexOf(ctx, this_val, argc, argv);
    int64_t v;
    JS_ToInt64(ctx, &v, idx);
    return JS_NewBool(ctx, v >= 0);
}

/* ---- Instance: toJSON() ---- */

static JSValue js_buffer_toJSON(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    size_t len;
    uint8_t *p = js_buffer_get_bytes(ctx, this_val, &len);
    if (!p) return JS_ThrowTypeError(ctx, "not a Buffer");

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, "Buffer"));
    JSValue data = JS_NewArray(ctx);
    for (size_t i = 0; i < len; i++)
        JS_SetPropertyUint32(ctx, data, (uint32_t)i, JS_NewInt32(ctx, p[i]));
    JS_SetPropertyStr(ctx, obj, "data", data);
    return obj;
}

/* ---- Instance: swap16/32/64 ---- */

static JSValue js_buffer_swap(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv, int magic)
{
    size_t len;
    uint8_t *p = js_buffer_get_bytes(ctx, this_val, &len);
    if (!p) return JS_ThrowTypeError(ctx, "not a Buffer");
    int elem_size = magic; /* 2, 4, or 8 */
    if (len % elem_size != 0)
        return JS_ThrowRangeError(ctx, "Buffer size must be a multiple of %d", elem_size);
    for (size_t i = 0; i < len; i += elem_size) {
        for (int j = 0; j < elem_size / 2; j++) {
            uint8_t tmp = p[i + j];
            p[i + j] = p[i + elem_size - 1 - j];
            p[i + elem_size - 1 - j] = tmp;
        }
    }
    return JS_DupValue(ctx, this_val);
}

/* ---- Instance: readUInt8/writeUInt8 etc. (basic set) ---- */

static JSValue js_buffer_readUInt(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv, int magic)
{
    /* magic: bits<<4 | big_endian */
    int bits = magic >> 4;
    int big_endian = magic & 1;
    size_t len;
    uint8_t *p = js_buffer_get_bytes(ctx, this_val, &len);
    if (!p) return JS_ThrowTypeError(ctx, "not a Buffer");
    int64_t offset = 0;
    if (argc >= 1 && !JS_IsUndefined(argv[0]))
        JS_ToInt64(ctx, &offset, argv[0]);
    int nbytes = bits / 8;
    if (offset < 0 || (size_t)(offset + nbytes) > len)
        return JS_ThrowRangeError(ctx, "out of range");

    uint64_t val = 0;
    const uint8_t *src = p + offset;
    if (big_endian) {
        for (int i = 0; i < nbytes; i++)
            val = (val << 8) | src[i];
    } else {
        for (int i = nbytes - 1; i >= 0; i--)
            val = (val << 8) | src[i];
    }
    if (bits <= 32)
        return JS_NewInt32(ctx, (int32_t)(val & 0xFFFFFFFF));
    return JS_NewInt64(ctx, (int64_t)val);
}

static JSValue js_buffer_writeUInt(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv, int magic)
{
    int bits = magic >> 4;
    int big_endian = magic & 1;
    size_t len;
    uint8_t *p = js_buffer_get_bytes(ctx, this_val, &len);
    if (!p) return JS_ThrowTypeError(ctx, "not a Buffer");
    if (argc < 2) return JS_ThrowTypeError(ctx, "need value and offset");
    int64_t val, offset;
    JS_ToInt64(ctx, &val, argv[0]);
    JS_ToInt64(ctx, &offset, argv[1]);
    int nbytes = bits / 8;
    if (offset < 0 || (size_t)(offset + nbytes) > len)
        return JS_ThrowRangeError(ctx, "out of range");
    uint64_t uval = (uint64_t)val;
    uint8_t *dst = p + offset;
    if (big_endian) {
        for (int i = nbytes - 1; i >= 0; i--) {
            dst[i] = (uint8_t)(uval & 0xFF);
            uval >>= 8;
        }
    } else {
        for (int i = 0; i < nbytes; i++) {
            dst[i] = (uint8_t)(uval & 0xFF);
            uval >>= 8;
        }
    }
    return JS_DupValue(ctx, this_val);
}

/* ---- Module init ---- */

static int js_buffer_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue u8_ctor = JS_GetPropertyStr(ctx, global, "Uint8Array");
    JSValue u8_proto = JS_GetPropertyStr(ctx, u8_ctor, "prototype");

    /* Create Buffer.prototype inheriting from Uint8Array.prototype */
    JSValue buffer_prototype = JS_NewObjectProto(ctx, u8_proto);
    JS_FreeValue(ctx, u8_proto);

    /* Instance methods */
    JS_SetPropertyStr(ctx, buffer_prototype, "toString",
                      JS_NewCFunction(ctx, js_buffer_toString, "toString", 3));
    JS_SetPropertyStr(ctx, buffer_prototype, "slice",
                      JS_NewCFunction(ctx, js_buffer_slice, "slice", 2));
    JS_SetPropertyStr(ctx, buffer_prototype, "subarray",
                      JS_NewCFunction(ctx, js_buffer_subarray, "subarray", 2));
    JS_SetPropertyStr(ctx, buffer_prototype, "write",
                      JS_NewCFunction(ctx, js_buffer_write, "write", 4));
    JS_SetPropertyStr(ctx, buffer_prototype, "fill",
                      JS_NewCFunction(ctx, js_buffer_fill, "fill", 3));
    JS_SetPropertyStr(ctx, buffer_prototype, "copy",
                      JS_NewCFunction(ctx, js_buffer_copy, "copy", 4));
    JS_SetPropertyStr(ctx, buffer_prototype, "equals",
                      JS_NewCFunction(ctx, js_buffer_equals, "equals", 1));
    JS_SetPropertyStr(ctx, buffer_prototype, "compare",
                      JS_NewCFunction(ctx, js_buffer_compare_inst, "compare", 1));
    JS_SetPropertyStr(ctx, buffer_prototype, "indexOf",
                      JS_NewCFunction(ctx, js_buffer_indexOf, "indexOf", 3));
    JS_SetPropertyStr(ctx, buffer_prototype, "includes",
                      JS_NewCFunction(ctx, js_buffer_includes, "includes", 3));
    JS_SetPropertyStr(ctx, buffer_prototype, "toJSON",
                      JS_NewCFunction(ctx, js_buffer_toJSON, "toJSON", 0));
    JS_SetPropertyStr(ctx, buffer_prototype, "swap16",
                      JS_NewCFunctionMagic(ctx, js_buffer_swap, "swap16", 0, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(ctx, buffer_prototype, "swap32",
                      JS_NewCFunctionMagic(ctx, js_buffer_swap, "swap32", 0, JS_CFUNC_generic_magic, 4));
    JS_SetPropertyStr(ctx, buffer_prototype, "swap64",
                      JS_NewCFunctionMagic(ctx, js_buffer_swap, "swap64", 0, JS_CFUNC_generic_magic, 8));
    /* readUInt / writeUInt (BE and LE, 8/16/32 bits) */
    JS_SetPropertyStr(ctx, buffer_prototype, "readUInt8",
                      JS_NewCFunctionMagic(ctx, js_buffer_readUInt, "readUInt8", 1, JS_CFUNC_generic_magic, 8 << 4));
    JS_SetPropertyStr(ctx, buffer_prototype, "writeUInt8",
                      JS_NewCFunctionMagic(ctx, js_buffer_writeUInt, "writeUInt8", 2, JS_CFUNC_generic_magic, 8 << 4));
    JS_SetPropertyStr(ctx, buffer_prototype, "readUInt16BE",
                      JS_NewCFunctionMagic(ctx, js_buffer_readUInt, "readUInt16BE", 1, JS_CFUNC_generic_magic, (16 << 4) | 1));
    JS_SetPropertyStr(ctx, buffer_prototype, "readUInt16LE",
                      JS_NewCFunctionMagic(ctx, js_buffer_readUInt, "readUInt16LE", 1, JS_CFUNC_generic_magic, (16 << 4) | 0));
    JS_SetPropertyStr(ctx, buffer_prototype, "writeUInt16BE",
                      JS_NewCFunctionMagic(ctx, js_buffer_writeUInt, "writeUInt16BE", 2, JS_CFUNC_generic_magic, (16 << 4) | 1));
    JS_SetPropertyStr(ctx, buffer_prototype, "writeUInt16LE",
                      JS_NewCFunctionMagic(ctx, js_buffer_writeUInt, "writeUInt16LE", 2, JS_CFUNC_generic_magic, (16 << 4) | 0));
    JS_SetPropertyStr(ctx, buffer_prototype, "readUInt32BE",
                      JS_NewCFunctionMagic(ctx, js_buffer_readUInt, "readUInt32BE", 1, JS_CFUNC_generic_magic, (32 << 4) | 1));
    JS_SetPropertyStr(ctx, buffer_prototype, "readUInt32LE",
                      JS_NewCFunctionMagic(ctx, js_buffer_readUInt, "readUInt32LE", 1, JS_CFUNC_generic_magic, (32 << 4) | 0));
    JS_SetPropertyStr(ctx, buffer_prototype, "writeUInt32BE",
                      JS_NewCFunctionMagic(ctx, js_buffer_writeUInt, "writeUInt32BE", 2, JS_CFUNC_generic_magic, (32 << 4) | 1));
    JS_SetPropertyStr(ctx, buffer_prototype, "writeUInt32LE",
                      JS_NewCFunctionMagic(ctx, js_buffer_writeUInt, "writeUInt32LE", 2, JS_CFUNC_generic_magic, (32 << 4) | 0));
    JS_SetPropertyStr(ctx, buffer_prototype, "readInt32BE",
                      JS_NewCFunctionMagic(ctx, js_buffer_readUInt, "readInt32BE", 1, JS_CFUNC_generic_magic, (32 << 4) | 1));
    JS_SetPropertyStr(ctx, buffer_prototype, "readInt32LE",
                      JS_NewCFunctionMagic(ctx, js_buffer_readUInt, "readInt32LE", 1, JS_CFUNC_generic_magic, (32 << 4) | 0));
    JS_SetPropertyStr(ctx, buffer_prototype, "writeInt32BE",
                      JS_NewCFunctionMagic(ctx, js_buffer_writeUInt, "writeInt32BE", 2, JS_CFUNC_generic_magic, (32 << 4) | 1));
    JS_SetPropertyStr(ctx, buffer_prototype, "writeInt32LE",
                      JS_NewCFunctionMagic(ctx, js_buffer_writeUInt, "writeInt32LE", 2, JS_CFUNC_generic_magic, (32 << 4) | 0));

    /* Create Buffer constructor */
    JSValue buffer_ctor = JS_NewCFunction(ctx, js_buffer_alloc, "Buffer", 2);
    JS_SetPropertyStr(ctx, buffer_ctor, "prototype", JS_DupValue(ctx, buffer_prototype));
    JS_SetPropertyStr(ctx, buffer_prototype, "constructor", JS_DupValue(ctx, buffer_ctor));

    /* Static methods */
    JS_SetPropertyStr(ctx, buffer_ctor, "from",
                      JS_NewCFunction(ctx, js_buffer_from, "from", 3));
    JS_SetPropertyStr(ctx, buffer_ctor, "alloc",
                      JS_NewCFunction(ctx, js_buffer_alloc, "alloc", 2));
    JS_SetPropertyStr(ctx, buffer_ctor, "allocUnsafe",
                      JS_NewCFunction(ctx, js_buffer_allocUnsafe, "allocUnsafe", 1));
    JS_SetPropertyStr(ctx, buffer_ctor, "isBuffer",
                      JS_NewCFunction(ctx, js_buffer_isBuffer, "isBuffer", 1));
    JS_SetPropertyStr(ctx, buffer_ctor, "concat",
                      JS_NewCFunction(ctx, js_buffer_concat, "concat", 2));
    JS_SetPropertyStr(ctx, buffer_ctor, "byteLength",
                      JS_NewCFunction(ctx, js_buffer_byteLength, "byteLength", 2));
    JS_SetPropertyStr(ctx, buffer_ctor, "compare",
                      JS_NewCFunction(ctx, js_buffer_compare_static, "compare", 2));

    /* Buffer.constants */
    JSValue constants = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, constants, "MAX_LENGTH", JS_NewInt64(ctx, 2147483647));
    JS_SetPropertyStr(ctx, constants, "MAX_STRING_LENGTH", JS_NewInt64(ctx, 2147483647));
    JS_SetPropertyStr(ctx, buffer_ctor, "constants", constants);

    JS_FreeValue(ctx, u8_ctor);

    /* Store prototype on global object so js_buffer_wrap can retrieve it.
       This avoids a static JSValue that leaks on runtime cleanup. */
    JS_SetPropertyStr(ctx, global, BUFFER_PROTO_KEY, buffer_prototype);
    JS_FreeValue(ctx, global);

    /* exports */
    if (JS_SetModuleExport(ctx, m, "default", buffer_ctor) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "Buffer", JS_DupValue(ctx, buffer_ctor)) < 0)
        return -1;
    if (JS_SetModuleExport(ctx, m, "SlowBuffer",
                           JS_NewCFunction(ctx, js_buffer_allocUnsafe, "SlowBuffer", 1)) < 0)
        return -1;

    return 0;
}

JSModuleDef *js_init_module_buffer(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m = JS_NewCModule(ctx, module_name, js_buffer_init);
    if (!m) return NULL;
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "Buffer");
    JS_AddModuleExport(ctx, m, "SlowBuffer");
    return m;
}
