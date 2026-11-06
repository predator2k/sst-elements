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

// Integer micro-ops of Advanced SIMD. The payload is common to all of them: a = log2 of the element bytes, b = bytes
// of the operation (8 or 16 for a vector form, the element bytes for a scalar form), c = the operation code. A scalar
// form runs the same code on one element and zeroes the rest of the register.

#ifndef _H_VANADIS_ARM64_SEM_SIMD_INT
#define _H_VANADIS_ARM64_SEM_SIMD_INT

#include "inst/arm64/sem_fp.h"
#include "inst/arm64/sem_simd.h"

#include <type_traits>

namespace SST {
namespace Vanadis {
namespace A64Simd {

using s128 = __int128;
using u128 = unsigned __int128;

// Calls f with a value of the unsigned element type of a log2 byte size.
template <typename F>
inline void
byElem(uint32_t log2_bytes, F&& f)
{
    switch ( log2_bytes ) {
    case 0: f(uint8_t {}); break;
    case 1: f(uint16_t {}); break;
    case 2: f(uint32_t {}); break;
    default: f(uint64_t {}); break;
    }
}

template <typename U>
inline s128
widen(U v, bool is_unsigned)
{
    using S = std::make_signed_t<U>;
    return is_unsigned ? static_cast<s128>(v) : static_cast<s128>(static_cast<S>(v));
}

inline s128
signedMax(unsigned bits)
{
    return (static_cast<s128>(1) << (bits - 1)) - 1;
}
inline s128
signedMin(unsigned bits)
{
    return -(static_cast<s128>(1) << (bits - 1));
}
inline s128
unsignedMax(unsigned bits)
{
    return (static_cast<s128>(1) << bits) - 1;
}

// SatQ: the value clamped to the signed or unsigned range of bits; QC is set when it changes.
inline s128
satQ(s128 v, unsigned bits, bool is_unsigned, uint32_t& flags)
{
    const s128 hi = is_unsigned ? unsignedMax(bits) : signedMax(bits);
    const s128 lo = is_unsigned ? static_cast<s128>(0) : signedMin(bits);
    if ( v > hi ) {
        flags |= A64Fp::QC;
        return hi;
    }
    if ( v < lo ) {
        flags |= A64Fp::QC;
        return lo;
    }
    return v;
}

// The shift of SSHL, USHL, SRSHL, URSHL, SQSHL, UQSHL, SQRSHL and UQRSHL: shift is the signed low byte of the second
// operand, negative for a right shift.
template <typename U>
inline U
regShift(U x, int shift, bool is_unsigned, bool rounding, bool saturating, uint32_t& flags)
{
    const unsigned bits = sizeof(U) * 8;
    const s128     v = widen(x, is_unsigned);
    if ( shift >= 0 ) {
        if ( !saturating ) return static_cast<unsigned>(shift) >= bits ? U(0) : static_cast<U>(x << shift);
        if ( v == 0 ) return U(0);
        if ( static_cast<unsigned>(shift) >= bits ) {
            flags |= A64Fp::QC;
            if ( is_unsigned ) return static_cast<U>(unsignedMax(bits));
            return static_cast<U>(v < 0 ? signedMin(bits) : signedMax(bits));
        }
        return static_cast<U>(satQ(v * (static_cast<s128>(1) << shift), bits, is_unsigned, flags));
    }
    const int right = -shift > 126 ? 126 : -shift;
    const s128 round = rounding ? (static_cast<s128>(1) << (right - 1)) : static_cast<s128>(0);
    return static_cast<U>((v + round) >> right);
}

template <typename U>
inline U
polyMul(U a, U b)
{
    U r = 0;
    for ( unsigned k = 0; k < sizeof(U) * 8; ++k ) {
        if ( (b >> k) & 1 ) r ^= static_cast<U>(a << k);
    }
    return r;
}

// SQDMULH and SQRDMULH of one element pair.
template <typename U>
inline U
satDoubleMulHigh(U x, U y, bool rounding, uint32_t& flags)
{
    // (2 * x * y + round) >> bits, computed without the factor 2 so a 64-bit element stays inside 128 bits.
    const unsigned bits = sizeof(U) * 8;
    s128 p = widen(x, false) * widen(y, false);
    if ( rounding ) p += static_cast<s128>(1) << (bits - 2);
    return static_cast<U>(satQ(p >> (bits - 1), bits, false, flags));
}

// SQRDMLAH and SQRDMLSH of one element triple.
template <typename U>
inline U
satRoundDoubleMulAccum(U d, U x, U y, bool subtract, uint32_t& flags)
{
    const unsigned bits = sizeof(U) * 8;
    // ((d << bits) +- 2 * x * y + round) >> bits, without the common factor 2.
    const s128 product = widen(x, false) * widen(y, false);
    const s128 accum = (widen(d, false) * (static_cast<s128>(1) << (bits - 1))) + (subtract ? -product : product) +
                       (static_cast<s128>(1) << (bits - 2));
    return static_cast<U>(satQ(accum >> (bits - 1), bits, false, flags));
}

// Three registers of the same element size. c = (U << 5) | opcode of the encoding. Sources: Vn, Vm and, for the
// accumulating and bit-select operations, Vd. d = the size field for the logical group.
inline void
simdThreeSame(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    const uint8_t* vm = op.Vin(rf, 1);
    uint32_t       flags = 0;
    const uint32_t code = op.c & 0x1F;
    const bool     is_u = op.c & 0x20;
    if ( code == 3 ) {
        // AND, BIC, ORR, ORN, EOR, BSL, BIT, BIF on the whole operation width.
        for ( uint32_t k = 0; k < op.b / 8; ++k ) {
            const uint64_t x = vget<uint64_t>(vn, k), y = vget<uint64_t>(vm, k);
            uint64_t v = 0;
            switch ( (static_cast<uint32_t>(is_u) << 2) | op.d ) {
            case 0: v = x & y; break;
            case 1: v = x & ~y; break;
            case 2: v = x | y; break;
            case 3: v = x | ~y; break;
            case 4: v = x ^ y; break;
            case 5: { const uint64_t d = vget<uint64_t>(op.Vin(rf, 2), k); v = (d & x) | (~d & y); } break;
            case 6: { const uint64_t d = vget<uint64_t>(op.Vin(rf, 2), k); v = (y & x) | (~y & d); } break;
            default: { const uint64_t d = vget<uint64_t>(op.Vin(rf, 2), k); v = (~y & x) | (y & d); } break;
            }
            vset<uint64_t>(r, k, v);
        }
        writeLow(op, rf, 0, r, op.b);
        return;
    }
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        const U        ones = static_cast<U>(~U(0));
        const unsigned n = op.b / sizeof(U);
        if ( code == 20 || code == 21 || code == 23 ) {
            // SMAXP, UMAXP, SMINP, UMINP, ADDP: pairs of the concatenation Vm:Vn.
            U c[32];
            for ( unsigned e = 0; e < n; ++e ) {
                c[e] = vget<U>(vn, e);
                c[e + n] = vget<U>(vm, e);
            }
            for ( unsigned e = 0; e < n; ++e ) {
                const U p = c[2 * e], q = c[2 * e + 1];
                U       v;
                if ( code == 23 ) v = static_cast<U>(p + q);
                else {
                    const bool first = code == 20 ? widen(p, is_u) >= widen(q, is_u) : widen(p, is_u) <= widen(q, is_u);
                    v = first ? p : q;
                }
                vset<U>(r, e, v);
            }
            return;
        }
        for ( unsigned e = 0; e < n; ++e ) {
            const U    x = vget<U>(vn, e), y = vget<U>(vm, e);
            const s128 ix = widen(x, is_u), iy = widen(y, is_u);
            U          v = 0;
            switch ( code ) {
            case 0: v = static_cast<U>((ix + iy) >> 1); break;
            case 1: v = static_cast<U>(satQ(ix + iy, bits, is_u, flags)); break;
            case 2: v = static_cast<U>((ix + iy + 1) >> 1); break;
            case 4: v = static_cast<U>((ix - iy) >> 1); break;
            case 5: v = static_cast<U>(satQ(ix - iy, bits, is_u, flags)); break;
            case 6: v = ix > iy ? ones : U(0); break;
            case 7: v = ix >= iy ? ones : U(0); break;
            case 8: v = regShift(x, static_cast<int8_t>(y), is_u, false, false, flags); break;
            case 9: v = regShift(x, static_cast<int8_t>(y), is_u, false, true, flags); break;
            case 10: v = regShift(x, static_cast<int8_t>(y), is_u, true, false, flags); break;
            case 11: v = regShift(x, static_cast<int8_t>(y), is_u, true, true, flags); break;
            case 12: v = ix >= iy ? x : y; break;
            case 13: v = ix <= iy ? x : y; break;
            case 14: v = static_cast<U>(ix > iy ? ix - iy : iy - ix); break;
            case 15: v = static_cast<U>(vget<U>(op.Vin(rf, 2), e) + static_cast<U>(ix > iy ? ix - iy : iy - ix)); break;
            case 16: v = is_u ? static_cast<U>(x - y) : static_cast<U>(x + y); break;
            case 17: v = is_u ? (x == y ? ones : U(0)) : ((x & y) != 0 ? ones : U(0)); break;
            case 18: {
                const U p = static_cast<U>(static_cast<uint64_t>(x) * static_cast<uint64_t>(y));
                const U d = vget<U>(op.Vin(rf, 2), e);
                v = is_u ? static_cast<U>(d - p) : static_cast<U>(d + p);
            } break;
            case 19: v = is_u ? polyMul(x, y) : static_cast<U>(static_cast<uint64_t>(x) * static_cast<uint64_t>(y)); break;
            case 22: v = satDoubleMulHigh(x, y, is_u, flags); break;
            default: break;
            }
            vset<U>(r, e, v);
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = flags;
}

// Indexed multiply. Sources Vn, Vm (the element), and Vd where needed. c: 0 MUL, 1 MLA, 2 MLS, 3 SQDMULH, 4 SQRDMULH,
// 5 SQRDMLAH, 6 SQRDMLSH. imm = the element index.
inline void
simdIndexedSame(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t  r[16];
    uint32_t flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const U y = vget<U>(op.Vin(rf, 1), static_cast<unsigned>(op.imm));
        for ( unsigned e = 0; e < op.b / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 0), e);
            U       v = 0;
            switch ( op.c ) {
            case 0: v = static_cast<U>(static_cast<uint64_t>(x) * y); break;
            case 1: v = static_cast<U>(vget<U>(op.Vin(rf, 2), e) + static_cast<uint64_t>(x) * y); break;
            case 2: v = static_cast<U>(vget<U>(op.Vin(rf, 2), e) - static_cast<uint64_t>(x) * y); break;
            case 3: v = satDoubleMulHigh(x, y, false, flags); break;
            case 4: v = satDoubleMulHigh(x, y, true, flags); break;
            case 5: v = satRoundDoubleMulAccum(vget<U>(op.Vin(rf, 2), e), x, y, false, flags); break;
            default: v = satRoundDoubleMulAccum(vget<U>(op.Vin(rf, 2), e), x, y, true, flags); break;
            }
            vset<U>(r, e, v);
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = flags;
}

// SQRDMLAH, SQRDMLSH (three-register extension). Sources Vn, Vm, Vd. c: 0 add, 1 subtract.
inline void
simdSatMulAccum(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t  r[16];
    uint32_t flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < op.b / sizeof(U); ++e ) {
            vset<U>(r, e, satRoundDoubleMulAccum(vget<U>(op.Vin(rf, 2), e), vget<U>(op.Vin(rf, 0), e),
                              vget<U>(op.Vin(rf, 1), e), op.c, flags));
        }
    });
    writeLow(op, rf, 0, r, op.b);
    op.fp_flags = flags;
}

