/* Open Watcom compatibility shim. Force-included via wcc386 -fi=ow_compat.h
 * before every BearSSL translation unit.
 */
#ifndef OW_COMPAT_H
#define OW_COMPAT_H

/* Watcom v2 does ship <stdint.h>, but be explicit in case of older snapshots.
 */
#if defined(__WATCOMC__) && !defined(_STDINT_H_INCLUDED)
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed __int64 int64_t;
typedef unsigned __int64 uint64_t;
typedef int32_t intptr_t;
typedef uint32_t uintptr_t;
#define _STDINT_H_INCLUDED
#endif

/* C99 keywords BearSSL headers use. */
#if defined(__WATCOMC__) && !defined(__cplusplus)
#define inline __inline
#endif

/* BearSSL config: pin to portable backends; no x86 intrinsics; we have 64-bit.
 * Disable Win32 system RNG (CryptGenRandom needs ADVAPI32, not on Win95 RTM)
 * and Win32 time (BearSSL's path is fine but we already supply plat_time_unix
 * and want one code path). Both come from plat_*.
 */
#define BR_64 1
#define BR_LOMUL 1
#define BR_NO_ARITH_SHIFT 0
#define BR_USE_WIN32_RAND 0
#define BR_USE_WIN32_TIME 0
#define BR_USE_UNIX_TIME 0
#define BR_USE_URANDOM 0
#undef BR_INT128
#undef BR_SSE2
#undef BR_AES_X86NI

#endif /* OW_COMPAT_H */
