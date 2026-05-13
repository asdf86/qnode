/*
 * MSVC Compatibility Header for QNode
 * This header provides MSVC equivalents for GCC-specific extensions
 */

#ifndef MSVC_COMPAT_H
#define MSVC_COMPAT_H

#ifdef _MSC_VER

/* Include MSVC-specific headers */
#include <intrin.h>
#include <Windows.h>

/* Compiler attributes */
#define force_inline __forceinline
#define no_inline __declspec(noinline)
#define __maybe_unused

/* Branch prediction */
#define likely(x)   (x)
#define unlikely(x) (x)

/* GCC built-in replacements */
static inline int __builtin_clz(unsigned int x) {
    unsigned long index;
    if (_BitScanReverse(&index, x)) {
        return 31 - index;
    }
    return 32;  // Undefined behavior for x=0
}

static inline int __builtin_clzll(unsigned long long x) {
    unsigned long index;
    if (_BitScanReverse64(&index, x)) {
        return 63 - index;
    }
    return 64;  // Undefined behavior for x=0
}

static inline int __builtin_ctz(unsigned int x) {
    unsigned long index;
    if (_BitScanForward(&index, x)) {
        return index;
    }
    return 32;  // Undefined behavior for x=0
}

static inline int __builtin_ctzll(unsigned long long x) {
    unsigned long index;
    if (_BitScanForward64(&index, x)) {
        return index;
    }
    return 64;  // Undefined behavior for x=0
}

#define __builtin_expect(x, v) (x)

/* Packed structures - use MSVC pragma pack */
#define __attribute__(x)

/* Define packed structures for MSVC */
#pragma pack(push, 1)

struct packed_u64 {
    unsigned long long v;
};

struct packed_u32 {
    unsigned int v;
};

struct packed_u16 {
    unsigned short v;
};

#pragma pack(pop)

/* Format specifier macros */
#define PRIu64 "I64u"
#define PRIi64 "I64d"
#define PRIx64 "I64x"

/* Additional MSVC compatibility */
#define __restrict__ __restrict
#ifdef _WIN64
typedef __int64 ssize_t; // 64位 Windows 下定义为 64 位带符号整型
#else
typedef int ssize_t;     // 32位 Windows 下定义为 32 位带符号整型
#endif
#endif /* _MSC_VER */

#endif /* MSVC_COMPAT_H */
