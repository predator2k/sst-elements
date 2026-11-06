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

// Floating-point micro-ops of SVE. The FPCR is integer source 0; a = log2 of the element bytes (1, 2, 3), c = the
// operation code. The element operations are those of sem_fp.h, so vector and scalar forms follow the same rules.

#ifndef _H_VANADIS_ARM64_SEM_SVE_FP
#define _H_VANADIS_ARM64_SEM_SVE_FP

#include "inst/arm64/sem_simd_fp.h"
#include "inst/arm64/sem_sve.h"

extern "C" {
float64_t softfloat_roundPackToF64(bool, int_fast16_t, uint_fast64_t);
}

namespace SST {
namespace Vanadis {
namespace A64SveFp {

using namespace A64Fp;
using namespace A64Sve;
using A64Simd::maskOf;
using A64Simd::toElementInt;

// The fraction of 2^(i/32) for half precision and of 2^(i/64) for the wider formats, rounded to the fraction width.
constexpr uint16_t FEXPA_H[32] = {
    0x000, 0x016, 0x02d, 0x045, 0x05d, 0x075, 0x08e, 0x0a8,
    0x0c2, 0x0dc, 0x0f8, 0x114, 0x130, 0x14d, 0x16b, 0x189,
    0x1a8, 0x1c8, 0x1e8, 0x209, 0x22b, 0x24e, 0x271, 0x295,
    0x2ba, 0x2e0, 0x306, 0x32e, 0x356, 0x37f, 0x3a9, 0x3d4,
};
constexpr uint32_t FEXPA_S[64] = {
    0x000000, 0x0164d2, 0x02cd87, 0x043a29, 0x05aac3, 0x071f62, 0x08980f, 0x0a14d5,
    0x0b95c2, 0x0d1adf, 0x0ea43a, 0x1031dc, 0x11c3d3, 0x135a2b, 0x14f4f0, 0x16942d,
    0x1837f0, 0x19e046, 0x1b8d3a, 0x1d3eda, 0x1ef532, 0x20b051, 0x227043, 0x243516,
    0x25fed7, 0x27cd94, 0x29a15b, 0x2b7a3a, 0x2d583f, 0x2f3b79, 0x3123f6, 0x3311c4,
    0x3504f3, 0x36fd92, 0x38fbaf, 0x3aff5b, 0x3d08a4, 0x3f179a, 0x412c4d, 0x4346cd,
    0x45672a, 0x478d75, 0x49b9be, 0x4bec15, 0x4e248c, 0x506334, 0x52a81e, 0x54f35b,
    0x5744fd, 0x599d16, 0x5bfbb8, 0x5e60f5, 0x60ccdf, 0x633f89, 0x65b907, 0x68396a,
    0x6ac0c7, 0x6d4f30, 0x6fe4ba, 0x728177, 0x75257d, 0x77d0df, 0x7a83b3, 0x7d3e0c,
};
constexpr uint64_t FEXPA_D[64] = {
    0x0000000000000ull, 0x02c9a3e778061ull, 0x059b0d3158574ull, 0x0874518759bc8ull,
    0x0b5586cf9890full, 0x0e3ec32d3d1a2ull, 0x11301d0125b51ull, 0x1429aaea92de0ull,
    0x172b83c7d517bull, 0x1a35beb6fcb75ull, 0x1d4873168b9aaull, 0x2063b88628cd6ull,
    0x2387a6e756238ull, 0x26b4565e27cddull, 0x29e9df51fdee1ull, 0x2d285a6e4030bull,
    0x306fe0a31b715ull, 0x33c08b26416ffull, 0x371a7373aa9cbull, 0x3a7db34e59ff7ull,
    0x3dea64c123422ull, 0x4160a21f72e2aull, 0x44e086061892dull, 0x486a2b5c13cd0ull,
    0x4bfdad5362a27ull, 0x4f9b2769d2ca7ull, 0x5342b569d4f82ull, 0x56f4736b527daull,
    0x5ab07dd485429ull, 0x5e76f15ad2148ull, 0x6247eb03a5585ull, 0x6623882552225ull,
    0x6a09e667f3bcdull, 0x6dfb23c651a2full, 0x71f75e8ec5f74ull, 0x75feb564267c9ull,
    0x7a11473eb0187ull, 0x7e2f336cf4e62ull, 0x82589994cce13ull, 0x868d99b4492edull,
    0x8ace5422aa0dbull, 0x8f1ae99157736ull, 0x93737b0cdc5e5ull, 0x97d829fde4e50ull,
    0x9c49182a3f090ull, 0xa0c667b5de565ull, 0xa5503b23e255dull, 0xa9e6b5579fdbfull,
    0xae89f995ad3adull, 0xb33a2b84f15fbull, 0xb7f76f2fb5e47ull, 0xbcc1e904bc1d2ull,
    0xc199bdd85529cull, 0xc67f12e57d14bull, 0xcb720dcef9069ull, 0xd072d4a07897cull,
    0xd5818dcfba487ull, 0xda9e603db3285ull, 0xdfc97337b9b5full, 0xe502ee78b3ff6ull,
    0xea4afa2a490daull, 0xefa1bee615a27ull, 0xf50765b6e4540ull, 0xfa7c1819e90d8ull,
};

// The series coefficients of FTMAD: sine terms, then cosine terms.
constexpr uint16_t FTMAD_H[16] = { 0x3c00, 0xb155, 0x2030, 0, 0, 0, 0, 0, 0x3c00, 0xb800, 0x293a, 0, 0, 0, 0, 0 };
constexpr uint32_t FTMAD_S[16] = { 0x3f800000, 0xbe2aaaab, 0x3c088886, 0xb95008b9, 0x36369d6d, 0, 0, 0,
    0x3f800000, 0xbf000000, 0x3d2aaaa6, 0xbab60705, 0x37cd37cc, 0, 0, 0 };
constexpr uint64_t FTMAD_D[16] = { 0x3ff0000000000000ull, 0xbfc5555555555543ull, 0x3f8111111110f30cull, 0xbf2a01a019b92fc6ull,
    0x3ec71de351f3d22bull, 0xbe5ae5e2b60f7b91ull, 0x3de5d8408868552full, 0,
    0x3ff0000000000000ull, 0xbfe0000000000000ull, 0x3fa5555555555536ull, 0xbf56c16c16c13a0bull,
    0x3efa01a019b1e8d8ull, 0xbe927e4f7282f468ull, 0x3e21ee96d2641b13ull, 0xbda8f76380fbb401ull };

inline uint64_t
ftmadCoefficient(unsigned bytes, unsigned idx)
{
    return bytes == 2 ? FTMAD_H[idx] : bytes == 4 ? FTMAD_S[idx] : FTMAD_D[idx];
}

// FSCALE: value * 2^n, rounded once.
template <typename T>
inline T
fscale(T a, int64_t n, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) return procNaN(a, c);
    if ( isZero(a) || isInf(a) ) return a;
    const int64_t limit = 8192;
    if ( n > limit ) n = limit;
    if ( n < -limit ) n = -limit;
    sfBegin(c.rmode());
    if constexpr ( sizeof(T) == 8 ) {
        // The significand normalized to bit 62, with the exponent as softfloat_roundPackToF64 wants it.
        uint64_t sig = a & Fmt<T>::FRACM;
        int64_t  e = static_cast<int64_t>((a & Fmt<T>::EXPM) >> Fmt<T>::FRAC);
        if ( e == 0 ) e = 1;
        else sig |= 1ull << Fmt<T>::FRAC;
        const int lead = __builtin_clzll(sig);
        sig <<= (lead - 1);
        // sig has its top bit at 62: the value is sig * 2^(e - 1023 - 52 - (lead - 1)).
        e = e - (lead - 1) + 10 + n - 1;
        if ( e > 4000 ) e = 4000;
        if ( e < -4000 ) e = -4000;
        const T r = softfloat_roundPackToF64(sign(a), static_cast<int_fast16_t>(e), sig).v;
        return sfFinish(r, c);
    }
    else {
        // A double holds the narrower format exactly, and a scale in this range stays exact in it.
        float64_t d;
        if constexpr ( sizeof(T) == 2 ) d = f16_to_f64(f(a));
        else d = f32_to_f64(f(a));
        const int64_t e = static_cast<int64_t>((d.v & Fmt<uint64_t>::EXPM) >> Fmt<uint64_t>::FRAC) + n;
        softfloat_exceptionFlags = 0;
        T r;
        if ( e >= 2047 ) {
            d.v = (d.v & Fmt<uint64_t>::SIGN) | (static_cast<uint64_t>(2046) << Fmt<uint64_t>::FRAC);
        }
        else if ( e <= 0 ) {
            d.v = (d.v & Fmt<uint64_t>::SIGN) | (static_cast<uint64_t>(1) << Fmt<uint64_t>::FRAC);
        }
        else {
            d.v = (d.v & ~Fmt<uint64_t>::EXPM) | (static_cast<uint64_t>(e) << Fmt<uint64_t>::FRAC);
        }
        if constexpr ( sizeof(T) == 2 ) r = f64_to_f16(d).v;
        else r = f64_to_f32(d).v;
        return sfFinish(r, c);
    }
}

// Predicated two-operand operations. c = bits 19..16: 0 FADD, 1 FSUB, 2 FMUL, 3 FSUBR, 4 FMAXNM, 5 FMINNM, 6 FMAX, 7 FMIN,
// 8 FABD, 9 FSCALE, 10 FMULX, 12 FDIVR, 13 FDIV. d & 1 = the second operand is imm. Sources Pg, Zdn, Zm.
template <typename T>
inline T
predFpElem(uint32_t code, T x, T y, FpCtx& c)
{
    switch ( code ) {
    case 0: return add(x, y, c);
    case 1: return sub(x, y, c);
    case 2: return mul(x, y, c);
    case 3: return sub(y, x, c);
    case 4: return maxMinNum(x, y, true, c);
    case 5: return maxMinNum(x, y, false, c);
    case 6: return maxMin(x, y, true, c);
    case 7: return maxMin(x, y, false, c);
    case 8: return abd(x, y, c);
    case 9: {
        using S = std::make_signed_t<T>;
        return fscale(x, static_cast<int64_t>(static_cast<S>(y)), c);
    }
    case 10: return mulx(x, y, c);
    case 12: return A64Fp::div(y, x, c);
    default: return A64Fp::div(x, y, c);
    }
}

inline void
predFpBin(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            T v = vget<T>(op.Vin(rf, 1), e);
            if ( pbit(op.Vin(rf, 0), e * sizeof(T)) ) {
                const T y = (op.d & 1) ? static_cast<T>(op.imm) : vget<T>(op.Vin(rf, 2), e);
                v = predFpElem<T>(op.c, v, y, c);
            }
            vset<T>(r, e, v);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FTMAD. Sources Zdn, Zm. imm = the term number.
inline void
ftmad(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            const T x = vget<T>(op.Vin(rf, 0), e), y = vget<T>(op.Vin(rf, 1), e);
            const unsigned idx = static_cast<unsigned>(op.imm) + (sign(y) ? 8 : 0);
            vset<T>(r, e, mulAdd(static_cast<T>(ftmadCoefficient(sizeof(T), idx)), x, A64Fp::abs(y), c));
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// Unpredicated: c: 0 FADD, 1 FSUB, 2 FMUL, 3 FTSMUL, 6 FRECPS, 7 FRSQRTS. Sources Zn, Zm.
inline void
unpredFp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            const T x = vget<T>(op.Vin(rf, 0), e), y = vget<T>(op.Vin(rf, 1), e);
            T v;
            switch ( op.c ) {
            case 0: v = add(x, y, c); break;
            case 1: v = sub(x, y, c); break;
            case 2: v = mul(x, y, c); break;
            case 3:
                v = mul(x, x, c);
                if ( !isNaN(v) ) v = T((v & ~Fmt<T>::SIGN) | ((y & 1) ? Fmt<T>::SIGN : T(0)));
                break;
            case 6: v = recipStep(x, y, c); break;
            default: v = rsqrtStep(x, y, c); break;
            }
            vset<T>(r, e, v);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FTSSEL, FEXPA: no FPCR. c: 0 FTSSEL (sources Zn, Zm), 1 FEXPA (source Zn).
inline void
trigMisc(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            const T x = vget<T>(op.Vin(rf, 0), e);
            T v;
            if ( op.c == 0 ) {
                const T y = vget<T>(op.Vin(rf, 1), e);
                if ( y & 1 ) v = T((T(Fmt<T>::BIAS) << Fmt<T>::FRAC) | ((y & 2) ? Fmt<T>::SIGN : T(0)));
                else if ( y & 2 ) v = T(x ^ Fmt<T>::SIGN);
                else v = x;
            }
            else if constexpr ( sizeof(T) == 2 ) {
                v = T(((x >> 5) & 0x1F) << 10 | FEXPA_H[x & 31]);
            }
            else if constexpr ( sizeof(T) == 4 ) {
                v = T(((x >> 6) & 0xFF) << 23 | FEXPA_S[x & 63]);
            }
            else {
                v = T((static_cast<uint64_t>((x >> 6) & 0x7FF) << 52) | FEXPA_D[x & 63]);
            }
            vset<T>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// FMLA (0), FMLS (1), FNMLA (2), FNMLS (3): sources Pg, Zda, Zn, Zm. FMAD (4), FMSB (5), FNMAD (6), FNMSB (7): sources
// Pg, Zdn, Zm, Za.
inline void
predFpMulAdd(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            const T d = vget<T>(op.Vin(rf, 1), e);
            T v = d;
            if ( pbit(op.Vin(rf, 0), e * sizeof(T)) ) {
                const T p = vget<T>(op.Vin(rf, 2), e), q = vget<T>(op.Vin(rf, 3), e);
                T acc = op.c < 4 ? d : q;
                T x = op.c < 4 ? p : d;
                T y = op.c < 4 ? q : p;
                const uint32_t kind = op.c & 3;
                if ( kind == 1 || kind == 2 ) x = neg(x);
                if ( kind >= 2 ) acc = neg(acc);
                v = mulAdd(acc, x, y, c);
            }
            vset<T>(r, e, v);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FMLA, FMLS (c 0, 1) and FMUL (c 2) by element. imm = index in each 128-bit segment. Sources Zn, Zm, Zda.
inline void
fpIndexed(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const unsigned per_seg = 16 / sizeof(T);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            const unsigned seg = e - e % per_seg;
            const T x = vget<T>(op.Vin(rf, 0), e);
            const T y = vget<T>(op.Vin(rf, 1), seg + static_cast<unsigned>(op.imm));
            T v;
            if ( op.c == 2 ) v = mul(x, y, c);
            else v = mulAdd(vget<T>(op.Vin(rf, 2), e), op.c == 1 ? neg(x) : x, y, c);
            vset<T>(r, e, v);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

template <typename T>
inline bool
cmpUnordered(T a, T b, FpCtx& c)
{
    a = flushIn(a, c);
    b = flushIn(b, c);
    if ( isSNaN(a) || isSNaN(b) ) c.flags |= IOC;
    return isNaN(a) || isNaN(b);
}

// FP compares. c: 0 GE, 1 GT, 2 EQ, 3 NE, 4 UO, 5 AC GE, 6 AC GT, 7 LT, 8 LE. d = compare with zero. Sources Pg, Zn, Zm.
inline void
fpCompareZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    std::memset(r, 0, vl / 8);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            if ( !pbit(op.Vin(rf, 0), e * sizeof(T)) ) continue;
            const T x = vget<T>(op.Vin(rf, 1), e);
            const T y = op.d ? T(0) : vget<T>(op.Vin(rf, 2), e);
            bool v = false;
            switch ( op.c ) {
            case 0: v = cmpGe(x, y, c); break;
            case 1: v = cmpGt(x, y, c); break;
            case 2: v = cmpEq(x, y, c); break;
            case 3: v = !cmpEq(x, y, c); break;
            case 4: v = cmpUnordered(x, y, c); break;
            case 5: v = cmpGe(A64Fp::abs(x), A64Fp::abs(y), c); break;
            case 6: v = cmpGt(A64Fp::abs(x), A64Fp::abs(y), c); break;
            case 7: v = cmpGt(y, x, c); break;
            default: v = cmpGe(y, x, c); break;
            }
            pset(r, e * sizeof(T), v);
        }
    });
    writeP(op, rf, r);
    op.fp_flags = c.flags;
}

// Unary, merging. c: FRINT by low three bits 0..7 (N, P, M, Z, A, -, X, I); 8 FRECPX, 9 FSQRT, 10 FRINT32Z, 11 FRINT32X,
// 12 FRINT64Z, 13 FRINT64X, 14 FLOGB. Sources Pg, Zd, Zn.
inline void
predFpUnary(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        using S = std::make_signed_t<T>;
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            T v = vget<T>(op.Vin(rf, 1), e);
            if ( pbit(op.Vin(rf, 0), e * sizeof(T)) ) {
                const T x = vget<T>(op.Vin(rf, 2), e);
                static constexpr Rounding table[8] = { Rounding::TIEEVEN, Rounding::POSINF, Rounding::NEGINF, Rounding::ZERO, Rounding::TIEAWAY, Rounding::TIEEVEN, Rounding::TIEEVEN, Rounding::TIEEVEN };
                switch ( op.c ) {
                case 8: v = recipExponent(x, c); break;
                case 9: v = A64Fp::sqrt(x, c); break;
                case 10: v = roundIntWidth(x, Rounding::ZERO, true, 32, c); break;
                case 11: v = roundIntWidth(x, c.rmode(), true, 32, c); break;
                case 12: v = roundIntWidth(x, Rounding::ZERO, true, 64, c); break;
                case 13: v = roundIntWidth(x, c.rmode(), true, 64, c); break;
                case 14: {
                    const T y = flushIn(x, c);
                    if ( isNaN(y) || isZero(y) ) {
                        c.flags |= IOC;
                        v = Fmt<T>::SIGN;
                    }
                    else if ( isInf(y) ) {
                        v = T(~Fmt<T>::SIGN);
                    }
                    else {
                        const int be = static_cast<int>((y & Fmt<T>::EXPM) >> Fmt<T>::FRAC);
                        int exponent;
                        if ( be ) {
                            exponent = be - Fmt<T>::BIAS;
                        }
                        else {
                            const uint64_t frac = y & Fmt<T>::FRACM;
                            exponent = (1 - Fmt<T>::BIAS) - (Fmt<T>::FRAC - (63 - __builtin_clzll(frac)));
                        }
                        v = static_cast<T>(static_cast<S>(exponent));
                    }
                } break;
                case 6: v = roundInt(x, c.rmode(), true, c); break;
                case 7: v = roundInt(x, c.rmode(), false, c); break;
                default: v = roundInt(x, table[op.c & 7], false, c); break;
                }
            }
            vset<T>(r, e, v);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FCVT and FCVTX. Sources Pg, Zd, Zn; a = log2 of the container bytes. c = (log2 source << 4) | log2 destination;
// d & 1 = FCVTX.
inline void
fpConvertZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)) & ~FPCR_AHP);
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    std::memcpy(r, op.Vin(rf, 1), vl);
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        if ( !pbit(op.Vin(rf, 0), e * ebytes) ) continue;
        const uint8_t* s = op.Vin(rf, 2) + e * ebytes;
        uint8_t*       d = r + e * ebytes;
        uint64_t       raw = 0;
        std::memcpy(&raw, s, ebytes);
        uint64_t out = 0;
        unsigned out_bytes = 0;
        if ( op.d & 1 ) {
            out = convertOdd(raw, c);
            out_bytes = 4;
        }
        else {
            switch ( op.c ) {
            case 0x21: out = A64FpScalar::convertCv<uint16_t>(static_cast<uint32_t>(raw), c); out_bytes = 2; break;
            case 0x12: out = A64FpScalar::convertCv<uint32_t>(static_cast<uint16_t>(raw), c); out_bytes = 4; break;
            case 0x31: out = A64FpScalar::convertCv<uint16_t>(raw, c); out_bytes = 2; break;
            case 0x13: out = A64FpScalar::convertCv<uint64_t>(static_cast<uint16_t>(raw), c); out_bytes = 8; break;
            case 0x32: out = convert<uint32_t>(raw, c); out_bytes = 4; break;
            default: out = convert<uint64_t>(static_cast<uint32_t>(raw), c); out_bytes = 8; break;
            }
        }
        std::memset(d, 0, ebytes);
        std::memcpy(d, &out, out_bytes);
    }
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// BFCVT: Sources Pg, Zd, Zn.
inline void
bfConvertZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    std::memcpy(r, op.Vin(rf, 1), vl);
    for ( unsigned e = 0; e < vl / 4; ++e ) {
        if ( pbit(op.Vin(rf, 0), e * 4) ) vset<uint32_t>(r, e, bfConvert(vget<uint32_t>(op.Vin(rf, 2), e), c));
    }
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FCVTZS, FCVTZU: a = log2 container; c = (log2 FP source << 4) | log2 integer destination; d = signed.
inline void
fpToIntZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    std::memcpy(r, op.Vin(rf, 1), vl);
    const bool is_signed = op.d;
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        if ( !pbit(op.Vin(rf, 0), e * ebytes) ) continue;
        uint64_t raw = 0;
        std::memcpy(&raw, op.Vin(rf, 2) + e * ebytes, ebytes);
        uint64_t out = 0;
        unsigned out_bytes = 8;
        switch ( op.c ) {
        case 0x11: out = toElementInt(static_cast<uint16_t>(raw), Rounding::ZERO, 0, is_signed, c); out_bytes = 2; break;
        case 0x12:
            out = is_signed ? static_cast<uint32_t>(toInt<uint16_t, int32_t>(static_cast<uint16_t>(raw), Rounding::ZERO, 0, c))
                            : toInt<uint16_t, uint32_t>(static_cast<uint16_t>(raw), Rounding::ZERO, 0, c);
            out_bytes = 4;
            break;
        case 0x13:
            out = is_signed ? static_cast<uint64_t>(toInt<uint16_t, int64_t>(static_cast<uint16_t>(raw), Rounding::ZERO, 0, c))
                            : toInt<uint16_t, uint64_t>(static_cast<uint16_t>(raw), Rounding::ZERO, 0, c);
            break;
        case 0x22:
            out = is_signed ? static_cast<uint32_t>(toInt<uint32_t, int32_t>(static_cast<uint32_t>(raw), Rounding::ZERO, 0, c))
                            : toInt<uint32_t, uint32_t>(static_cast<uint32_t>(raw), Rounding::ZERO, 0, c);
            out_bytes = 4;
            break;
        case 0x23:
            out = is_signed ? static_cast<uint64_t>(toInt<uint32_t, int64_t>(static_cast<uint32_t>(raw), Rounding::ZERO, 0, c))
                            : toInt<uint32_t, uint64_t>(static_cast<uint32_t>(raw), Rounding::ZERO, 0, c);
            break;
        case 0x32:
            // A signed 32-bit result is sign-extended in its 64-bit container.
            out = is_signed ? static_cast<uint64_t>(static_cast<int64_t>(toInt<uint64_t, int32_t>(raw, Rounding::ZERO, 0, c)))
                            : toInt<uint64_t, uint32_t>(raw, Rounding::ZERO, 0, c);
            break;
        default:
            out = is_signed ? static_cast<uint64_t>(toInt<uint64_t, int64_t>(raw, Rounding::ZERO, 0, c))
                            : toInt<uint64_t, uint64_t>(raw, Rounding::ZERO, 0, c);
            break;
        }
        std::memset(r + e * ebytes, 0, ebytes);
        std::memcpy(r + e * ebytes, &out, out_bytes);
    }
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// SCVTF, UCVTF: a = log2 container; c = (log2 integer source << 4) | log2 FP destination; d = signed.
inline void
intToFpZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    std::memcpy(r, op.Vin(rf, 1), vl);
    const bool s = op.d;
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        if ( !pbit(op.Vin(rf, 0), e * ebytes) ) continue;
        uint64_t raw = 0;
        std::memcpy(&raw, op.Vin(rf, 2) + e * ebytes, ebytes);
        uint64_t out = 0;
        unsigned out_bytes = 8;
        switch ( op.c ) {
        case 0x11: out = s ? fromInt<uint16_t, int32_t>(static_cast<int16_t>(raw), 0, c) : fromInt<uint16_t, uint32_t>(static_cast<uint16_t>(raw), 0, c); out_bytes = 2; break;
        case 0x21: out = s ? fromInt<uint16_t, int32_t>(static_cast<int32_t>(raw), 0, c) : fromInt<uint16_t, uint32_t>(static_cast<uint32_t>(raw), 0, c); out_bytes = 2; break;
        case 0x31: out = s ? fromInt<uint16_t, int64_t>(static_cast<int64_t>(raw), 0, c) : fromInt<uint16_t, uint64_t>(raw, 0, c); out_bytes = 2; break;
        case 0x22: out = s ? fromInt<uint32_t, int32_t>(static_cast<int32_t>(raw), 0, c) : fromInt<uint32_t, uint32_t>(static_cast<uint32_t>(raw), 0, c); out_bytes = 4; break;
        case 0x32: out = s ? fromInt<uint32_t, int64_t>(static_cast<int64_t>(raw), 0, c) : fromInt<uint32_t, uint64_t>(raw, 0, c); out_bytes = 4; break;
        case 0x23: out = s ? fromInt<uint64_t, int32_t>(static_cast<int32_t>(raw), 0, c) : fromInt<uint64_t, uint32_t>(static_cast<uint32_t>(raw), 0, c); break;
        default: out = s ? fromInt<uint64_t, int64_t>(static_cast<int64_t>(raw), 0, c) : fromInt<uint64_t, uint64_t>(raw, 0, c); break;
        }
        std::memset(r + e * ebytes, 0, ebytes);
        std::memcpy(r + e * ebytes, &out, out_bytes);
    }
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FRECPE, FRSQRTE: c: 0, 1. Source Zn.
inline void
estimateZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            const T x = vget<T>(op.Vin(rf, 0), e);
            vset<T>(r, e, op.c ? rsqrtEstimate(x, c) : recipEstimate(x, c));
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// The reductions by halves. c: 0 FADDV, 4 FMAXNMV, 5 FMINNMV, 6 FMAXV, 7 FMINV. Sources Pg, Zn.
template <typename T>
inline T
fpReduceHalves(T* v, unsigned n, uint32_t code, FpCtx& c)
{
    if ( n == 1 ) return v[0];
    unsigned half = 1;
    while ( half * 2 < n ) half *= 2;
    const T x = fpReduceHalves<T>(v, half, code, c);
    const T y = fpReduceHalves<T>(v + half, n - half, code, c);
    switch ( code ) {
    case 0: return add(x, y, c);
    case 4: return maxMinNum(x, y, true, c);
    case 5: return maxMinNum(x, y, false, c);
    case 6: return maxMin(x, y, true, c);
    default: return maxMin(x, y, false, c);
    }
}

