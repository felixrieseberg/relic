/* nxdk / Xbox compatibility shim. Force-included via clang -include before
 * every BearSSL translation unit (see build/xbox/build_bearssl.sh).
 *
 * nxdk is an LLVM-based toolchain producing i386 .xbe binaries against the
 * XBOX kernel. Its libc is based on pdclib/newlib with a bespoke CRT. It
 * exposes <stdint.h>, so we don't redefine the integer types; we only pin
 * BearSSL's compile-time knobs so the build doesn't reach for POSIX or
 * Win32 entropy/time sources that don't exist here.
 */
#ifndef XBOX_COMPAT_H
#define XBOX_COMPAT_H

/* BearSSL config: pin to portable backends.
 * - BR_64: the Pentium III target has a 64-bit multiply; use the wider paths.
 * - BR_LOMUL: prefer 32x32 partial products (the PIII's 64-bit multiply is
 *   fine, but BR_LOMUL avoids code that leans on fast 64x64).
 * - BR_USE_WIN32_RAND / _TIME: OFF. nxdk's CRT doesn't expose
 *   CryptGenRandom or GetSystemTimeAsFileTime; we supply both via plat_*.
 * - BR_USE_UNIX_TIME / BR_USE_URANDOM: OFF. No /dev/urandom, no time(2)
 *   semantics we want to trust on an Xbox with a dead RTC battery.
 * - BR_INT128 / BR_SSE2 / BR_AES_X86NI: OFF. i386 LLVM output, no SSE2
 *   baseline assumed (nxdk's default is i686-pc-win32-elf, pre-SSE2 safe),
 *   no AES-NI on an original Xbox Pentium III.
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

#endif /* XBOX_COMPAT_H */