// Widening, narrowing and long operations of "three registers of different types". a is the size of the narrower
// element. c = (U << 4) | opcode. d & 1: Q (the upper half of the narrow operands). Sources Vn, Vm and Vd where needed.
// With e & scalar (d & 2): one element, the scalar forms SQDMLAL, SQDMLSL and SQDMULL.
inline void
simdThreeDiff(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    const uint8_t* vm = op.Vin(rf, 1);
    uint32_t       flags = 0;
    const uint32_t code = op.c & 0xF;
    const bool     is_u = op.c & 0x10;
    const bool     part = op.d & 1;
    const bool     scalar = op.d & 2;
    const unsigned out_bytes = scalar ? (2u << op.a) : 16u;
    byElem(op.a, [&](auto zero) {
        using N = decltype(zero);
        const unsigned nbytes = sizeof(N);
        const unsigned bits = nbytes * 8;
        const unsigned n = scalar ? 1 : 8 / nbytes;
        const unsigned hi_off = part ? n : 0;
        const auto     wide_get = [&](const uint8_t* p, unsigned e) -> s128 {
            if constexpr ( sizeof(N) == 8 ) return 0;
            else {
                using W = std::conditional_t<sizeof(N) == 1, uint16_t, std::conditional_t<sizeof(N) == 2, uint32_t, uint64_t>>;
                return widen(vget<W>(p, e), is_u);
            }
        };
        const auto wide_set = [&](unsigned e, s128 v) { std::memcpy(r + e * 2 * nbytes, &v, 2 * nbytes); };
        if ( code == 4 || code == 6 ) {
            // ADDHN, RADDHN, SUBHN, RSUBHN: the upper half of each wide result; Q keeps the low half of Vd.
            if constexpr ( sizeof(N) < 8 ) {
                using W = std::conditional_t<sizeof(N) == 1, uint16_t, std::conditional_t<sizeof(N) == 2, uint32_t, uint64_t>>;
                if ( part ) std::memcpy(r, op.Vin(rf, 2), 8);
                for ( unsigned e = 0; e < n; ++e ) {
                    const W a = vget<W>(vn, e), b = vget<W>(vm, e);
                    W       s = code == 4 ? static_cast<W>(a + b) : static_cast<W>(a - b);
                    if ( is_u ) s = static_cast<W>(s + (W(1) << (bits - 1)));
                    vset<N>(r, e + hi_off, static_cast<N>(s >> bits));
                }
            }
            return;
        }
        if ( code == 14 ) {
            // PMULL: 8 by 8, or 64 by 64 from the crypto extension.
            for ( unsigned e = 0; e < n; ++e ) {
                const N x = vget<N>(vn, e + hi_off), y = vget<N>(vm, e + hi_off);
                u128    p = 0;
                for ( unsigned k = 0; k < bits; ++k ) {
                    if ( (y >> k) & 1 ) p ^= static_cast<u128>(x) << k;
                }
                std::memcpy(r + e * 2 * nbytes, &p, 2 * nbytes);
            }
            return;
        }
        for ( unsigned e = 0; e < n; ++e ) {
            s128 v = 0;
            const bool wide_op = code == 1 || code == 3;
            const s128 x = wide_op ? wide_get(vn, e) : widen(vget<N>(vn, e + hi_off), is_u);
            const s128 y = widen(vget<N>(vm, e + hi_off), is_u);
            const auto acc = [&]() -> s128 {
                s128 a = 0;
                std::memcpy(&a, op.Vin(rf, 2) + e * 2 * nbytes, 2 * nbytes);
                if ( !is_u && nbytes * 2 < 16 && (a >> (2 * bits - 1)) ) a -= static_cast<s128>(1) << (2 * bits);
                return a;
            };
            const s128 d = x > y ? x - y : y - x;
            switch ( code ) {
            case 0: case 1: v = x + y; break;
            case 2: case 3: v = x - y; break;
            case 5: v = acc() + d; break;
            case 7: v = d; break;
            case 8: v = acc() + x * y; break;
            case 10: v = acc() - x * y; break;
            case 12: v = x * y; break;
            case 9:
            case 11:
            case 13: {
                const s128 p = satQ(2 * x * y, 2 * bits, false, flags);
                if ( code == 13 ) v = p;
                else {
                    s128 a = 0;
                    std::memcpy(&a, op.Vin(rf, 2) + e * 2 * nbytes, 2 * nbytes);
                    if ( a >> (2 * bits - 1) ) a -= static_cast<s128>(1) << (2 * bits);
                    v = satQ(code == 9 ? a + p : a - p, 2 * bits, false, flags);
                }
            } break;
            default: break;
            }
            wide_set(e, v);
        }
    });
    const bool narrow_result = code == 4 || code == 6;
    writeLow(op, rf, 0, r, narrow_result ? (part ? 16 : 8) : out_bytes);
    op.fp_flags = flags;
}