inline void
fpReduceZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        T v[MAX_VL_BYTES / sizeof(T)];
        T identity;
        switch ( op.c ) {
        case 0: identity = T(0); break;
        case 4: case 5: identity = T(Fmt<T>::DEFNAN); break;
        case 6: identity = infinity<T>(true); break;
        default: identity = infinity<T>(false); break;
        }
        const unsigned n = vl / sizeof(T);
        for ( unsigned e = 0; e < n; ++e ) v[e] = pbit(op.Vin(rf, 0), e * sizeof(T)) ? vget<T>(op.Vin(rf, 1), e) : identity;
        const T out = fpReduceHalves<T>(v, n, op.c, c);
        writeLow(op, rf, 0, &out, sizeof(T));
    });
    op.fp_flags = c.flags;
}

// FADDA: sources Pg, Vdn, Zm.
inline void
fadda(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        T sum = vget<T>(op.Vin(rf, 1), 0);
        for ( unsigned e = 0; e < vl / sizeof(T); ++e ) {
            if ( pbit(op.Vin(rf, 0), e * sizeof(T)) ) sum = add(sum, vget<T>(op.Vin(rf, 2), e), c);
        }
        writeLow(op, rf, 0, &sum, sizeof(T));
    });
    op.fp_flags = c.flags;
}

