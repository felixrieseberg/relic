/* devkitPPC / Wii compatibility shim. Force-included via -include before
 * every BearSSL translation unit (see build/wii/build_bearssl.sh).
 *
 * devkitPPC is GCC targeting big-endian 32-bit PowerPC (750CL "Broadway")
 * against newlib. <stdint.h> is present so we don't redefine integer types;
 * we only pin BearSSL's compile-time knobs so it doesn't reach for POSIX
 * entropy/time sources that aren't there on bare libogc.
 */
#ifndef WII_COMPAT_H
#define WII_COMPAT_H

/* BearSSL config: pin to portable backends.
 * - BR_LOMUL: 32x32->64 mul is what the 750CL has; avoid the 64x64 paths.
 * - BR_USE_UNIX_TIME / BR_USE_URANDOM: OFF. newlib's time() works (libogc
 *   reads the EXI RTC), but we centralise time + entropy through plat_* and
 *   don't want BearSSL probing for /dev/urandom that isn't mounted.
 * - BR_USE_WIN32_*: OFF (obvious).
 * - BR_BE_UNALIGNED: leave undefined -- Broadway handles unaligned integer
 *   loads in hardware (with a cycle penalty) for cacheable big-endian
 *   storage, but lmw/stmw, FP and string ops still take the alignment
 *   exception; the byte-wise path is the safe default.
 */
#define BR_LOMUL 1
#define BR_USE_UNIX_TIME 0
#define BR_USE_URANDOM 0
#define BR_USE_WIN32_RAND 0
#define BR_USE_WIN32_TIME 0
#undef BR_INT128
#undef BR_POWER8

#endif /* WII_COMPAT_H */