// The indexed long multiplies: SMLAL, SMLSL, SMULL, UMLAL, UMLSL, UMULL, SQDMLAL, SQDMLSL, SQDMULL. a = narrow size;
// c = (U << 4) | the code of simdThreeDiff; d & 1 = Q, d & 2 = scalar; imm = the element index of Vm.
inline void
simdIndexedLong(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    uint32_t       flags = 0;
    const uint32_t code = op.c & 0xF;
    const bool     is_u = op.c & 0x10;
    const bool     part = op.d & 1, scalar = op.d & 2;
    byElem(op.a, [&](auto zero) {
        using N = decltype(zero);
        const unsigned nbytes = sizeof(N), bits = nbytes * 8;
        const unsigned n = scalar ? 1 : 8 / nbytes;
        const unsigned hi_off = part ? n : 0;
        const s128     y = widen(vget<N>(op.Vin(rf, 1), static_cast<unsigned>(op.imm)), is_u);
        for ( unsigned e = 0; e < n; ++e ) {
            const s128 x = widen(vget<N>(op.Vin(rf, 0), e + hi_off), is_u);
            s128 a = 0;
            if ( code != 12 && code != 13 ) {
                std::memcpy(&a, op.Vin(rf, 2) + e * 2 * nbytes, 2 * nbytes);
                if ( !is_u && (a >> (2 * bits - 1)) ) a -= static_cast<s128>(1) << (2 * bits);
            }
            s128 v = 0;
            switch ( code ) {
            case 8: v = a + x * y; break;
            case 10: v = a - x * y; break;
            case 12: v = x * y; break;
            case 13: v = satQ(2 * x * y, 2 * bits, false, flags); break;
            case 9: v = satQ(a + satQ(2 * x * y, 2 * bits, false, flags), 2 * bits, false, flags); break;
            default: v = satQ(a - satQ(2 * x * y, 2 * bits, false, flags), 2 * bits, false, flags); break;
            }
            std::memcpy(r + e * 2 * nbytes, &v, 2 * nbytes);
        }
    });
    writeLow(op, rf, 0, r, scalar ? (2u << op.a) : 16u);
    op.fp_flags = flags;
}

