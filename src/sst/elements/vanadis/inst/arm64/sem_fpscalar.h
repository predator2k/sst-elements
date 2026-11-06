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

// Micro-ops of the scalar floating-point instructions of A64. Every micro-op that depends on the FPCR has it as
// integer source 0. The format is given in op.a as the log2 of its bytes (1 = H, 2 = S, 3 = D).

#ifndef _H_VANADIS_ARM64_SEM_FPSCALAR
#define _H_VANADIS_ARM64_SEM_FPSCALAR

#include "inst/arm64/sem_fp.h"
#include "inst/arm64/sem_simd.h"

namespace SST {
namespace Vanadis {
namespace A64FpScalar {

using namespace A64Fp;
using A64Simd::byFormat;
using A64Simd::vget;
using A64Simd::writeLow;

template <typename T>
inline void
setScalar(VanadisArm64Inst& op, VanadisRegisterFile* rf, T v)
{
    writeLow(op, rf, 0, &v, sizeof(T));
}

inline FpCtx
ctxOf(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    return FpCtx(static_cast<uint32_t>(op.X(rf, 0)));
}

// Values of op.c for fpDp2: the opcode field of the encoding.
inline void
fpDp2(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c = ctxOf(op, rf);
    byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const T a = vget<T>(op.Vin(rf, 0), 0), b = vget<T>(op.Vin(rf, 1), 0);
        T       r = 0;
        switch ( op.c ) {
        case 0: r = mul(a, b, c); break;
        case 1: r = A64Fp::div(a, b, c); break;
        case 2: r = add(a, b, c); break;
        case 3: r = sub(a, b, c); break;
        case 4: r = maxMin(a, b, true, c); break;
        case 5: r = maxMin(a, b, false, c); break;
        case 6: r = maxMinNum(a, b, true, c); break;
        case 7: r = maxMinNum(a, b, false, c); break;
        default: r = neg(mul(a, b, c)); break;   // FNMUL
        }
        setScalar(op, rf, r);
    });
    op.fp_flags = c.flags;
}

// FMADD, FMSUB, FNMADD, FNMSUB. Sources: Vn, Vm, Va. c = (o1 << 1) | o0.
inline void
fpDp3(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c = ctxOf(op, rf);
    byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        T n = vget<T>(op.Vin(rf, 0), 0), m = vget<T>(op.Vin(rf, 1), 0), a = vget<T>(op.Vin(rf, 2), 0);
        const bool neg_a = op.c & 2;
        const bool neg_n = (op.c == 1) || (op.c == 2);
        if ( neg_a ) a = neg(a);
        if ( neg_n ) n = neg(n);
        setScalar(op, rf, mulAdd(a, n, m, c));
    });
    op.fp_flags = c.flags;
}

// The rounding of FRINT<x> by the low three opcode bits: N, P, M, Z, A, unused, X, I.
inline Rounding
frintRounding(uint32_t sel, const FpCtx& c)
{
    switch ( sel ) {
    case 0: return Rounding::TIEEVEN;
    case 1: return Rounding::POSINF;
    case 2: return Rounding::NEGINF;
    case 3: return Rounding::ZERO;
    case 4: return Rounding::TIEAWAY;
    default: return c.rmode();
    }
}

// FPCR.AHP alternative half precision: exponent 31 is an ordinary exponent and there are no NaNs or infinities.
template <typename To>
inline To
fromAltHalf(uint16_t a, FpCtx& c)
{
    if ( (a & Fmt<uint16_t>::EXPM) != Fmt<uint16_t>::EXPM ) {
        // Below exponent 31 the two formats are the same, and a conversion never flushes a half input.
        if ( isSub(a) ) c.flags |= 0;
        sfBegin(c.rmode());
        if constexpr ( sizeof(To) == 4 ) return f16_to_f32(f(a)).v;
        else return f16_to_f64(f(a)).v;
    }
    const To s = (a & Fmt<uint16_t>::SIGN) ? Fmt<To>::SIGN : To(0);
    const To e = To(31 - Fmt<uint16_t>::BIAS + Fmt<To>::BIAS) << Fmt<To>::FRAC;
    return To(s | e | (To(a & Fmt<uint16_t>::FRACM) << (Fmt<To>::FRAC - Fmt<uint16_t>::FRAC)));
}