// FCADD: d = rot (0: 90, 1: 270). Sources Pg, Zdn, Zm.
inline void
fcaddZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(T) / 2; ++e ) {
            const T a0 = vget<T>(op.Vin(rf, 1), 2 * e), a1 = vget<T>(op.Vin(rf, 1), 2 * e + 1);
            T m0 = vget<T>(op.Vin(rf, 2), 2 * e), m1 = vget<T>(op.Vin(rf, 2), 2 * e + 1);
            if ( op.d == 0 ) m1 = neg(m1);
            else m0 = neg(m0);
            vset<T>(r, 2 * e, pbit(op.Vin(rf, 0), 2 * e * sizeof(T)) ? add(a0, m1, c) : a0);
            vset<T>(r, 2 * e + 1, pbit(op.Vin(rf, 0), (2 * e + 1) * sizeof(T)) ? add(a1, m0, c) : a1);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FCMLA: d = rot. Sources Pg, Zda, Zn, Zm; with c & 1 the element pair imm of each 128-bit segment and no predicate
// (the first source is Zda).
inline void
fcmlaZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    const bool     indexed = op.c & 1;
    const unsigned base = indexed ? 0 : 1;
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const uint8_t* zd = op.Vin(rf, base);
        const uint8_t* zn = op.Vin(rf, base + 1);
        const uint8_t* zm = op.Vin(rf, base + 2);
        const unsigned pairs_per_seg = 8 / sizeof(T);
        for ( unsigned e = 0; e < vl / sizeof(T) / 2; ++e ) {
            const unsigned seg = e - e % pairs_per_seg;
            const unsigned me = indexed ? seg + static_cast<unsigned>(op.imm) : e;
            const T m_r = vget<T>(zm, 2 * me), m_i = vget<T>(zm, 2 * me + 1);
            const T n_r = vget<T>(zn, 2 * e), n_i = vget<T>(zn, 2 * e + 1);
            T e1, e2, e3, e4;
            switch ( op.d ) {
            case 0: e1 = m_r; e2 = n_r; e3 = m_i; e4 = n_r; break;
            case 1: e1 = neg(m_i); e2 = n_i; e3 = m_r; e4 = n_i; break;
            case 2: e1 = neg(m_r); e2 = n_r; e3 = neg(m_i); e4 = n_r; break;
            default: e1 = m_i; e2 = n_i; e3 = neg(m_r); e4 = n_i; break;
            }
            T a0 = vget<T>(zd, 2 * e), a1 = vget<T>(zd, 2 * e + 1);
            if ( indexed || pbit(op.Vin(rf, 0), 2 * e * sizeof(T)) ) a0 = mulAdd(a0, e2, e1, c);
            if ( indexed || pbit(op.Vin(rf, 0), (2 * e + 1) * sizeof(T)) ) a1 = mulAdd(a1, e4, e3, c);
            vset<T>(r, 2 * e, a0);
            vset<T>(r, 2 * e + 1, a1);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FMLALB, FMLALT, FMLSLB, FMLSLT: a unused. c & 1 = subtract, d & 1 = top, d & 2 = indexed by imm in each segment. Sources
// Zda, Zn, Zm.
inline void
fmlalZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    const unsigned top = (op.d & 1) ? 1 : 0;
    for ( unsigned e = 0; e < vl / 4; ++e ) {
        const unsigned seg = (e - e % 4) * 2;
        uint16_t x = vget<uint16_t>(op.Vin(rf, 1), 2 * e + top);
        const uint16_t y = vget<uint16_t>(op.Vin(rf, 2), (op.d & 2) ? seg + static_cast<unsigned>(op.imm) : 2 * e + top);
        if ( op.c & 1 ) x = neg(x);
        const uint32_t sx = A64Simd::halfAsSingle(x, c), sy = A64Simd::halfAsSingle(y, c);
        vset<uint32_t>(r, e, mulAdd(vget<uint32_t>(op.Vin(rf, 0), e), sx, sy, c));
    }
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// BFMLALB, BFMLALT: d & 1 = top, d & 2 = indexed by imm in each segment. Sources Zda, Zn, Zm.
inline void
bfmlalZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    const unsigned top = (op.d & 1) ? 1 : 0;
    for ( unsigned e = 0; e < vl / 4; ++e ) {
        const unsigned seg = (e - e % 4) * 2;
        const uint16_t x = vget<uint16_t>(op.Vin(rf, 1), 2 * e + top);
        const uint16_t y = vget<uint16_t>(op.Vin(rf, 2), (op.d & 2) ? seg + static_cast<unsigned>(op.imm) : 2 * e + top);
        vset<uint32_t>(r, e, mulAdd(vget<uint32_t>(op.Vin(rf, 0), e), static_cast<uint32_t>(x) << 16, static_cast<uint32_t>(y) << 16, c));
    }
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// BFDOT: d & 1 = indexed by imm in each segment. Sources Zda, Zn, Zm.
inline void
bfdotZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    for ( unsigned g = 0; g < vl / 4; ++g ) {
        const unsigned mg = (op.d & 1) ? (g - g % 4) + static_cast<unsigned>(op.imm) : g;
        const uint32_t prod = A64Simd::bfAdd(A64Simd::bfMul(vget<uint16_t>(op.Vin(rf, 1), 2 * g), vget<uint16_t>(op.Vin(rf, 2), 2 * mg)),
            A64Simd::bfMul(vget<uint16_t>(op.Vin(rf, 1), 2 * g + 1), vget<uint16_t>(op.Vin(rf, 2), 2 * mg + 1)));
        vset<uint32_t>(r, g, A64Simd::bfAdd(vget<uint32_t>(op.Vin(rf, 0), g), prod));
    }
    writeZ(op, rf, r);
}

// BFMMLA over 128-bit segments. Sources Zda, Zn, Zm.
inline void
bfmmlaZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    for ( unsigned q = 0; q + 16 <= vl; q += 16 ) {
        const uint8_t* n = op.Vin(rf, 1) + q;
        const uint8_t* m = op.Vin(rf, 2) + q;
        for ( unsigned i = 0; i < 2; ++i ) {
            for ( unsigned j = 0; j < 2; ++j ) {
                uint32_t sum = vget<uint32_t>(op.Vin(rf, 0) + q, 2 * i + j);
                for ( unsigned k = 0; k < 2; ++k ) {
                    const uint32_t prod = A64Simd::bfAdd(A64Simd::bfMul(vget<uint16_t>(n, 4 * i + 2 * k), vget<uint16_t>(m, 4 * j + 2 * k)),
                        A64Simd::bfMul(vget<uint16_t>(n, 4 * i + 2 * k + 1), vget<uint16_t>(m, 4 * j + 2 * k + 1)));
                    sum = A64Simd::bfAdd(sum, prod);
                }
                vset<uint32_t>(r + q, 2 * i + j, sum);
            }
        }
    }
    writeZ(op, rf, r);
}