// Dot products. c: 0 SDOT, 1 UDOT, 2 USDOT (Vn unsigned, Vm signed), 3 SUDOT (indexed only: Vn signed, Vm unsigned).
// d & 1: indexed by imm, a 32-bit group of Vm. a = log2 of the narrow element is 0; b = operation bytes.
inline void
simdDot(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    const uint8_t* vm = op.Vin(rf, 1);
    const uint8_t* vd = op.Vin(rf, 2);
    const bool     n_u = op.c == 1 || op.c == 2;
    const bool     m_u = op.c == 1 || op.c == 3;
    for ( uint32_t g = 0; g < op.b / 4; ++g ) {
        int64_t sum = static_cast<int32_t>(vget<uint32_t>(vd, g));
        const uint32_t mg = (op.d & 1) ? static_cast<uint32_t>(op.imm) : g;
        for ( uint32_t k = 0; k < 4; ++k ) {
            const int32_t x = n_u ? static_cast<int32_t>(vn[g * 4 + k]) : static_cast<int8_t>(vn[g * 4 + k]);
            const int32_t y = m_u ? static_cast<int32_t>(vm[mg * 4 + k]) : static_cast<int8_t>(vm[mg * 4 + k]);
            sum += x * y;
        }
        vset<uint32_t>(r, g, static_cast<uint32_t>(sum));
    }
    writeLow(op, rf, 0, r, op.b);
}