template <typename From>
inline uint16_t
toAltHalf(From a, FpCtx& c)
{
    a = flushIn(a, c);
    const uint16_t s = sign(a) ? Fmt<uint16_t>::SIGN : uint16_t(0);
    if ( isNaN(a) ) {
        c.flags |= IOC;
        return s;
    }
    if ( isInf(a) ) {
        c.flags |= IOC;
        return uint16_t(s | 0x7FFF);
    }
    if ( isZero(a) ) return s;
    // A double holds every S value exactly, and halving a value at or above 2^15 is exact.
    float64_t d;
    sfBegin(c.rmode());
    if constexpr ( sizeof(From) == 4 ) d = f32_to_f64(f(a));
    else d.v = a;
    softfloat_exceptionFlags = 0;
    const uint64_t mag = d.v & ~Fmt<uint64_t>::SIGN;
    if ( mag >= (static_cast<uint64_t>(15 + Fmt<uint64_t>::BIAS) << Fmt<uint64_t>::FRAC) ) {
        float64_t half;
        half.v = d.v - (1ull << Fmt<uint64_t>::FRAC);
        const uint16_t r = f64_to_f16(half).v;
        if ( isInf(r) ) {
            c.flags |= IOC;
            return uint16_t(s | 0x7FFF);
        }
        sfCollect(c);
        return uint16_t(r + (1u << Fmt<uint16_t>::FRAC));
    }
    const uint16_t r = f64_to_f16(d).v;
    sfCollect(c);
    return r;
}

// FCVT with the conversion rules of FPUnpackCV and FPRoundCV: FZ16 does not apply to a half input or output.
template <typename To, typename From>
inline To
convertCv(From a, FpCtx& c)
{
    if constexpr ( sizeof(From) == 2 ) {
        if ( c.fpcr & FPCR_AHP ) return fromAltHalf<To>(a, c);
        FpCtx no16(c.fpcr & ~FPCR_FZ16);
        const To r = convert<To>(a, no16);
        c.flags |= no16.flags;
        return r;
    }
    else if constexpr ( sizeof(To) == 2 ) {
        if ( c.fpcr & FPCR_AHP ) return toAltHalf(a, c);
        FpCtx no16(c.fpcr & ~FPCR_FZ16);
        const To r = convert<To>(a, no16);
        c.flags |= no16.flags;
        return r;
    }
    else {
        return convert<To>(a, c);
    }
}

// The one-source operations. c is the opcode field; b is the ftype-independent destination format of FCVT.
inline void
fpDp1(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c = ctxOf(op, rf);
    const uint32_t code = op.c;
    byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const T a = vget<T>(op.Vin(rf, 0), 0);
        if ( code >= 4 && code <= 7 ) {
            if ( code == 6 ) {   // BFCVT: single to BFloat16, with BF16 rounding: round to odd is not used
                setScalar(op, rf, bfConvert(static_cast<uint32_t>(a), c));
            }
            else if ( code == 4 ) {
                setScalar(op, rf, convertCv<uint32_t>(a, c));
            }
            else if ( code == 5 ) {
                setScalar(op, rf, convertCv<uint64_t>(a, c));
            }
            else {
                setScalar(op, rf, convertCv<uint16_t>(a, c));
            }
            return;
        }
        T r = 0;
        switch ( code ) {
        case 0: r = a; break;
        case 1: r = A64Fp::abs(a); break;
        case 2: r = neg(a); break;
        case 3: r = A64Fp::sqrt(a, c); break;
        case 0x10: r = roundIntWidth(a, Rounding::ZERO, true, 32, c); break;
        case 0x11: r = roundIntWidth(a, c.rmode(), true, 32, c); break;
        case 0x12: r = roundIntWidth(a, Rounding::ZERO, true, 64, c); break;
        case 0x13: r = roundIntWidth(a, c.rmode(), true, 64, c); break;
        default: r = roundInt(a, frintRounding(code & 7, c), (code & 7) == 6, c); break;
        }
        setScalar(op, rf, r);
    });
    op.fp_flags = c.flags;
}

// FCMP, FCMPE. Sources: FPCR; Vn and Vm unless d & 2 (compare with zero). d & 1: signal on quiet NaNs.
inline void
fpCompare(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c = ctxOf(op, rf);
    byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const T a = vget<T>(op.Vin(rf, 0), 0);
        const T b = (op.d & 2) ? T(0) : vget<T>(op.Vin(rf, 1), 0);
        op.setX(rf, 0, compareFlags(a, b, op.d & 1, c));
    });
    op.fp_flags = c.flags;
}

// FCCMP, FCCMPE. Integer sources: FPCR, NZCV. c = cond, imm = NZCV value when the condition fails, d & 1 signals.
inline void
fpCondCompare(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c = ctxOf(op, rf);
    if ( !a64ConditionHolds(op.X(rf, 1), op.c) ) {
        op.setX(rf, 0, op.imm);
        op.fp_flags = 0;
        return;
    }
    byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        op.setX(rf, 0, compareFlags(vget<T>(op.Vin(rf, 0), 0), vget<T>(op.Vin(rf, 1), 0), op.d & 1, c));
    });
    op.fp_flags = c.flags;
}

// FCSEL. Integer source: NZCV. c = cond. The sources are not canonicalized.
inline void
fpCondSelect(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint8_t* s = op.Vin(rf, a64ConditionHolds(op.X(rf, 0), op.c) ? 0 : 1);
    writeLow(op, rf, 0, s, 1u << op.a);
}

// FMOV immediate. imm = the value, a = format.
inline void
fpImmediate(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    writeLow(op, rf, 0, &op.imm, 1u << op.a);
}

