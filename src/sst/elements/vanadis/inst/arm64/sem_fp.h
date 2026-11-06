// Copyright 2009-2025 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2025, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

// Element-level floating-point operations of A64, AdvSIMD and SVE, on raw uint16_t, uint32_t and uint64_t
// values. The arithmetic is Berkeley SoftFloat-3e, so the result does not depend on the host's rounding or
// NaN rules; the functions here add what the Arm architecture defines around it: input and output flushing
// (FZ, FZ16), the NaN selection of FPProcessNaNs and FPProcessNaNs3 (including DN), and tininess detected
// before rounding.
//
// FpCtx carries the FPCR value and collects the FPSR cumulative bits of one instruction.

#ifndef _H_VANADIS_ARM64_SEM_FP
#define _H_VANADIS_ARM64_SEM_FP

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#ifndef THREAD_LOCAL
#define THREAD_LOCAL __thread
#endif
extern "C" {
#include "softfloat/include/softfloat.h"
}

namespace SST {
namespace Vanadis {
namespace A64Fp {

// FPSR cumulative exception bits.
constexpr uint32_t IOC = 1u << 0;
constexpr uint32_t DZC = 1u << 1;
constexpr uint32_t OFC = 1u << 2;
constexpr uint32_t UFC = 1u << 3;
constexpr uint32_t IXC = 1u << 4;
constexpr uint32_t IDC = 1u << 7;
constexpr uint32_t QC  = 1u << 27;

// FPCR fields.
constexpr uint32_t FPCR_AHP  = 1u << 26;
constexpr uint32_t FPCR_DN   = 1u << 25;
constexpr uint32_t FPCR_FZ   = 1u << 24;
constexpr uint32_t FPCR_FZ16 = 1u << 19;

// Rounding modes as the FPCR.RMode field and the FP-to-integer "rounding" operands give them.
enum class Rounding : uint32_t { TIEEVEN = 0, POSINF = 1, NEGINF = 2, ZERO = 3, TIEAWAY = 4, ODD = 5 };

struct FpCtx
{
    uint32_t fpcr;
    uint32_t flags = 0;
    explicit FpCtx(uint32_t fpcr_value) : fpcr(fpcr_value) {}
    Rounding rmode() const { return static_cast<Rounding>((fpcr >> 22) & 3); }
    bool     dn() const { return fpcr & FPCR_DN; }
};

inline uint_fast8_t
sfRounding(Rounding r)
{
    switch ( r ) {
    case Rounding::TIEEVEN: return softfloat_round_near_even;
    case Rounding::POSINF: return softfloat_round_max;
    case Rounding::NEGINF: return softfloat_round_min;
    case Rounding::ZERO: return softfloat_round_minMag;
    case Rounding::TIEAWAY: return softfloat_round_near_maxMag;
    default: return softfloat_round_odd;
    }
}

// Field layout of each format.
template <typename T>
struct Fmt;
template <>
struct Fmt<uint16_t>
{
    static constexpr int      FRAC = 10, EXP = 5, BITS = 16;
    static constexpr uint16_t SIGN = 0x8000, EXPM = 0x7C00, FRACM = 0x03FF, QUIET = 0x0200, DEFNAN = 0x7E00, INF = 0x7C00;
    static constexpr int      BIAS = 15;
};
template <>
struct Fmt<uint32_t>
{
    static constexpr int      FRAC = 23, EXP = 8, BITS = 32;
    static constexpr uint32_t SIGN = 0x80000000u, EXPM = 0x7F800000u, FRACM = 0x007FFFFFu, QUIET = 0x00400000u, DEFNAN = 0x7FC00000u, INF = 0x7F800000u;
    static constexpr int      BIAS = 127;
};
template <>
struct Fmt<uint64_t>
{
    static constexpr int      FRAC = 52, EXP = 11, BITS = 64;
    static constexpr uint64_t SIGN = 0x8000000000000000ull, EXPM = 0x7FF0000000000000ull, FRACM = 0x000FFFFFFFFFFFFFull,
                              QUIET = 0x0008000000000000ull, DEFNAN = 0x7FF8000000000000ull, INF = 0x7FF0000000000000ull;
    static constexpr int      BIAS = 1023;
};

template <typename T> inline bool isNaN(T v) { return (v & Fmt<T>::EXPM) == Fmt<T>::EXPM && (v & Fmt<T>::FRACM); }
template <typename T> inline bool isSNaN(T v) { return isNaN(v) && !(v & Fmt<T>::QUIET); }
template <typename T> inline bool isInf(T v) { return (v & ~Fmt<T>::SIGN) == Fmt<T>::EXPM; }
template <typename T> inline bool isZero(T v) { return (v & ~Fmt<T>::SIGN) == 0; }
template <typename T> inline bool isSub(T v) { return (v & Fmt<T>::EXPM) == 0 && (v & Fmt<T>::FRACM); }
template <typename T> inline bool sign(T v) { return v & Fmt<T>::SIGN; }
template <typename T> inline T    zero(bool s) { return s ? Fmt<T>::SIGN : T(0); }
template <typename T> inline T    infinity(bool s) { return s ? T(Fmt<T>::SIGN | Fmt<T>::INF) : T(Fmt<T>::INF); }

// Flushes one input per FPUnpack.
template <typename T>
inline T
flushIn(T v, FpCtx& c)
{
    if ( !isSub(v) ) return v;
    if constexpr ( sizeof(T) == 2 ) {
        if ( c.fpcr & FPCR_FZ16 ) return zero<T>(sign(v));
    }
    else {
        if ( c.fpcr & FPCR_FZ ) {
            c.flags |= IDC;
            return zero<T>(sign(v));
        }
    }
    return v;
}

template <typename T>
inline bool
flushesOutput(const FpCtx& c)
{
    return sizeof(T) == 2 ? (c.fpcr & FPCR_FZ16) : (c.fpcr & FPCR_FZ);
}

// One quieted NaN, or the default NaN under DN.
template <typename T>
inline T
procNaN(T v, FpCtx& c)
{
    if ( isSNaN(v) ) c.flags |= IOC;
    return c.dn() ? T(Fmt<T>::DEFNAN) : T(v | Fmt<T>::QUIET);
}

// FPProcessNaNs: true when an input is a NaN, *out then holds the result.
template <typename T>
inline bool
procNaNs(T a, T b, FpCtx& c, T* out)
{
    if ( isSNaN(a) ) { *out = procNaN(a, c); return true; }
    if ( isSNaN(b) ) { *out = procNaN(b, c); return true; }
    if ( isNaN(a) ) { *out = procNaN(a, c); return true; }
    if ( isNaN(b) ) { *out = procNaN(b, c); return true; }
    return false;
}

template <typename T>
inline bool
procNaNs3(T a, T b, T d, FpCtx& c, T* out)
{
    if ( isSNaN(a) ) { *out = procNaN(a, c); return true; }
    if ( isSNaN(b) ) { *out = procNaN(b, c); return true; }
    if ( isSNaN(d) ) { *out = procNaN(d, c); return true; }
    if ( isNaN(a) ) { *out = procNaN(a, c); return true; }
    if ( isNaN(b) ) { *out = procNaN(b, c); return true; }
    if ( isNaN(d) ) { *out = procNaN(d, c); return true; }
    return false;
}

inline void
sfBegin(Rounding r)
{
    softfloat_roundingMode   = sfRounding(r);
    softfloat_exceptionFlags = 0;
    softfloat_detectTininess = softfloat_tininess_beforeRounding;
}

inline void
sfCollect(FpCtx& c)
{
    const uint_fast8_t e = softfloat_exceptionFlags;
    if ( e & softfloat_flag_invalid ) c.flags |= IOC;
    if ( e & softfloat_flag_infinite ) c.flags |= DZC;
    if ( e & softfloat_flag_overflow ) c.flags |= OFC;
    if ( e & softfloat_flag_underflow ) c.flags |= UFC;
    if ( e & softfloat_flag_inexact ) c.flags |= IXC;
}

// The result of an arithmetic operation under the flush mode of the output.
template <typename T>
inline T
sfFinish(T r, FpCtx& c)
{
    const uint_fast8_t e = softfloat_exceptionFlags;
    if ( flushesOutput<T>(c) && (isSub(r) || (e & softfloat_flag_underflow)) ) {
        c.flags |= UFC;
        sfCollect(c);
        c.flags &= ~IXC;
        return zero<T>(sign(r));
    }
    sfCollect(c);
    return r;
}

// Conversions between raw values and the SoftFloat types.
inline float16_t f(uint16_t v) { float16_t x; x.v = v; return x; }
inline float32_t f(uint32_t v) { float32_t x; x.v = v; return x; }
inline float64_t f(uint64_t v) { float64_t x; x.v = v; return x; }

#define A64_FP_BINARY(NAME, SF16, SF32, SF64)                                                    \
    template <typename T>                                                                        \
    inline T NAME(T a, T b, FpCtx& c)                                                            \
    {                                                                                            \
        T out;                                                                                   \
        a = flushIn(a, c);                                                                       \
        b = flushIn(b, c);                                                                       \
        if ( procNaNs(a, b, c, &out) ) return out;                                               \
        sfBegin(c.rmode());                                                                      \
        T r;                                                                                     \
        if constexpr ( sizeof(T) == 2 ) r = SF16(f(a), f(b)).v;                                  \
        else if constexpr ( sizeof(T) == 4 ) r = SF32(f(a), f(b)).v;                             \
        else r = SF64(f(a), f(b)).v;                                                             \
        if ( isNaN(r) ) { sfCollect(c); return T(Fmt<T>::DEFNAN); }                              \
        return sfFinish(r, c);                                                                   \
    }

A64_FP_BINARY(add, f16_add, f32_add, f64_add)
A64_FP_BINARY(sub, f16_sub, f32_sub, f64_sub)
A64_FP_BINARY(mul, f16_mul, f32_mul, f64_mul)
A64_FP_BINARY(div, f16_div, f32_div, f64_div)
#undef A64_FP_BINARY

// FMULX: infinity times zero is 2 with the sign of the product.
template <typename T>
inline T
mulx(T a, T b, FpCtx& c)
{
    T out;
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( procNaNs(a, b, c, &out) ) return out;
    if ( (isInf(a) && isZero(b)) || (isZero(a) && isInf(b)) ) {
        const T two = T(T(Fmt<T>::BIAS + 1) << Fmt<T>::FRAC);
        return T(two | ((sign(a) != sign(b)) ? Fmt<T>::SIGN : T(0)));
    }
    return mul(a, b, c);
}

template <typename T>
inline T
abd(T a, T b, FpCtx& c)
{
    const T r = sub(a, b, c);
    return isNaN(r) ? r : T(r & ~Fmt<T>::SIGN);
}

template <typename T>
inline T
neg(T a)
{
    return T(a ^ Fmt<T>::SIGN);
}

template <typename T>
inline T
abs(T a)
{
    return T(a & ~Fmt<T>::SIGN);
}

template <typename T>
inline T
sqrt(T a, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) return procNaN(a, c);
    sfBegin(c.rmode());
    T r;
    if constexpr ( sizeof(T) == 2 ) r = f16_sqrt(f(a)).v;
    else if constexpr ( sizeof(T) == 4 ) r = f32_sqrt(f(a)).v;
    else r = f64_sqrt(f(a)).v;
    if ( isNaN(r) ) { sfCollect(c); return T(Fmt<T>::DEFNAN); }
    return sfFinish(r, c);
}

// FPMulAdd(addend, op1, op2) with the sign flips already applied by the caller.
template <typename T>
inline T
mulAdd(T addend, T x, T y, FpCtx& c)
{
    T out;
    addend = flushIn(addend, c);
    x      = flushIn(x, c);
    y      = flushIn(y, c);
    const bool inf_zero = (isInf(x) && isZero(y)) || (isZero(x) && isInf(y));
    // FPProcessNaNs3 considers the addend first; a quiet-NaN addend with infinity times zero is invalid.
    if ( isNaN(addend) && !isSNaN(addend) && !isSNaN(x) && !isSNaN(y) && inf_zero ) {
        c.flags |= IOC;
        return T(Fmt<T>::DEFNAN);
    }
    if ( procNaNs3(addend, x, y, c, &out) ) return out;
    sfBegin(c.rmode());
    T r;
    if constexpr ( sizeof(T) == 2 ) r = f16_mulAdd(f(x), f(y), f(addend)).v;
    else if constexpr ( sizeof(T) == 4 ) r = f32_mulAdd(f(x), f(y), f(addend)).v;
    else r = f64_mulAdd(f(x), f(y), f(addend)).v;
    if ( isNaN(r) ) { sfCollect(c); return T(Fmt<T>::DEFNAN); }
    return sfFinish(r, c);
}

// The ordered comparison of two non-NaN values: -1, 0 or 1. Signed zeros compare equal.
template <typename T>
inline int
orderOf(T a, T b)
{
    if ( isZero(a) && isZero(b) ) return 0;
    const bool sa = sign(a), sb = sign(b);
    if ( sa != sb ) return sa ? -1 : 1;
    const T ma = T(a & ~Fmt<T>::SIGN), mb = T(b & ~Fmt<T>::SIGN);
    if ( ma == mb ) return 0;
    const bool less = (ma < mb) != sa;
    return less ? -1 : 1;
}

// FMAX, FMIN: NaNs propagate; +0 is above -0.
template <typename T>
inline T
maxMin(T a, T b, bool is_max, FpCtx& c)
{
    T out;
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( procNaNs(a, b, c, &out) ) return out;
    if ( isZero(a) && isZero(b) ) {
        const bool s = is_max ? (sign(a) && sign(b)) : (sign(a) || sign(b));
        return zero<T>(s);
    }
    const int o = orderOf(a, b);
    T         r = is_max ? (o >= 0 ? a : b) : (o <= 0 ? a : b);
    return flushesOutput<T>(c) && isSub(r) ? (c.flags |= UFC, zero<T>(sign(r))) : r;
}

// FMAXNM, FMINNM: one quiet NaN gives the other operand.
template <typename T>
inline T
maxMinNum(T a, T b, bool is_max, FpCtx& c)
{
    T fa = flushIn(a, c), fb = flushIn(b, c);
    const bool qa = isNaN(fa) && !isSNaN(fa), qb = isNaN(fb) && !isSNaN(fb);
    if ( qa && !qb && !isNaN(fb) ) fa = is_max ? infinity<T>(true) : infinity<T>(false);
    else if ( qb && !qa && !isNaN(fa) ) fb = is_max ? infinity<T>(true) : infinity<T>(false);
    return maxMin(fa, fb, is_max, c);
}

// The NZCV of FCMP: (0,0,1,1) unordered, (1,0,0,0) less, (0,1,1,0) equal, (0,0,1,0) greater, in bits 31..28.
template <typename T>
inline uint64_t
compareFlags(T a, T b, bool signal_all, FpCtx& c)
{
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( isNaN(a) || isNaN(b) ) {
        if ( signal_all || isSNaN(a) || isSNaN(b) ) c.flags |= IOC;
        return 0x30000000ull;
    }
    const int o = orderOf(a, b);
    if ( o == 0 ) return 0x60000000ull;
    return o < 0 ? 0x80000000ull : 0x20000000ull;
}

// Element compares of AdvSIMD and SVE. FCMEQ does not signal on quiet NaNs; FCMGE, FCMGT do.
template <typename T>
inline bool
cmpEq(T a, T b, FpCtx& c)
{
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( isNaN(a) || isNaN(b) ) {
        if ( isSNaN(a) || isSNaN(b) ) c.flags |= IOC;
        return false;
    }
    return orderOf(a, b) == 0;
}

template <typename T>
inline bool
cmpGe(T a, T b, FpCtx& c)
{
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( isNaN(a) || isNaN(b) ) {
        c.flags |= IOC;
        return false;
    }
    return orderOf(a, b) >= 0;
}

template <typename T>
inline bool
cmpGt(T a, T b, FpCtx& c)
{
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( isNaN(a) || isNaN(b) ) {
        c.flags |= IOC;
        return false;
    }
    return orderOf(a, b) > 0;
}

// FRINT*: rounds to an integral value. exact raises IXC when the value changes (FRINTX and FRINTI-free forms).
template <typename T>
inline T
roundInt(T a, Rounding r, bool exact, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) return procNaN(a, c);
    sfBegin(c.rmode());
    softfloat_exceptionFlags = 0;
    T res;
    if constexpr ( sizeof(T) == 2 ) res = f16_roundToInt(f(a), sfRounding(r), exact).v;
    else if constexpr ( sizeof(T) == 4 ) res = f32_roundToInt(f(a), sfRounding(r), exact).v;
    else res = f64_roundToInt(f(a), sfRounding(r), exact).v;
    sfCollect(c);
    return res;
}

// FRINT32*, FRINT64*: round to an integral value that fits the signed width; out of range gives the most
// negative value of the width and IOC.
template <typename T>
inline T
roundIntWidth(T a, Rounding r, bool exact, unsigned width, FpCtx& c)
{
    a = flushIn(a, c);
    const T neg_limit = T(Fmt<T>::SIGN | (T(Fmt<T>::BIAS + width - 1) << Fmt<T>::FRAC));
    if ( isNaN(a) || isInf(a) ) {
        if ( isSNaN(a) || isNaN(a) || isInf(a) ) c.flags |= IOC;
        return neg_limit;
    }
    T res = roundInt(a, r, exact, c);
    if ( !isZero(res) ) {
        const int e = static_cast<int>((res & Fmt<T>::EXPM) >> Fmt<T>::FRAC) - Fmt<T>::BIAS;
        const bool too_big = sign(res) ? (e > static_cast<int>(width) - 1 || (e == static_cast<int>(width) - 1 && (res & Fmt<T>::FRACM)))
                                       : e >= static_cast<int>(width) - 1 + 0 && e > static_cast<int>(width) - 2;
        if ( too_big ) {
            c.flags &= ~IXC;
            c.flags |= IOC;
            return neg_limit;
        }
    }
    return res;
}

// FP to integer with saturation; fbits is the fixed-point fraction width. Sets IOC on NaN or out of range.
template <typename T, typename I>
inline I
toInt(T a, Rounding r, unsigned fbits, FpCtx& c)
{
    a = flushIn(a, c);
    const bool is_signed = std::is_signed<I>::value;
    if ( isNaN(a) ) {
        c.flags |= IOC;
        return I(0);
    }
    if ( fbits ) {
        // Multiply by 2^fbits exactly through the exponent, in double precision when the format is wider.
        sfBegin(Rounding::TIEEVEN);
        uint64_t d;
        if constexpr ( sizeof(T) == 2 ) d = f16_to_f64(f(a)).v;
        else if constexpr ( sizeof(T) == 4 ) d = f32_to_f64(f(a)).v;
        else d = a;
        softfloat_exceptionFlags = 0;
        float64_t scale;
        scale.v = static_cast<uint64_t>(1023 + fbits) << 52;
        const float64_t scaled = f64_mul(f(d), scale);
        softfloat_exceptionFlags = 0;
        softfloat_roundingMode = softfloat_round_near_even;
        I out;
        if constexpr ( is_signed && sizeof(I) == 8 ) out = f64_to_i64(scaled, sfRounding(r), true);
        else if constexpr ( is_signed ) out = static_cast<I>(f64_to_i32(scaled, sfRounding(r), true));
        else if constexpr ( sizeof(I) == 8 ) out = f64_to_ui64(scaled, sfRounding(r), true);
        else out = static_cast<I>(f64_to_ui32(scaled, sfRounding(r), true));
        if ( softfloat_exceptionFlags & softfloat_flag_invalid ) {
            c.flags |= IOC;
            const bool s = sign(a);
            if ( is_signed ) return s ? std::numeric_limits<I>::min() : std::numeric_limits<I>::max();
            return s ? I(0) : std::numeric_limits<I>::max();
        }
        if ( softfloat_exceptionFlags & softfloat_flag_inexact ) c.flags |= IXC;
        return out;
    }
    softfloat_exceptionFlags = 0;
    I out;
    if constexpr ( sizeof(T) == 2 ) {
        if constexpr ( is_signed && sizeof(I) == 8 ) out = f16_to_i64(f(a), sfRounding(r), true);
        else if constexpr ( is_signed ) out = static_cast<I>(f16_to_i32(f(a), sfRounding(r), true));
        else if constexpr ( sizeof(I) == 8 ) out = f16_to_ui64(f(a), sfRounding(r), true);
        else out = static_cast<I>(f16_to_ui32(f(a), sfRounding(r), true));
    }
    else if constexpr ( sizeof(T) == 4 ) {
        if constexpr ( is_signed && sizeof(I) == 8 ) out = f32_to_i64(f(a), sfRounding(r), true);
        else if constexpr ( is_signed ) out = static_cast<I>(f32_to_i32(f(a), sfRounding(r), true));
        else if constexpr ( sizeof(I) == 8 ) out = f32_to_ui64(f(a), sfRounding(r), true);
        else out = static_cast<I>(f32_to_ui32(f(a), sfRounding(r), true));
    }
    else {
        if constexpr ( is_signed && sizeof(I) == 8 ) out = f64_to_i64(f(a), sfRounding(r), true);
        else if constexpr ( is_signed ) out = static_cast<I>(f64_to_i32(f(a), sfRounding(r), true));
        else if constexpr ( sizeof(I) == 8 ) out = f64_to_ui64(f(a), sfRounding(r), true);
        else out = static_cast<I>(f64_to_ui32(f(a), sfRounding(r), true));
    }
    if ( softfloat_exceptionFlags & softfloat_flag_invalid ) {
        c.flags |= IOC;
        const bool s = sign(a);
        if ( is_signed ) return s ? std::numeric_limits<I>::min() : std::numeric_limits<I>::max();
        return s ? I(0) : std::numeric_limits<I>::max();
    }
    if ( softfloat_exceptionFlags & softfloat_flag_inexact ) c.flags |= IXC;
    return out;
}

// Integer to FP; fbits is the fixed-point fraction width.
template <typename T, typename I>
inline T
fromInt(I v, unsigned fbits, FpCtx& c)
{
    sfBegin(c.rmode());
    T r;
    const bool is_signed = std::is_signed<I>::value;
    if ( fbits ) {
        // The exact value v / 2^fbits rounded once: SoftFloat has no scaled conversion, so scale through a
        // format with enough bits. 64-bit integers go through a 128-bit-free path: divide after conversion only
        // when the conversion to double is exact; otherwise round in the division's sticky-free form.
        float64_t d;
        if constexpr ( is_signed ) d = sizeof(I) == 8 ? i64_to_f64(static_cast<int64_t>(v)) : i32_to_f64(static_cast<int32_t>(v));
        else d = sizeof(I) == 8 ? ui64_to_f64(static_cast<uint64_t>(v)) : ui32_to_f64(static_cast<uint32_t>(v));
        float64_t scale;
        scale.v = static_cast<uint64_t>(1023 - fbits) << 52;
        const float64_t scaled = f64_mul(d, scale);
        if constexpr ( sizeof(T) == 2 ) r = f64_to_f16(scaled).v;
        else if constexpr ( sizeof(T) == 4 ) r = f64_to_f32(scaled).v;
        else r = scaled.v;
        return sfFinish(r, c);
    }
    if constexpr ( sizeof(T) == 2 ) {
        if constexpr ( is_signed ) r = sizeof(I) == 8 ? i64_to_f16(static_cast<int64_t>(v)).v : i32_to_f16(static_cast<int32_t>(v)).v;
        else r = sizeof(I) == 8 ? ui64_to_f16(static_cast<uint64_t>(v)).v : ui32_to_f16(static_cast<uint32_t>(v)).v;
    }
    else if constexpr ( sizeof(T) == 4 ) {
        if constexpr ( is_signed ) r = sizeof(I) == 8 ? i64_to_f32(static_cast<int64_t>(v)).v : i32_to_f32(static_cast<int32_t>(v)).v;
        else r = sizeof(I) == 8 ? ui64_to_f32(static_cast<uint64_t>(v)).v : ui32_to_f32(static_cast<uint32_t>(v)).v;
    }
    else {
        if constexpr ( is_signed ) r = sizeof(I) == 8 ? i64_to_f64(static_cast<int64_t>(v)).v : i32_to_f64(static_cast<int32_t>(v)).v;
        else r = sizeof(I) == 8 ? ui64_to_f64(static_cast<uint64_t>(v)).v : ui32_to_f64(static_cast<uint32_t>(v)).v;
    }
    return sfFinish(r, c);
}

// FCVT between formats. A NaN keeps its sign and top fraction bits, quieted.
template <typename To, typename From>
inline To
convert(From a, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) {
        if ( isSNaN(a) ) c.flags |= IOC;
        if ( c.dn() ) return To(Fmt<To>::DEFNAN);
        const int shift = Fmt<To>::FRAC - Fmt<From>::FRAC;
        const uint64_t frac = static_cast<uint64_t>(a & Fmt<From>::FRACM);
        const uint64_t f2 = shift >= 0 ? (frac << shift) : (frac >> -shift);
        return To((sign(a) ? Fmt<To>::SIGN : To(0)) | Fmt<To>::EXPM | Fmt<To>::QUIET | To(f2));
    }
    sfBegin(c.rmode());
    To r;
    if constexpr ( sizeof(From) == 2 && sizeof(To) == 4 ) r = f16_to_f32(f(a)).v;
    else if constexpr ( sizeof(From) == 2 && sizeof(To) == 8 ) r = f16_to_f64(f(a)).v;
    else if constexpr ( sizeof(From) == 4 && sizeof(To) == 2 ) r = f32_to_f16(f(a)).v;
    else if constexpr ( sizeof(From) == 4 && sizeof(To) == 8 ) r = f32_to_f64(f(a)).v;
    else if constexpr ( sizeof(From) == 8 && sizeof(To) == 2 ) r = f64_to_f16(f(a)).v;
    else if constexpr ( sizeof(From) == 8 && sizeof(To) == 4 ) r = f64_to_f32(f(a)).v;
    else r = static_cast<To>(a);
    return sfFinish(r, c);
}