// SMMLA, UMMLA, USMMLA: c: 0, 1, 2. Sources Vn, Vm, Vd, 128 bits.
inline void
simdMmla(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    const uint8_t* vm = op.Vin(rf, 1);
    const uint8_t* vd = op.Vin(rf, 2);
    const bool     n_u = op.c == 1 || op.c == 2;
    const bool     m_u = op.c == 1;
    for ( uint32_t i = 0; i < 2; ++i ) {
        for ( uint32_t j = 0; j < 2; ++j ) {
            uint32_t sum = vget<uint32_t>(vd, 2 * i + j);
            for ( uint32_t k = 0; k < 8; ++k ) {
                const int32_t x = n_u ? static_cast<int32_t>(vn[8 * i + k]) : static_cast<int8_t>(vn[8 * i + k]);
                const int32_t y = m_u ? static_cast<int32_t>(vm[8 * j + k]) : static_cast<int8_t>(vm[8 * j + k]);
                sum += static_cast<uint32_t>(x * y);
            }
            vset<uint32_t>(r, 2 * i + j, sum);
        }
    }
    writeLow(op, rf, 0, r, 16);
}

// Two-register integer operations. c = (U << 5) | opcode. Narrowing forms: a = narrow size, d & 1 = Q, d & 2 = scalar.
inline void
simdMisc(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    uint32_t       flags = 0;
    const uint32_t code = op.c & 0x1F;
    const bool     is_u = op.c & 0x20;
    unsigned       out_bytes = op.b;
    std::memset(r, 0, sizeof(r));
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        using S = std::make_signed_t<U>;
        const unsigned bits = sizeof(U) * 8;
        const U        ones = static_cast<U>(~U(0));
        const unsigned n = op.b / sizeof(U);
        switch ( code ) {
        case 0:   // REV64, REV32
        case 1: { // REV16
            const unsigned container = code == 1 ? 2 : (is_u ? 4 : 8);
            const unsigned per = container / sizeof(U);
            for ( unsigned g = 0; g < op.b / container; ++g ) {
                for ( unsigned e = 0; e < per; ++e ) {
                    vset<U>(r, g * per + e, vget<U>(vn, g * per + per - 1 - e));
                }
            }
        } break;
        case 2:   // SADDLP, UADDLP
        case 6: { // SADALP, UADALP
            if constexpr ( sizeof(U) < 8 ) {
                using W = std::conditional_t<sizeof(U) == 1, uint16_t, std::conditional_t<sizeof(U) == 2, uint32_t, uint64_t>>;
                for ( unsigned g = 0; g < n / 2; ++g ) {
                    const s128 v = widen(vget<U>(vn, 2 * g), is_u) + widen(vget<U>(vn, 2 * g + 1), is_u);
                    W          w = static_cast<W>(v);
                    if ( code == 6 ) w = static_cast<W>(w + vget<W>(op.Vin(rf, 1), g));
                    vset<W>(r, g, w);
                }
            }
        } break;
        case 3: // SUQADD, USQADD
            for ( unsigned e = 0; e < n; ++e ) {
                const U d = vget<U>(op.Vin(rf, 1), e), x = vget<U>(vn, e);
                const s128 sum = widen(d, is_u) + widen(x, !is_u);
                vset<U>(r, e, static_cast<U>(satQ(sum, bits, is_u, flags)));
            }
            break;
        case 4: // CLS, CLZ
            for ( unsigned e = 0; e < n; ++e ) {
                U x = vget<U>(vn, e);
                unsigned count = 0;
                if ( is_u ) {
                    for ( int k = bits - 1; k >= 0 && !((x >> k) & 1); --k ) ++count;
                }
                else {
                    const bool top = (x >> (bits - 1)) & 1;
                    for ( int k = bits - 2; k >= 0 && (((x >> k) & 1) != 0) == top; --k ) ++count;
                }
                vset<U>(r, e, static_cast<U>(count));
            }
            break;
        case 5: // CNT; NOT and RBIT (U = 1, a is the raw size field)
            for ( unsigned e = 0; e < n; ++e ) {
                const uint8_t x = vn[e];
                uint8_t v;
                if ( !is_u ) {
                    v = static_cast<uint8_t>(__builtin_popcount(x));
                }
                else if ( op.imm == 0 ) {
                    v = static_cast<uint8_t>(~x);
                }
                else {
                    v = 0;
                    for ( unsigned k = 0; k < 8; ++k ) {
                        if ( (x >> k) & 1 ) v |= static_cast<uint8_t>(1u << (7 - k));
                    }
                }
                r[e] = v;
            }
            break;
        case 7: // SQABS, SQNEG
            for ( unsigned e = 0; e < n; ++e ) {
                const s128 x = widen(vget<U>(vn, e), false);
                const s128 v = is_u ? -x : (x < 0 ? -x : x);
                vset<U>(r, e, static_cast<U>(satQ(v, bits, false, flags)));
            }
            break;
        case 8: // CMGT #0, CMGE #0
            for ( unsigned e = 0; e < n; ++e ) {
                const S x = static_cast<S>(vget<U>(vn, e));
                vset<U>(r, e, (is_u ? x >= 0 : x > 0) ? ones : U(0));
            }
            break;
        case 9: // CMEQ #0, CMLE #0
            for ( unsigned e = 0; e < n; ++e ) {
                const S x = static_cast<S>(vget<U>(vn, e));
                vset<U>(r, e, (is_u ? x <= 0 : x == 0) ? ones : U(0));
            }
            break;
        case 10: // CMLT #0
            for ( unsigned e = 0; e < n; ++e ) vset<U>(r, e, static_cast<S>(vget<U>(vn, e)) < 0 ? ones : U(0));
            break;
        case 11: // ABS, NEG
            for ( unsigned e = 0; e < n; ++e ) {
                const U x = vget<U>(vn, e);
                U v;
                if ( is_u ) v = static_cast<U>(U(0) - x);
                else v = static_cast<S>(x) < 0 ? static_cast<U>(U(0) - x) : x;
                vset<U>(r, e, v);
            }
            break;
        case 18:   // XTN, SQXTUN
        case 20: { // SQXTN, UQXTN
            if constexpr ( sizeof(U) < 8 ) {
                using W = std::conditional_t<sizeof(U) == 1, uint16_t, std::conditional_t<sizeof(U) == 2, uint32_t, uint64_t>>;
                const bool     scalar = op.d & 2;
                const bool     part = op.d & 1;
                const unsigned count = scalar ? 1 : 8 / sizeof(U);
                const unsigned off = part ? count : 0;
                if ( part ) std::memcpy(r, op.Vin(rf, 1), 8);
                for ( unsigned e = 0; e < count; ++e ) {
                    const W w = vget<W>(vn, e);
                    U v;
                    if ( code == 18 && !is_u ) v = static_cast<U>(w);
                    else if ( code == 18 ) v = static_cast<U>(satQ(widen(w, false), bits, true, flags));
                    else v = static_cast<U>(satQ(widen(w, is_u), bits, is_u, flags));
                    vset<U>(r, e + off, v);
                }
                out_bytes = scalar ? sizeof(U) : (part ? 16 : 8);
            }
        } break;
        case 19: { // SHLL: a = narrow size, d & 1 = Q
            if constexpr ( sizeof(U) < 8 ) {
                using W = std::conditional_t<sizeof(U) == 1, uint16_t, std::conditional_t<sizeof(U) == 2, uint32_t, uint64_t>>;
                const unsigned count = 8 / sizeof(U);
                const unsigned off = (op.d & 1) ? count : 0;
                for ( unsigned e = 0; e < count; ++e ) vset<W>(r, e, static_cast<W>(static_cast<W>(vget<U>(vn, e + off)) << bits));
                out_bytes = 16;
            }
        } break;
        case 28: // URECPE
            for ( unsigned e = 0; e < n; ++e ) {
                const uint32_t x = vget<uint32_t>(vn, e);
                vset<uint32_t>(r, e, (x >> 31) ? (A64Fp::recipEstimateTable((x >> 23) & 0x1FF) << 23) : 0xFFFFFFFFu);
            }
            break;
        case 29: // URSQRTE
            for ( unsigned e = 0; e < n; ++e ) {
                const uint32_t x = vget<uint32_t>(vn, e);
                vset<uint32_t>(r, e, (x >> 30) ? (A64Fp::rsqrtEstimateTable((x >> 23) & 0x1FF) << 23) : 0xFFFFFFFFu);
            }
            break;
        default: break;
        }
    });
    writeLow(op, rf, 0, r, out_bytes);
    op.fp_flags = flags;
}