// FMMLA over segments of four elements (S over 16 bytes, D over 32). Sources Zda, Zn, Zm.
inline void
fmmlaZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const unsigned seg_bytes = 4 * sizeof(T);
        std::memset(r, 0, vl);
        for ( unsigned q = 0; q + seg_bytes <= vl; q += seg_bytes ) {
            const uint8_t* d = op.Vin(rf, 0) + q;
            const uint8_t* n = op.Vin(rf, 1) + q;
            const uint8_t* m = op.Vin(rf, 2) + q;
            T out[4];
            out[0] = add(add(vget<T>(d, 0), mul(vget<T>(n, 0), vget<T>(m, 0), c), c), mul(vget<T>(n, 1), vget<T>(m, 2), c), c);
            out[1] = add(add(vget<T>(d, 1), mul(vget<T>(n, 0), vget<T>(m, 1), c), c), mul(vget<T>(n, 1), vget<T>(m, 3), c), c);
            out[2] = add(add(vget<T>(d, 2), mul(vget<T>(n, 2), vget<T>(m, 0), c), c), mul(vget<T>(n, 3), vget<T>(m, 2), c), c);
            out[3] = add(add(vget<T>(d, 3), mul(vget<T>(n, 2), vget<T>(m, 1), c), c), mul(vget<T>(n, 3), vget<T>(m, 3), c), c);
            std::memcpy(r + q, out, seg_bytes);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FCVTLT, FCVTNT, FCVTXNT, BFCVTNT: merging on the top half of each container. a = log2 container bytes.
// c: 0 FCVTLT (narrow element in the top half widens), 1 FCVTNT, 2 FCVTXNT, 3 BFCVTNT. Sources Pg, Zd, Zn.
inline void
fpTopConvert(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)) & ~FPCR_AHP);
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    std::memcpy(r, op.Vin(rf, 1), vl);
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        if ( !pbit(op.Vin(rf, 0), e * ebytes) ) continue;
        const uint8_t* s = op.Vin(rf, 2) + e * ebytes;
        uint8_t*       d = r + e * ebytes;
        if ( op.c == 0 ) {
            if ( ebytes == 4 ) vset<uint32_t>(d, 0, A64FpScalar::convertCv<uint32_t>(vget<uint16_t>(s, 1), c));
            else vset<uint64_t>(d, 0, convert<uint64_t>(vget<uint32_t>(s, 1), c));
        }
        else if ( op.c == 1 ) {
            if ( ebytes == 4 ) vset<uint16_t>(d, 1, A64FpScalar::convertCv<uint16_t>(vget<uint32_t>(s, 0), c));
            else vset<uint32_t>(d, 1, convert<uint32_t>(vget<uint64_t>(s, 0), c));
        }
        else if ( op.c == 2 ) {
            vset<uint32_t>(d, 1, convertOdd(vget<uint64_t>(s, 0), c));
        }
        else {
            vset<uint16_t>(d, 1, bfConvert(vget<uint32_t>(s, 0), c));
        }
    }
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

// FADDP, FMAXNMP, FMINNMP, FMAXP, FMINP. c as predFpElem. Sources Pg, Zdn, Zm.
inline void
fpPairwiseZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned vl = vlOf(rf);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const unsigned n = vl / sizeof(T);
        for ( unsigned e = 0; e < n; ++e ) {
            const uint8_t* src = (e % 2 == 0) ? op.Vin(rf, 1) : op.Vin(rf, 2);
            const T a = vget<T>(src, e - e % 2), b = vget<T>(src, e - e % 2 + 1);
            T v = vget<T>(op.Vin(rf, 1), e);
            if ( pbit(op.Vin(rf, 0), e * sizeof(T)) ) v = predFpElem<T>(op.c, a, b, c);
            vset<T>(r, e, v);
        }
    });
    writeZ(op, rf, r);
    op.fp_flags = c.flags;
}

} // namespace A64SveFp
} // namespace Vanadis
} // namespace SST

#endif