// FCVTXN: double to single, rounding to odd.
inline uint32_t
convertOdd(uint64_t a, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) return convert<uint32_t>(a, c);
    sfBegin(Rounding::ODD);
    return sfFinish<uint32_t>(f64_to_f32(f(a)).v, c);
}

// FRECPE: the estimate table of the Arm architecture (FPRecipEstimate).
inline uint32_t
recipEstimateTable(uint32_t a)
{
    a = a * 2 + 1;
    uint32_t b = (1u << 19) / a;
    return (b + 1) / 2;
}

template <typename T>
inline T
recipEstimate(T a, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) return procNaN(a, c);
    if ( isInf(a) ) return zero<T>(sign(a));
    if ( isZero(a) ) {
        c.flags |= DZC;
        return infinity<T>(sign(a));
    }
    using F = Fmt<T>;
    const bool s = sign(a);
    uint64_t   frac = (a & F::FRACM);
    int        exp  = static_cast<int>((a & F::EXPM) >> F::FRAC);
    // Very small inputs overflow to the largest normal or infinity per the rounding mode.
    if ( (sizeof(T) == 2 && a != 0 && abs(a) < T(1u << 8)) || (sizeof(T) == 4 && abs(a) < T(1u << 21)) || (sizeof(T) == 8 && abs(a) < (T(1ull) << 50)) ) {
        const Rounding r = c.rmode();
        const bool overflow_to_inf = r == Rounding::TIEEVEN || (r == Rounding::POSINF && !s) || (r == Rounding::NEGINF && s);
        c.flags |= OFC | IXC;
        return overflow_to_inf ? infinity<T>(s) : T((s ? F::SIGN : T(0)) | (F::EXPM - (T(1) << F::FRAC)) | F::FRACM);
    }
    if ( flushesOutput<T>(c) ) {
        const int limit = sizeof(T) == 2 ? 29 : (sizeof(T) == 4 ? 253 : 2045);
        if ( exp >= limit ) {
            c.flags |= UFC;
            return zero<T>(s);
        }
    }
    // Normalize to 8 fraction bits above the implied one.
    uint64_t scaled;
    if ( exp == 0 ) {
        // A denormal: shift until bit FRAC is set; exp becomes 0 or -1 per the architecture.
        uint64_t m = frac;
        int      e = 0;
        while ( !(m & (1ull << F::FRAC)) ) { m <<= 1; --e; }
        frac = m & F::FRACM;
        exp  = e + 1;
        if ( e < 0 ) exp = e + 1;
    }
    scaled = 0x100 | ((F::FRAC >= 8) ? (frac >> (F::FRAC - 8)) : (frac << (8 - F::FRAC)));
    const int result_exp = (2 * F::BIAS - 1 + (sizeof(T) == 2 ? 0 : 0)) - exp;
    uint32_t  est = recipEstimateTable(static_cast<uint32_t>(scaled));
    uint64_t  res_frac = static_cast<uint64_t>(est & 0xFF) << (F::FRAC >= 8 ? F::FRAC - 8 : 0);
    int       e2 = result_exp;
    if ( e2 == 0 ) res_frac = (1ull << (F::FRAC - 1)) | (res_frac >> 1);
    else if ( e2 == -1 ) { res_frac = (1ull << (F::FRAC - 2)) | (res_frac >> 2); e2 = 0; }
    return T((s ? F::SIGN : T(0)) | (T(e2) << F::FRAC) | T(res_frac));
}

