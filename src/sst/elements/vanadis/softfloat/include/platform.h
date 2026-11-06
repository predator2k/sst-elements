/* Platform definitions of Berkeley SoftFloat-3e inside the Vanadis element (Linux, x86-64 and aarch64 hosts). */
#ifndef VANADIS_SOFTFLOAT_PLATFORM_H
#define VANADIS_SOFTFLOAT_PLATFORM_H
#define LITTLEENDIAN 1
#define INLINE_LEVEL 5
#define SOFTFLOAT_FAST_DIV32TO16 1
#define SOFTFLOAT_FAST_DIV64TO32 1
#define SOFTFLOAT_FAST_INT64 1
#define SOFTFLOAT_ROUND_ODD 1
#define INLINE static inline
/* The rounding mode and exception flags are per-thread, because a Vanadis core may run on any SST thread. */
#define THREAD_LOCAL __thread
#ifdef __GNUC__
#define SOFTFLOAT_BUILTIN_CLZ 1
#define SOFTFLOAT_INTRINSIC_INT128 1
#include "opts-GCC.h"
#endif
#endif