// FMOV from a vector register to a general register. a = bytes, c = byte offset in the register.
inline void
fpMoveToGp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t v = 0;
    std::memcpy(&v, op.Vin(rf, 0) + op.c, op.a);
    op.setX(rf, 0, v);
}

// FMOV from a general register. a = bytes. d & 1: write only D[1], keeping D[0] (source 0 is the old register).
inline void
fpMoveFromGp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint64_t v = op.X(rf, 0);
    if ( op.d & 1 ) {
        uint8_t r[16];
        std::memcpy(r, op.Vin(rf, 0), 8);
        std::memcpy(r + 8, &v, 8);
        writeLow(op, rf, 0, r, sizeof(r));
    }
    else {
        writeLow(op, rf, 0, &v, op.a);
    }
}

// FP to integer. Sources: FPCR, Vn. b = Rounding, c = fbits, d & 1: signed, d & 2: 64-bit destination.
inline void
fpToGp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c = ctxOf(op, rf);
    const Rounding r = static_cast<Rounding>(op.b);
    uint64_t out = 0;
    byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const T a = vget<T>(op.Vin(rf, 0), 0);
        switch ( op.d & 3 ) {
        case 0: out = toInt<T, uint32_t>(a, r, op.c, c); break;
        case 1: out = static_cast<uint32_t>(toInt<T, int32_t>(a, r, op.c, c)); break;
        case 2: out = toInt<T, uint64_t>(a, r, op.c, c); break;
        default: out = static_cast<uint64_t>(toInt<T, int64_t>(a, r, op.c, c)); break;
        }
    });
    op.setX(rf, 0, out);
    op.fp_flags = c.flags;
}

// Integer to FP. Sources: FPCR, Xn. c = fbits, d & 1: signed, d & 2: 64-bit source.
inline void
gpToFp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c = ctxOf(op, rf);
    const uint64_t v = op.X(rf, 1);
    byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        T out;
        switch ( op.d & 3 ) {
        case 0: out = fromInt<T, uint32_t>(static_cast<uint32_t>(v), op.c, c); break;
        case 1: out = fromInt<T, int32_t>(static_cast<int32_t>(v), op.c, c); break;
        case 2: out = fromInt<T, uint64_t>(v, op.c, c); break;
        default: out = fromInt<T, int64_t>(static_cast<int64_t>(v), op.c, c); break;
        }
        setScalar(op, rf, out);
    });
    op.fp_flags = c.flags;
}

// FJCVTZS: FPToFixedJS. Sources: FPCR, Dn. Destinations: Wd, NZCV with Z set when the conversion is exact.
inline void
fpJsConvert(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c = ctxOf(op, rf);
    const uint64_t raw = vget<uint64_t>(op.Vin(rf, 0), 0);
    const bool     neg_value = sign(raw);
    const int      exp = static_cast<int>((raw & Fmt<uint64_t>::EXPM) >> Fmt<uint64_t>::FRAC);
    uint64_t       frac = raw & Fmt<uint64_t>::FRACM;
    bool           z = true;
    uint32_t       result = 0;
    if ( isNaN(raw) ) {
        c.flags |= IOC;
        z = false;
    }
    else if ( isInf(raw) ) {
        c.flags |= IOC;
        z = false;
    }
    else if ( exp == 0 && frac != 0 && (c.fpcr & FPCR_FZ) ) {
        c.flags |= IDC;
        z = false;
    }
    else if ( exp == 0 && frac == 0 ) {
        if ( neg_value ) z = false;
    }
    else {
        const uint64_t mant = exp == 0 ? frac : (frac | (1ull << Fmt<uint64_t>::FRAC));
        const int      e = (exp == 0 ? 1 : exp) - Fmt<uint64_t>::BIAS - Fmt<uint64_t>::FRAC;
        unsigned __int128 mag = 0;
        bool              inexact = false;
        bool              big = false;
        if ( e >= 0 ) {
            if ( e >= 64 ) {
                mag = 0;
                big = true;
            }
            else {
                mag = static_cast<unsigned __int128>(mant) << e;
            }
        }
        else if ( e > -64 ) {
            mag = mant >> -e;
            inexact = (mant & ((1ull << -e) - 1)) != 0;
        }
        else {
            inexact = true;
        }
        const uint64_t low = static_cast<uint64_t>(mag);
        result = static_cast<uint32_t>(neg_value ? (0 - low) : low);
        const bool overflow = big || (mag >> 32) != 0 ||
                              (neg_value ? (low > 0x80000000ull) : (low > 0x7FFFFFFFull));
        if ( overflow ) {
            c.flags |= IOC;
            z = false;
        }
        else if ( inexact ) {
            c.flags |= IXC;
            z = false;
        }
    }
    op.setX(rf, 0, result);
    op.setX(rf, 1, z ? A64Flag::Z : 0);
    op.fp_flags = c.flags;
}

} // namespace A64FpScalar
} // namespace Vanadis
} // namespace SST

#endif