inline uint32_t
rsqrtEstimateTable(uint32_t a)
{
    // a is 9 bits, 128..511; FPRSqrtEstimate's integer form.
    uint32_t b = 512;
    if ( a < 256 ) a = a * 2 + 1;
    else { a = (a >> 1) << 1; a = (a + 1) * 2; }
    while ( a * (b + 1) * (b + 1) < (1u << 28) ) ++b;
    return (b + 1) / 2;
}

template <typename T>
inline T
rsqrtEstimate(T a, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) return procNaN(a, c);
    if ( isZero(a) ) {
        c.flags |= DZC;
        return infinity<T>(sign(a));
    }
    if ( sign(a) ) {
        c.flags |= IOC;
        return T(Fmt<T>::DEFNAN);
    }
    if ( isInf(a) ) return zero<T>(false);
    using F = Fmt<T>;
    uint64_t frac = a & F::FRACM;
    int      exp  = static_cast<int>((a & F::EXPM) >> F::FRAC);
    if ( exp == 0 ) {
        uint64_t m = frac;
        int      e = 0;
        while ( !(m & (1ull << F::FRAC)) ) { m <<= 1; --e; }
        frac = m & F::FRACM;
        exp  = e + 1;
    }
    uint32_t scaled;
    const uint64_t top = (F::FRAC >= 8) ? (frac >> (F::FRAC - 8)) : (frac << (8 - F::FRAC));
    if ( (exp & 1) == 0 ) scaled = 0x100 | static_cast<uint32_t>(top);
    else scaled = 0x80 | static_cast<uint32_t>(top >> 1);
    const int      result_exp = (3 * F::BIAS - 1 - exp) / 2;
    const uint32_t est = rsqrtEstimateTable(scaled);
    return T((T(result_exp) << F::FRAC) | (T(est & 0xFF) << (F::FRAC - 8)));
}