// Shift by immediate. c = (U << 5) | opcode; imm = right shift for the right-shift forms, left shift otherwise. Narrowing
// forms: a = narrow size, d & 1 = Q; scalar narrowing with d & 2. Sources Vn and, for accumulate or insert, Vd.
inline void
simdShiftImm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    uint32_t       flags = 0;
    const uint32_t code = op.c & 0x1F;
    const bool     is_u = op.c & 0x20;
    const unsigned sh = static_cast<unsigned>(op.imm);
    unsigned       out_bytes = op.b;
    std::memset(r, 0, sizeof(r));
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        const unsigned n = op.b / sizeof(U);
        if ( code >= 16 && code <= 19 ) {
            // SHRN, RSHRN, SQSHRN, SQRSHRN, UQSHRN, UQRSHRN, SQSHRUN, SQRSHRUN
            if constexpr ( sizeof(U) < 8 ) {
                using W = std::conditional_t<sizeof(U) == 1, uint16_t, std::conditional_t<sizeof(U) == 2, uint32_t, uint64_t>>;
                const bool     scalar = op.d & 2, part = op.d & 1;
                const unsigned count = scalar ? 1 : 8 / sizeof(U);
                const unsigned off = part ? count : 0;
                const bool     rounding = code & 1;
                if ( part ) std::memcpy(r, op.Vin(rf, 1), 8);
                for ( unsigned e = 0; e < count; ++e ) {
                    const W    w = vget<W>(vn, e);
                    U          v;
                    if ( code <= 17 && !is_u ) {   // SHRN, RSHRN
                        const u128 x = static_cast<u128>(w) + (rounding ? (static_cast<u128>(1) << (sh - 1)) : static_cast<u128>(0));
                        v = static_cast<U>(x >> sh);
                    }
                    else {
                        // SQSHRUN and SQRSHRUN (U = 1, opcode 16, 17) read a signed input and saturate to unsigned.
                        const bool in_u = code >= 18 && is_u;
                        const bool out_u = is_u;
                        s128 x = widen(w, in_u);
                        if ( rounding ) x += static_cast<s128>(1) << (sh - 1);
                        v = static_cast<U>(satQ(x >> sh, bits, out_u, flags));
                    }
                    vset<U>(r, e + off, v);
                }
                out_bytes = scalar ? sizeof(U) : (part ? 16 : 8);
            }
            return;
        }
        if ( code == 20 ) {   // SSHLL, USHLL
            if constexpr ( sizeof(U) < 8 ) {
                using W = std::conditional_t<sizeof(U) == 1, uint16_t, std::conditional_t<sizeof(U) == 2, uint32_t, uint64_t>>;
                const unsigned count = 8 / sizeof(U);
                const unsigned off = (op.d & 1) ? count : 0;
                for ( unsigned e = 0; e < count; ++e ) {
                    vset<W>(r, e, static_cast<W>(widen(vget<U>(vn, e + off), is_u) * (static_cast<s128>(1) << sh)));
                }
                out_bytes = 16;
            }
            return;
        }
        for ( unsigned e = 0; e < n; ++e ) {
            const U x = vget<U>(vn, e);
            U       v = 0;
            switch ( code ) {
            case 0:   // SSHR, USHR
            case 2:   // SSRA, USRA
            case 4:   // SRSHR, URSHR
            case 6: { // SRSRA, URSRA
                s128 w = widen(x, is_u);
                if ( code >= 4 ) w += static_cast<s128>(1) << (sh - 1);
                v = static_cast<U>(w >> sh);
                if ( code == 2 || code == 6 ) v = static_cast<U>(v + vget<U>(op.Vin(rf, 1), e));
            } break;
            case 8: { // SRI
                const U d = vget<U>(op.Vin(rf, 1), e);
                const U mask = sh >= bits ? U(0) : static_cast<U>(static_cast<U>(~U(0)) >> sh);
                const U shifted = sh >= bits ? U(0) : static_cast<U>(x >> sh);
                v = static_cast<U>((d & ~mask) | (shifted & mask));
            } break;
            case 10: // SHL, SLI
                if ( is_u ) {
                    const U d = vget<U>(op.Vin(rf, 1), e);
                    const U mask = static_cast<U>(static_cast<U>(~U(0)) << sh);
                    v = static_cast<U>((d & ~mask) | (static_cast<U>(x << sh) & mask));
                }
                else {
                    v = static_cast<U>(x << sh);
                }
                break;
            case 12: // SQSHLU: signed input, unsigned saturation
                v = static_cast<U>(satQ(widen(x, false) * (static_cast<s128>(1) << sh), bits, true, flags));
                break;
            case 14: // SQSHL, UQSHL
                v = static_cast<U>(satQ(widen(x, is_u) * (static_cast<s128>(1) << sh), bits, is_u, flags));
                break;
            default: break;
            }
            vset<U>(r, e, v);
        }
    });
    writeLow(op, rf, 0, r, out_bytes);
    op.fp_flags = flags;
}

