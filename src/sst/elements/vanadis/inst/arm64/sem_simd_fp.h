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

// Floating-point micro-ops of Advanced SIMD. The FPCR is integer source 0. a = log2 of the element bytes, b = bytes of
// the operation (the element bytes for a scalar form), c = the operation code. A vector element uses the operations
// of sem_fp.h, so it follows the same flush and NaN rules as the scalar instructions.

#ifndef _H_VANADIS_ARM64_SEM_SIMD_FP
#define _H_VANADIS_ARM64_SEM_SIMD_FP

#include "inst/arm64/sem_fpscalar.h"
#include "inst/arm64/sem_simd.h"

namespace SST {
namespace Vanadis {
namespace A64Simd {

using namespace A64Fp;

template <typename T>
inline T
maskOf(bool v)
{
    return v ? static_cast<T>(~T(0)) : T(0);
}

// c = (U << 6) | (size<1> << 5) | opcode of the encoding, the FP16 opcodes being mapped to 24 + their 3-bit opcode.
// Sources: FPCR; Vn, Vm and Vd for FMLA and FMLS.
inline void
simdFpThreeSame(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const uint8_t* vn = op.Vin(rf, 0);
    const uint8_t* vm = op.Vin(rf, 1);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const unsigned n = op.b / sizeof(T);
        const uint32_t code = op.c;
        const bool pairwise = code == 0x58 || code == 0x5A || code == 0x5E || code == 0x78 || code == 0x7E;
        if ( pairwise ) {
            T con[16];
            for ( unsigned e = 0; e < n; ++e ) {
                con[e] = vget<T>(vn, e);
                con[e + n] = vget<T>(vm, e);
            }
            for ( unsigned e = 0; e < n; ++e ) {
                const T x = con[2 * e], y = con[2 * e + 1];
                T v;
                switch ( code ) {
                case 0x58: v = maxMinNum(x, y, true, c); break;
                case 0x5A: v = add(x, y, c); break;
                case 0x5E: v = maxMin(x, y, true, c); break;
                case 0x78: v = maxMinNum(x, y, false, c); break;
                default: v = maxMin(x, y, false, c); break;
                }
                vset<T>(r, e, v);
            }
            return;
        }
        for ( unsigned e = 0; e < n; ++e ) {
            const T x = vget<T>(vn, e), y = vget<T>(vm, e);
            T       v = 0;
            switch ( code ) {
            case 0x18: v = maxMinNum(x, y, true, c); break;
            case 0x19: v = mulAdd(vget<T>(op.Vin(rf, 2), e), x, y, c); break;
            case 0x1A: v = add(x, y, c); break;
            case 0x1B: v = mulx(x, y, c); break;
            case 0x1C: v = maskOf<T>(cmpEq(x, y, c)); break;
            case 0x1E: v = maxMin(x, y, true, c); break;
            case 0x1F: v = recipStep(x, y, c); break;
            case 0x38: v = maxMinNum(x, y, false, c); break;
            case 0x39: v = mulAdd(vget<T>(op.Vin(rf, 2), e), neg(x), y, c); break;
            case 0x3A: v = sub(x, y, c); break;
            case 0x3E: v = maxMin(x, y, false, c); break;
            case 0x3F: v = rsqrtStep(x, y, c); break;
            case 0x5B: v = mul(x, y, c); break;
            case 0x5C: v = maskOf<T>(cmpGe(x, y, c)); break;
            case 0x5D: v = maskOf<T>(cmpGe(A64Fp::abs(x), A64Fp::abs(y), c)); break;
            case 0x5F: v = A64Fp::div(x, y, c); break;
            case 0x7A: v = abd(x, y, c); break;
            case 0x7C: v = maskOf<T>(cmpGt(x, y, c)); break;
            case 0x7D: v = maskOf<T>(cmpGt(A64Fp::abs(x), A64Fp::abs(y), c)); break;
            default: break;
            }
            vset<T>(r, e, v);
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = c.flags;
}

// The conversion of one element to an integer of the same width. FP16 converts through 32 bits and saturates to 16.
template <typename T>
inline T
toElementInt(T a, Rounding rounding, unsigned fbits, bool is_signed, FpCtx& c)
{
    if constexpr ( sizeof(T) == 2 ) {
        FpCtx local(c.fpcr);
        const int64_t wide = is_signed ? static_cast<int64_t>(toInt<T, int32_t>(a, rounding, fbits, local))
                                       : static_cast<int64_t>(toInt<T, uint32_t>(a, rounding, fbits, local));
        const int64_t hi = is_signed ? 32767 : 65535;
        const int64_t lo = is_signed ? -32768 : 0;
        if ( wide > hi || wide < lo ) {
            c.flags |= (local.flags & ~IXC) | IOC;
            return static_cast<T>(wide > hi ? hi : lo);
        }
        c.flags |= local.flags;
        return static_cast<T>(wide);
    }
    else if constexpr ( sizeof(T) == 4 ) {
        return is_signed ? static_cast<T>(toInt<T, int32_t>(a, rounding, fbits, c)) : toInt<T, uint32_t>(a, rounding, fbits, c);
    }
    else {
        return is_signed ? static_cast<T>(toInt<T, int64_t>(a, rounding, fbits, c)) : toInt<T, uint64_t>(a, rounding, fbits, c);
    }
}

template <typename T>
inline T
fromElementInt(T v, unsigned fbits, bool is_signed, FpCtx& c)
{
    if constexpr ( sizeof(T) == 2 ) {
        return is_signed ? fromInt<T, int32_t>(static_cast<int16_t>(v), fbits, c)
                         : fromInt<T, uint32_t>(v, fbits, c);
    }
    else if constexpr ( sizeof(T) == 4 ) {
        return is_signed ? fromInt<T, int32_t>(static_cast<int32_t>(v), fbits, c) : fromInt<T, uint32_t>(v, fbits, c);
    }
    else {
        return is_signed ? fromInt<T, int64_t>(static_cast<int64_t>(v), fbits, c) : fromInt<T, uint64_t>(v, fbits, c);
    }
}

// Two-register floating-point operations. c = (U << 6) | (size<1> << 5) | opcode.
inline void
simdFpMisc(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const uint8_t* vn = op.Vin(rf, 0);
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const T z = 0;
        for ( unsigned e = 0; e < op.b / sizeof(T); ++e ) {
            const T a = vget<T>(vn, e);
            T       v = 0;
            switch ( op.c ) {
            case 0x18: v = roundInt(a, Rounding::TIEEVEN, false, c); break;
            case 0x19: v = roundInt(a, Rounding::NEGINF, false, c); break;
            case 0x1A: v = toElementInt(a, Rounding::TIEEVEN, 0, true, c); break;
            case 0x1B: v = toElementInt(a, Rounding::NEGINF, 0, true, c); break;
            case 0x1C: v = toElementInt(a, Rounding::TIEAWAY, 0, true, c); break;
            case 0x1D: v = fromElementInt(a, 0, true, c); break;
            case 0x1E: v = roundIntWidth(a, Rounding::ZERO, true, 32, c); break;
            case 0x1F: v = roundIntWidth(a, Rounding::ZERO, true, 64, c); break;
            case 0x2C: v = maskOf<T>(cmpGt(a, z, c)); break;
            case 0x2D: v = maskOf<T>(cmpEq(a, z, c)); break;
            case 0x2E: v = maskOf<T>(cmpGt(z, a, c)); break;
            case 0x2F: v = A64Fp::abs(a); break;
            case 0x38: v = roundInt(a, Rounding::POSINF, false, c); break;
            case 0x39: v = roundInt(a, Rounding::ZERO, false, c); break;
            case 0x3A: v = toElementInt(a, Rounding::POSINF, 0, true, c); break;
            case 0x3B: v = toElementInt(a, Rounding::ZERO, 0, true, c); break;
            case 0x3D: v = recipEstimate(a, c); break;
            case 0x3F: v = recipExponent(a, c); break;
            case 0x58: v = roundInt(a, Rounding::TIEAWAY, false, c); break;
            case 0x59: v = roundInt(a, c.rmode(), true, c); break;
            case 0x5A: v = toElementInt(a, Rounding::TIEEVEN, 0, false, c); break;
            case 0x5B: v = toElementInt(a, Rounding::NEGINF, 0, false, c); break;
            case 0x5C: v = toElementInt(a, Rounding::TIEAWAY, 0, false, c); break;
            case 0x5D: v = fromElementInt(a, 0, false, c); break;
            case 0x5E: v = roundIntWidth(a, c.rmode(), true, 32, c); break;
            case 0x5F: v = roundIntWidth(a, c.rmode(), true, 64, c); break;
            case 0x6C: v = maskOf<T>(cmpGe(a, z, c)); break;
            case 0x6D: v = maskOf<T>(cmpGe(z, a, c)); break;
            case 0x6F: v = neg(a); break;
            case 0x79: v = roundInt(a, c.rmode(), false, c); break;
            case 0x7A: v = toElementInt(a, Rounding::POSINF, 0, false, c); break;
            case 0x7B: v = toElementInt(a, Rounding::ZERO, 0, false, c); break;
            case 0x7D: v = rsqrtEstimate(a, c); break;
            case 0x7F: v = A64Fp::sqrt(a, c); break;
            default: break;
            }
            vset<T>(r, e, v);
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = c.flags;
}

// FCVTN, FCVTXN, FCVTL and the scalar FCVTXN. c: 0 FCVTN, 1 FCVTXN, 2 FCVTL. a = log2 of the narrow element bytes.
// d & 1 = Q, d & 2 = scalar. Source Vd when Q narrows into the upper half.
inline void
simdFpWidthConvert(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    FpCtx          c(static_cast<uint32_t>(op.X(rf, 0)));
    const uint8_t* vn = op.Vin(rf, 0);
    const bool     part = op.d & 1, scalar = op.d & 2;
    std::memset(r, 0, sizeof(r));
    unsigned out_bytes;
    if ( op.c == 2 ) {
        out_bytes = 16;
        if ( op.a == 1 ) {
            for ( unsigned e = 0; e < 4; ++e ) vset<uint32_t>(r, e, A64FpScalar::convertCv<uint32_t>(vget<uint16_t>(vn, e + (part ? 4 : 0)), c));
        }
        else {
            for ( unsigned e = 0; e < 2; ++e ) vset<uint64_t>(r, e, A64FpScalar::convertCv<uint64_t>(vget<uint32_t>(vn, e + (part ? 2 : 0)), c));
        }
    }
    else {
        out_bytes = scalar ? (1u << op.a) : (part ? 16 : 8);
        if ( part ) std::memcpy(r, op.Vin(rf, 1), 8);
        const unsigned count = scalar ? 1 : (op.a == 1 ? 4 : 2);
        for ( unsigned e = 0; e < count; ++e ) {
            if ( op.a == 1 ) {
                vset<uint16_t>(r, e + (part ? 4 : 0), A64FpScalar::convertCv<uint16_t>(vget<uint32_t>(vn, e), c));
            }
            else if ( op.c == 1 ) {
                vset<uint32_t>(r, e + (part ? 2 : 0), convertOdd(vget<uint64_t>(vn, e), c));
            }
            else {
                vset<uint32_t>(r, e + (part ? 2 : 0), convert<uint32_t>(vget<uint64_t>(vn, e), c));
            }
        }
    }
    writeLow(op, rf, 0, r, out_bytes);
    op.fp_flags = c.flags;
}

// FMAXNMV, FMINNMV, FMAXV, FMINV: Reduce by halves. c: 0 FMAXNM, 1 FMAX, 2 FMINNM, 3 FMIN. Also scalar pairwise with
// two elements: c: 4 FADD.
template <typename T>
inline T
fpReduce(const uint8_t* vn, unsigned lo, unsigned n, uint32_t code, FpCtx& c)
{
    if ( n == 1 ) return vget<T>(vn, lo);
    const T x = fpReduce<T>(vn, lo, n / 2, code, c);
    const T y = fpReduce<T>(vn, lo + n / 2, n / 2, code, c);
    switch ( code ) {
    case 0: return maxMinNum(x, y, true, c);
    case 1: return maxMin(x, y, true, c);
    case 2: return maxMinNum(x, y, false, c);
    case 3: return maxMin(x, y, false, c);
    default: return add(x, y, c);
    }
}

inline void
simdFpReduce(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    FpCtx c(static_cast<uint32_t>(op.X(rf, 0)));
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const T v = fpReduce<T>(op.Vin(rf, 0), 0, op.b / sizeof(T), op.c, c);
        writeLow(op, rf, 0, &v, sizeof(T));
    });
    op.fp_flags = c.flags;
}

// SCVTF, UCVTF, FCVTZS, FCVTZU with a fixed-point fraction width. c: 0 FCVTZS, 1 FCVTZU, 2 SCVTF, 3 UCVTF; imm = fbits.
inline void
simdFpFixed(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    FpCtx   c(static_cast<uint32_t>(op.X(rf, 0)));
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < op.b / sizeof(T); ++e ) {
            const T a = vget<T>(op.Vin(rf, 0), e);
            T v;
            if ( op.c < 2 ) v = toElementInt(a, Rounding::ZERO, static_cast<unsigned>(op.imm), op.c == 0, c);
            else v = fromElementInt(a, static_cast<unsigned>(op.imm), op.c == 2, c);
            vset<T>(r, e, v);
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = c.flags;
}

// Indexed FMLA, FMLS, FMUL, FMULX. c: 0, 1, 2, 3; imm = the element index of Vm. Sources: Vn, Vm and Vd.
inline void
simdFpIndexed(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    FpCtx   c(static_cast<uint32_t>(op.X(rf, 0)));
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const T y = vget<T>(op.Vin(rf, 1), static_cast<unsigned>(op.imm));
        for ( unsigned e = 0; e < op.b / sizeof(T); ++e ) {
            const T x = vget<T>(op.Vin(rf, 0), e);
            T v;
            switch ( op.c ) {
            case 0: v = mulAdd(vget<T>(op.Vin(rf, 2), e), x, y, c); break;
            case 1: v = mulAdd(vget<T>(op.Vin(rf, 2), e), neg(x), y, c); break;
            case 2: v = mul(x, y, c); break;
            default: v = mulx(x, y, c); break;
            }
            vset<T>(r, e, v);
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = c.flags;
}

// A half value as a single with the same value; a NaN keeps its type. FZ16 flushes first.
inline uint32_t
halfAsSingle(uint16_t a, FpCtx& c)
{
    a = flushIn(a, c);
    if ( isNaN(a) ) {
        return (static_cast<uint32_t>(a & Fmt<uint16_t>::SIGN) << 16) | Fmt<uint32_t>::EXPM |
               (static_cast<uint32_t>(a & Fmt<uint16_t>::FRACM) << 13);
    }
    sfBegin(Rounding::TIEEVEN);
    return f16_to_f32(f(a)).v;
}

// FMLAL, FMLAL2, FMLSL, FMLSL2. a is unused. c: 0 FMLAL, 1 FMLSL. d & 1: the upper half of the 16-bit sources.
// d & 2: indexed by imm. b = operation bytes of the 32-bit results. Sources: Vn, Vm, Vd.
inline void
simdFpMulAddLong(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    FpCtx   c(static_cast<uint32_t>(op.X(rf, 0)));
    const unsigned n = op.b / 4;
    const unsigned off = (op.d & 1) ? n : 0;
    for ( unsigned e = 0; e < n; ++e ) {
        uint16_t x = vget<uint16_t>(op.Vin(rf, 0), e + off);
        if ( op.c ) x = neg(x);
        const uint16_t y = vget<uint16_t>(op.Vin(rf, 1), (op.d & 2) ? static_cast<unsigned>(op.imm) : e + off);
        const uint32_t a = vget<uint32_t>(op.Vin(rf, 2), e);
        const uint32_t sx = halfAsSingle(x, c), sy = halfAsSingle(y, c);
        vset<uint32_t>(r, e, mulAdd(a, sx, sy, c));
    }
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = c.flags;
}

// FCADD: d = rotation select (0 = 90, 1 = 270). Sources Vn, Vm.
inline void
simdFcadd(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    FpCtx   c(static_cast<uint32_t>(op.X(rf, 0)));
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        for ( unsigned e = 0; e < op.b / sizeof(T) / 2; ++e ) {
            T m0 = vget<T>(op.Vin(rf, 1), 2 * e), m1 = vget<T>(op.Vin(rf, 1), 2 * e + 1);
            if ( op.d == 0 ) m1 = neg(m1);
            else m0 = neg(m0);
            vset<T>(r, 2 * e, add(vget<T>(op.Vin(rf, 0), 2 * e), m1, c));
            vset<T>(r, 2 * e + 1, add(vget<T>(op.Vin(rf, 0), 2 * e + 1), m0, c));
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = c.flags;
}

// FCMLA: d = rot field (0..3), c & 1 = indexed, imm = pair index of Vm. Sources Vn, Vm, Vd.
inline void
simdFcmla(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    FpCtx   c(static_cast<uint32_t>(op.X(rf, 0)));
    A64FpScalar::byFormat(op.a, [&](auto zero) {
        using T = decltype(zero);
        const uint8_t* vn = op.Vin(rf, 0);
        const uint8_t* vm = op.Vin(rf, 1);
        const uint8_t* vd = op.Vin(rf, 2);
        for ( unsigned e = 0; e < op.b / sizeof(T) / 2; ++e ) {
            const unsigned me = (op.c & 1) ? static_cast<unsigned>(op.imm) : e;
            const T m_r = vget<T>(vm, 2 * me), m_i = vget<T>(vm, 2 * me + 1);
            const T n_r = vget<T>(vn, 2 * e), n_i = vget<T>(vn, 2 * e + 1);
            T e1, e2, e3, e4;
            switch ( op.d ) {
            case 0: e1 = m_r; e2 = n_r; e3 = m_i; e4 = n_r; break;
            case 1: e1 = neg(m_i); e2 = n_i; e3 = m_r; e4 = n_i; break;
            case 2: e1 = neg(m_r); e2 = n_r; e3 = neg(m_i); e4 = n_r; break;
            default: e1 = m_i; e2 = n_i; e3 = neg(m_r); e4 = n_i; break;
            }
            vset<T>(r, 2 * e, mulAdd(vget<T>(vd, 2 * e), e2, e1, c));
            vset<T>(r, 2 * e + 1, mulAdd(vget<T>(vd, 2 * e + 1), e4, e3, c));
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = c.flags;
}

// BFloat16: a value of the format, unpacked to a single with subnormals flushed and every NaN quiet.
inline uint32_t
bfUnpack(uint16_t a)
{
    if ( (a & 0x7F80) == 0 ) return static_cast<uint32_t>(a & 0x8000) << 16;
    return static_cast<uint32_t>(a) << 16;
}

// The rounding of BFMulH and BFAdd without FEAT_EBF16 output behaviour: round to odd, flush tiny results, an overflow
// gives infinity, no exception flags.
inline uint32_t
bfRoundResult(float32_t r)
{
    const uint_fast8_t e = softfloat_exceptionFlags;
    const uint32_t     s = r.v & Fmt<uint32_t>::SIGN;
    if ( isSub(r.v) || (e & softfloat_flag_underflow) ) return s;
    if ( e & softfloat_flag_overflow ) return s | Fmt<uint32_t>::INF;
    return r.v;
}

inline uint32_t
bfMul(uint16_t a, uint16_t b)
{
    const uint32_t x = bfUnpack(a), y = bfUnpack(b);
    if ( isNaN(x) || isNaN(y) ) return Fmt<uint32_t>::DEFNAN;
    sfBegin(Rounding::ODD);
    const float32_t r = f32_mul(f(x), f(y));
    if ( isNaN(r.v) ) return Fmt<uint32_t>::DEFNAN;
    return bfRoundResult(r);
}

inline uint32_t
bfAdd(uint32_t a, uint32_t b)
{
    if ( isSub(a) ) a &= Fmt<uint32_t>::SIGN;
    if ( isSub(b) ) b &= Fmt<uint32_t>::SIGN;
    if ( isNaN(a) || isNaN(b) ) return Fmt<uint32_t>::DEFNAN;
    sfBegin(Rounding::ODD);
    const float32_t r = f32_add(f(a), f(b));
    if ( isNaN(r.v) ) return Fmt<uint32_t>::DEFNAN;
    return bfRoundResult(r);
}

// BFDOT. d & 1: indexed by imm, a 32-bit group of Vm. Sources: Vn, Vm, Vd.
inline void
simdBfDot(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    const uint8_t* vm = op.Vin(rf, 1);
    for ( unsigned g = 0; g < op.b / 4; ++g ) {
        const unsigned mg = (op.d & 1) ? static_cast<unsigned>(op.imm) : g;
        const uint32_t prod = bfAdd(bfMul(vget<uint16_t>(vn, 2 * g), vget<uint16_t>(vm, 2 * mg)),
            bfMul(vget<uint16_t>(vn, 2 * g + 1), vget<uint16_t>(vm, 2 * mg + 1)));
        vset<uint32_t>(r, g, bfAdd(vget<uint32_t>(op.Vin(rf, 2), g), prod));
    }
    writeLow(op, rf, 0, r, op.b);
}

// BFMMLA: BFMatMulAdd. Sources: Vn, Vm, Vd.
inline void
simdBfMmla(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    const uint8_t* vm = op.Vin(rf, 1);
    for ( unsigned i = 0; i < 2; ++i ) {
        for ( unsigned j = 0; j < 2; ++j ) {
            uint32_t sum = vget<uint32_t>(op.Vin(rf, 2), 2 * i + j);
            for ( unsigned k = 0; k < 2; ++k ) {
                const uint32_t prod = bfAdd(bfMul(vget<uint16_t>(vn, 4 * i + 2 * k), vget<uint16_t>(vm, 4 * j + 2 * k)),
                    bfMul(vget<uint16_t>(vn, 4 * i + 2 * k + 1), vget<uint16_t>(vm, 4 * j + 2 * k + 1)));
                sum = bfAdd(sum, prod);
            }
            vset<uint32_t>(r, 2 * i + j, sum);
        }
    }
    writeLow(op, rf, 0, r, 16);
}

// BFMLALB, BFMLALT: BFMulAddH is FPMulAdd on the operands as singles, under the FPCR.
// c: 0 bottom, 1 top. d & 1: indexed by imm. Sources: Vn, Vm, Vd.
inline void
simdBfMlal(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    FpCtx   c(static_cast<uint32_t>(op.X(rf, 0)));
    for ( unsigned e = 0; e < 4; ++e ) {
        const uint16_t x = vget<uint16_t>(op.Vin(rf, 0), 2 * e + op.c);
        const uint16_t y = vget<uint16_t>(op.Vin(rf, 1), (op.d & 1) ? static_cast<unsigned>(op.imm) : 2 * e + op.c);
        vset<uint32_t>(r, e, mulAdd(vget<uint32_t>(op.Vin(rf, 2), e), static_cast<uint32_t>(x) << 16, static_cast<uint32_t>(y) << 16, c));
    }
    writeLow(op, rf, 0, r, 16);
    op.fp_flags = c.flags;
}

// BFCVTN, BFCVTN2. d & 1 = Q. Source Vd when Q.
inline void
simdBfConvertNarrow(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    FpCtx   c(static_cast<uint32_t>(op.X(rf, 0)));
    std::memset(r, 0, sizeof(r));
    if ( op.d & 1 ) std::memcpy(r, op.Vin(rf, 1), 8);
    for ( unsigned e = 0; e < 4; ++e ) {
        vset<uint16_t>(r, e + ((op.d & 1) ? 4 : 0), bfConvert(vget<uint32_t>(op.Vin(rf, 0), e), c));
    }
    writeLow(op, rf, 0, r, (op.d & 1) ? 16 : 8);
    op.fp_flags = c.flags;
}

} // namespace A64Simd
} // namespace Vanadis
} // namespace SST

#endif