// FRECPX: the exponent inverted, fraction zero.
template <typename T>
inline T
recipExponent(T a, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) return procNaN(a, c);
    using F = Fmt<T>;
    const T   s = T(a & F::SIGN);
    const T   e = T((a & F::EXPM) >> F::FRAC);
    const T   max = T(F::EXPM >> F::FRAC);
    return T(s | (T(e == 0 ? max - 1 : (~e) & max) << F::FRAC));
}

// FRECPS: 2 - a*b; FRSQRTS: (3 - a*b) / 2. Infinity times zero gives 2 or 1.5.
template <typename T>
inline T
recipStep(T a, T b, FpCtx& c)
{
    T out;
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( procNaNs(a, b, c, &out) ) return out;
    const T two = T(T(Fmt<T>::BIAS + 1) << Fmt<T>::FRAC);
    if ( (isInf(a) && isZero(b)) || (isZero(a) && isInf(b)) ) return two;
    return mulAdd<T>(two, neg(a), b, c);
}

template <typename T>
inline T
rsqrtStep(T a, T b, FpCtx& c)
{
    T out;
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( procNaNs(a, b, c, &out) ) return out;
    const T one_half = T((T(Fmt<T>::BIAS) << Fmt<T>::FRAC) | (T(1) << (Fmt<T>::FRAC - 1)));
    if ( (isInf(a) && isZero(b)) || (isZero(a) && isInf(b)) ) return one_half;
    const T three = T((T(Fmt<T>::BIAS + 1) << Fmt<T>::FRAC) | (T(1) << (Fmt<T>::FRAC - 1)));
    T       r = mulAdd<T>(three, neg(a), b, c);
    if ( isNaN(r) || isInf(r) || isZero(r) ) return r;
    // Divide by two through the exponent; the result of this fused step is not re-rounded.
    const T e = T((r & Fmt<T>::EXPM) >> Fmt<T>::FRAC);
    if ( e > 1 ) return T((r & ~Fmt<T>::EXPM) | (T(e - 1) << Fmt<T>::FRAC));
    FpCtx half(c.fpcr);
    const T two = T(T(Fmt<T>::BIAS + 1) << Fmt<T>::FRAC);
    return div<T>(r, two, half);
}

} // namespace A64Fp
} // namespace Vanadis
} // namespace SST

#endif