// Across all lanes: SADDLV, UADDLV (c 3), SMAXV, UMAXV (c 10), SMINV, UMINV (c 26), ADDV (c 27). d = U.
inline void
simdAcross(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint8_t* vn = op.Vin(rf, 0);
    const bool     is_u = op.d;
    u128           out = 0;
    unsigned       out_bytes = 1u << op.a;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned n = op.b / sizeof(U);
        s128 v = widen(vget<U>(vn, 0), is_u);
        for ( unsigned e = 1; e < n; ++e ) {
            const s128 x = widen(vget<U>(vn, e), is_u);
            switch ( op.c ) {
            case 3: case 27: v += x; break;
            case 10: if ( x > v ) v = x; break;
            default: if ( x < v ) v = x; break;
            }
        }
        if ( op.c == 3 ) out_bytes = 2u << op.a;
        out = static_cast<u128>(v);
    });
    writeLow(op, rf, 0, &out, out_bytes);
}

// The modified-immediate move and logic. c: 0 MOVI (imm), 1 ORR, 2 BIC; imm = the 64-bit pattern, imm2 = pattern of the
// upper half is the same. Source Vd for ORR and BIC.
inline void
simdImm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    for ( uint32_t k = 0; k < op.b / 8; ++k ) {
        uint64_t v = op.imm;
        if ( op.c == 1 ) v |= vget<uint64_t>(op.Vin(rf, 0), k);
        else if ( op.c == 2 ) v = vget<uint64_t>(op.Vin(rf, 0), k) & ~v;
        vset<uint64_t>(r, k, v);
    }
    writeLow(op, rf, 0, r, op.b);
}

// DUP (element), scalar or vector. a = log2 element bytes, b = operation bytes, c = byte offset of the element.
inline void
simdDupElem(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    const uint32_t bytes = 1u << op.a;
    for ( uint32_t e = 0; e < op.b / bytes; ++e ) std::memcpy(r + e * bytes, op.Vin(rf, 0) + op.c, bytes);
    writeLow(op, rf, 0, r, op.b);
}

// DUP (general). a = log2 element bytes, b = operation bytes.
inline void
simdDupGp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    const uint32_t bytes = 1u << op.a;
    const uint64_t v = op.X(rf, 0);
    for ( uint32_t e = 0; e < op.b / bytes; ++e ) std::memcpy(r + e * bytes, &v, bytes);
    writeLow(op, rf, 0, r, op.b);
}

// INS (general). a = log2 element bytes, c = byte offset. Source Vd.
inline void
simdInsGp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    const uint64_t v = op.X(rf, 0);
    std::memcpy(r, op.Vin(rf, 0), sizeof(r));
    std::memcpy(r + op.c, &v, 1u << op.a);
    writeLow(op, rf, 0, r, sizeof(r));
}

// INS (element). a = log2 element bytes, c = destination byte offset, d = source byte offset. Sources Vd, Vn.
inline void
simdInsElem(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    std::memcpy(r, op.Vin(rf, 0), sizeof(r));
    std::memcpy(r + op.c, op.Vin(rf, 1) + op.d, 1u << op.a);
    writeLow(op, rf, 0, r, sizeof(r));
}

// SMOV, UMOV. a = log2 element bytes, c = byte offset, d: 1 sign-extend, 2 to a 32-bit register.
inline void
simdMovToGp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t v = 0;
    const uint32_t bytes = 1u << op.a;
    std::memcpy(&v, op.Vin(rf, 0) + op.c, bytes);
    if ( (op.d & 1) && bytes < 8 ) {
        const unsigned sh = 64 - bytes * 8;
        v = static_cast<uint64_t>(static_cast<int64_t>(v << sh) >> sh);
    }
    if ( op.d & 2 ) v &= 0xFFFFFFFFull;
    op.setX(rf, 0, v);
}

// UZP1, TRN1, ZIP1, UZP2, TRN2, ZIP2: c = the opcode of the encoding (1, 2, 3, 5, 6, 7).
inline void
simdPermute(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[16];
    const uint8_t* vn = op.Vin(rf, 0);
    const uint8_t* vm = op.Vin(rf, 1);
    const uint32_t bytes = 1u << op.a;
    const uint32_t n = op.b / bytes;
    const uint32_t part = (op.c >> 2) & 1;
    const auto     put = [&](uint32_t e, const uint8_t* src, uint32_t s) { std::memcpy(r + e * bytes, src + s * bytes, bytes); };
    switch ( op.c & 3 ) {
    case 1:
        for ( uint32_t e = 0; e < n / 2; ++e ) {
            put(e, vn, 2 * e + part);
            put(e + n / 2, vm, 2 * e + part);
        }
        break;
    case 2:
        for ( uint32_t e = 0; e < n / 2; ++e ) {
            put(2 * e, vn, 2 * e + part);
            put(2 * e + 1, vm, 2 * e + part);
        }
        break;
    default: {
        const uint32_t base = part ? n / 2 : 0;
        for ( uint32_t e = 0; e < n / 2; ++e ) {
            put(2 * e, vn, base + e);
            put(2 * e + 1, vm, base + e);
        }
    } break;
    }
    writeLow(op, rf, 0, r, op.b);
}

// TBL, TBX. Sources: the table registers in order, Vm, and Vd for TBX. c = table registers, d = TBX.
inline void
simdTable(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t  table[64];
    uint8_t  r[16];
    for ( uint32_t k = 0; k < op.c; ++k ) std::memcpy(table + 16 * k, op.Vin(rf, k), 16);
    const uint8_t* vm = op.Vin(rf, op.c);
    for ( uint32_t e = 0; e < op.b; ++e ) {
        const uint32_t idx = vm[e];
        if ( idx < 16 * op.c ) r[e] = table[idx];
        else r[e] = op.d ? op.Vin(rf, op.c + 1)[e] : uint8_t(0);
    }
    writeLow(op, rf, 0, r, op.b);
}

// EXT: b = operation bytes, c = byte position.
inline void
simdExt(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t c[32];
    uint8_t r[16];
    std::memcpy(c, op.Vin(rf, 0), op.b);
    std::memcpy(c + op.b, op.Vin(rf, 1), op.b);
    std::memcpy(r, c + op.c, op.b);
    writeLow(op, rf, 0, r, op.b);
}

} // namespace A64Simd
} // namespace Vanadis
} // namespace SST

#endif
