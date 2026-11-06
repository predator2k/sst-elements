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

// Micro-ops of the integer instructions of SVE2. SVE saturation does not touch FPSR.QC. a = log2 of the element bytes
// of the operation (the wider one for a widening or narrowing form), c = the operation code.

#ifndef _H_VANADIS_ARM64_SEM_SVE2
#define _H_VANADIS_ARM64_SEM_SVE2

#include "inst/arm64/sem_crypto.h"
#include "inst/arm64/sem_sve.h"

namespace SST {
namespace Vanadis {
namespace A64Sve2 {

using namespace A64Sve;

// Applies f to the narrow type N of a wide element size (a = 1, 2, 3).
template <typename F>
inline void
byWide(uint32_t log2_wide, F&& f)
{
    switch ( log2_wide ) {
    case 1: f(uint8_t {}, uint16_t {}); break;
    case 2: f(uint16_t {}, uint32_t {}); break;
    default: f(uint32_t {}, uint64_t {}); break;
    }
}

// ShiftSat of the Arm architecture: the whole element is the signed shift count, clamped to esize + 1.
template <typename U>
inline int
shiftSat(U y)
{
    using S = std::make_signed_t<U>;
    const int64_t limit = sizeof(U) * 8 + 1;
    const int64_t v = static_cast<S>(y);
    return static_cast<int>(v > limit ? limit : (v < -limit ? -limit : v));
}

// The predicated group 0x44..8000: c = bits 20..16.
template <typename U>
inline U
predElem(uint32_t code, U x, U y)
{
    const unsigned bits = sizeof(U) * 8;
    uint32_t       flags = 0;
    const bool     is_u = code & 1;
    const s128     ix = widen(x, is_u), iy = widen(y, is_u);
    switch ( code ) {
    case 0x02: case 0x03: return regShift(x, shiftSat(y), is_u, true, false, flags);
    case 0x06: case 0x07: return regShift(y, shiftSat(x), is_u, true, false, flags);
    case 0x08: case 0x09: return regShift(x, shiftSat(y), is_u, false, true, flags);
    case 0x0A: case 0x0B: return regShift(x, shiftSat(y), is_u, true, true, flags);
    case 0x0C: case 0x0D: return regShift(y, shiftSat(x), is_u, false, true, flags);
    case 0x0E: case 0x0F: return regShift(y, shiftSat(x), is_u, true, true, flags);
    case 0x10: case 0x11: return static_cast<U>((ix + iy) >> 1);
    case 0x12: case 0x13: return static_cast<U>((ix - iy) >> 1);
    case 0x14: case 0x15: return static_cast<U>((ix + iy + 1) >> 1);
    case 0x16: case 0x17: return static_cast<U>((iy - ix) >> 1);
    case 0x18: case 0x19: return static_cast<U>(satQ(ix + iy, bits, is_u, flags));
    case 0x1A: case 0x1B: return static_cast<U>(satQ(ix - iy, bits, is_u, flags));
    case 0x1C: return static_cast<U>(satQ(widen(x, false) + widen(y, true), bits, false, flags));   // SUQADD
    case 0x1D: return static_cast<U>(satQ(widen(x, true) + widen(y, false), bits, true, flags));    // USQADD
    case 0x1E: case 0x1F: return static_cast<U>(satQ(iy - ix, bits, is_u, flags));
    default: return x;
    }
}

// Sources Pg, Zdn, Zm.
inline void
predBinary(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 1), e);
            vset<U>(r, e, pbit(op.Vin(rf, 0), e * sizeof(U)) ? predElem<U>(op.c, x, vget<U>(op.Vin(rf, 2), e)) : x);
        }
    });
    writeZ(op, rf, r);
}

// ADDP (0x11), SMAXP (0x14), UMAXP (0x15), SMINP (0x16), UMINP (0x17). Sources Pg, Zdn, Zm.
inline void
predPairwise(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const bool is_u = op.c & 1;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const uint8_t* src = (e % 2 == 0) ? op.Vin(rf, 1) : op.Vin(rf, 2);
            const U a = vget<U>(src, e - e % 2), b = vget<U>(src, e - e % 2 + 1);
            U v = vget<U>(op.Vin(rf, 1), e);
            if ( pbit(op.Vin(rf, 0), e * sizeof(U)) ) {
                switch ( op.c ) {
                case 0x11: v = static_cast<U>(a + b); break;
                case 0x14: case 0x15: v = widen(a, is_u) >= widen(b, is_u) ? a : b; break;
                default: v = widen(a, is_u) <= widen(b, is_u) ? a : b; break;
                }
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// SADALP (d 0), UADALP (d 1): a = wide size. Sources Pg, Zda, Zn.
inline void
adalp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byWide(op.a, [&](auto nz, auto wz) {
        using N = decltype(nz);
        using W = decltype(wz);
        for ( unsigned e = 0; e < vl / sizeof(W); ++e ) {
            W v = vget<W>(op.Vin(rf, 1), e);
            if ( pbit(op.Vin(rf, 0), e * sizeof(W)) ) {
                const s128 sum = widen(vget<N>(op.Vin(rf, 2), 2 * e), op.d) + widen(vget<N>(op.Vin(rf, 2), 2 * e + 1), op.d);
                v = static_cast<W>(v + static_cast<W>(sum));
            }
            vset<W>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// Unary, merging: c: 0 URECPE, 1 URSQRTE, 8 SQABS, 9 SQNEG. Sources Pg, Zd, Zn.
inline void
predUnary2(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            U v = vget<U>(op.Vin(rf, 1), e);
            if ( pbit(op.Vin(rf, 0), e * sizeof(U)) ) {
                const U x = vget<U>(op.Vin(rf, 2), e);
                const uint32_t w = static_cast<uint32_t>(x);
                switch ( op.c ) {
                case 0: v = static_cast<U>((w >> 31) ? (A64Fp::recipEstimateTable((w >> 23) & 0x1FF) << 23) : 0xFFFFFFFFu); break;
                case 1: v = static_cast<U>((w >> 30) ? (A64Fp::rsqrtEstimateTable((w >> 23) & 0x1FF) << 23) : 0xFFFFFFFFu); break;
                case 8: {
                    const s128 s = widen(x, false);
                    v = static_cast<U>(satQ(s < 0 ? -s : s, bits, false, flags));
                } break;
                default: v = static_cast<U>(satQ(-widen(x, false), bits, false, flags)); break;
                }
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// Unpredicated multiplies. c: 0 MUL, 1 PMUL, 2 SMULH, 3 UMULH, 4 SQDMULH, 5 SQRDMULH. Sources Zn, Zm.
inline void
mulUnpred(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 0), e), y = vget<U>(op.Vin(rf, 1), e);
            U v;
            switch ( op.c ) {
            case 0: v = static_cast<U>(static_cast<uint64_t>(x) * y); break;
            case 1: v = polyMul(x, y); break;
            case 2: v = static_cast<U>((widen(x, false) * widen(y, false)) >> bits); break;
            case 3: v = static_cast<U>((static_cast<u128>(x) * static_cast<u128>(y)) >> bits); break;
            case 4: v = satDoubleMulHigh(x, y, false, flags); break;
            default: v = satDoubleMulHigh(x, y, true, flags); break;
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// EOR3 (0), BCAX (1), BSL (2), BSL1N (3), BSL2N (4), NBSL (5). Sources Zdn, Zm, Zk.
inline void
ternaryLogic(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    for ( unsigned k = 0; k < vl / 8; ++k ) {
        const uint64_t d = vget<uint64_t>(op.Vin(rf, 0), k), m = vget<uint64_t>(op.Vin(rf, 1), k), z = vget<uint64_t>(op.Vin(rf, 2), k);
        uint64_t v;
        switch ( op.c ) {
        case 0: v = d ^ m ^ z; break;
        case 1: v = d ^ (m & ~z); break;
        case 2: v = (d & z) | (m & ~z); break;
        case 3: v = (~d & z) | (m & ~z); break;
        case 4: v = (d & z) | (~m & ~z); break;
        default: v = ~((d & z) | (m & ~z)); break;
        }
        vset<uint64_t>(r, k, v);
    }
    writeZ(op, rf, r);
}

// XAR: sources Zdn, Zm; imm = rotation.
inline void
xar(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        const unsigned sh = static_cast<unsigned>(op.imm) % bits;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = static_cast<U>(vget<U>(op.Vin(rf, 0), e) ^ vget<U>(op.Vin(rf, 1), e));
            vset<U>(r, e, sh ? static_cast<U>((x >> sh) | static_cast<U>(x << (bits - sh))) : x);
        }
    });
    writeZ(op, rf, r);
}

// The widening group: a = wide size. c = bits 15..10 of the encoding >> 0. Sources Zn, Zm.
// Long: SADDL (0), UADDL (2), SSUBL (4), USUBL (6), SABDL (12), UABDL (14), with +1 for the top form.
// Wide: SADDW (16), UADDW (18), SSUBW (20), USUBW (22), +1 for the top form (Zn is wide).
// Multiply: SQDMULL (24), PMULL (26), SMULL (28), UMULL (30), +1 for the top form.
// Mixed: SADDLBT (32), SSUBLBT (34), SSUBLTB (35).
inline void
widening(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byWide(op.a, [&](auto nz, auto wz) {
        using N = decltype(nz);
        using W = decltype(wz);
        const unsigned wbits = sizeof(W) * 8;
        const uint32_t base = op.c & ~1u;
        const unsigned top = op.c & 1;
        for ( unsigned e = 0; e < vl / sizeof(W); ++e ) {
            const uint8_t* zn = op.Vin(rf, 0);
            const uint8_t* zm = op.Vin(rf, 1);
            W v = 0;
            switch ( base ) {
            case 0: case 2: case 4: case 6: case 12: case 14: {
                const bool is_u = base & 2;
                const s128 x = widen(vget<N>(zn, 2 * e + top), is_u), y = widen(vget<N>(zm, 2 * e + top), is_u);
                if ( base <= 2 ) v = static_cast<W>(x + y);
                else if ( base <= 6 ) v = static_cast<W>(x - y);
                else v = static_cast<W>(x > y ? x - y : y - x);
            } break;
            case 16: case 18: case 20: case 22: {
                const bool is_u = base & 2;
                const s128 x = widen(vget<W>(zn, e), is_u), y = widen(vget<N>(zm, 2 * e + top), is_u);
                v = static_cast<W>(base <= 18 ? x + y : x - y);
            } break;
            case 24:
                v = static_cast<W>(satQ(2 * widen(vget<N>(zn, 2 * e + top), false) * widen(vget<N>(zm, 2 * e + top), false), wbits, false, flags));
                break;
            case 26: {
                const N x = vget<N>(zn, 2 * e + top), y = vget<N>(zm, 2 * e + top);
                W p = 0;
                for ( unsigned k = 0; k < sizeof(N) * 8; ++k ) {
                    if ( (y >> k) & 1 ) p = static_cast<W>(p ^ (static_cast<W>(x) << k));
                }
                v = p;
            } break;
            case 28: v = static_cast<W>(widen(vget<N>(zn, 2 * e + top), false) * widen(vget<N>(zm, 2 * e + top), false)); break;
            case 30: v = static_cast<W>(widen(vget<N>(zn, 2 * e + top), true) * widen(vget<N>(zm, 2 * e + top), true)); break;
            case 32: v = static_cast<W>(widen(vget<N>(zn, 2 * e), false) + widen(vget<N>(zm, 2 * e + 1), false)); break;
            default:
                if ( op.c == 34 ) v = static_cast<W>(widen(vget<N>(zn, 2 * e), false) - widen(vget<N>(zm, 2 * e + 1), false));
                else v = static_cast<W>(widen(vget<N>(zn, 2 * e + 1), false) - widen(vget<N>(zm, 2 * e), false));
                break;
            }
            vset<W>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// PMULLB, PMULLT over 64-bit elements with 128-bit results. c = top. Sources Zn, Zm.
inline void
pmull64(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    for ( unsigned q = 0; q < vl / 16; ++q ) {
        const uint64_t x = vget<uint64_t>(op.Vin(rf, 0), 2 * q + op.c), y = vget<uint64_t>(op.Vin(rf, 1), 2 * q + op.c);
        u128 p = 0;
        for ( unsigned k = 0; k < 64; ++k ) {
            if ( (y >> k) & 1 ) p ^= static_cast<u128>(x) << k;
        }
        std::memcpy(r + q * 16, &p, 16);
    }
    writeZ(op, rf, r);
}

// SSHLLB (0), SSHLLT (1), USHLLB (2), USHLLT (3). a = wide size, imm = shift. Source Zn.
inline void
shiftLong(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byWide(op.a, [&](auto nz, auto wz) {
        using N = decltype(nz);
        using W = decltype(wz);
        for ( unsigned e = 0; e < vl / sizeof(W); ++e ) {
            const s128 x = widen(vget<N>(op.Vin(rf, 0), 2 * e + (op.c & 1)), op.c & 2);
            vset<W>(r, e, static_cast<W>(x * (static_cast<s128>(1) << op.imm)));
        }
    });
    writeZ(op, rf, r);
}

// The accumulating long and multiply-accumulate-long forms. a = wide size. Sources Zda, Zn, Zm.
// c: SABALB 0, SABALT 1, UABALB 2, UABALT 3; SMLALB 4, T 5; UMLALB 6, T 7; SMLSLB 8, T 9; UMLSLB 10, T 11;
// SQDMLALB 12, T 13; SQDMLSLB 14, T 15; SQDMLALBT 16; SQDMLSLBT 17. d & 1: Zm indexed by imm in each segment.
inline void
accumLong(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byWide(op.a, [&](auto nz, auto wz) {
        using N = decltype(nz);
        using W = decltype(wz);
        const unsigned wbits = sizeof(W) * 8;
        const unsigned narrow_per_seg = 16 / sizeof(N);
        for ( unsigned e = 0; e < vl / sizeof(W); ++e ) {
            const W d = vget<W>(op.Vin(rf, 0), e);
            const uint8_t* zn = op.Vin(rf, 1);
            const uint8_t* zm = op.Vin(rf, 2);
            const unsigned top = op.c & 1;
            unsigned n_idx = 2 * e + top, m_idx = 2 * e + top;
            if ( op.c == 16 || op.c == 17 ) {
                n_idx = 2 * e;
                m_idx = 2 * e + 1;
            }
            if ( op.d & 1 ) m_idx = (2 * e - (2 * e) % narrow_per_seg) + static_cast<unsigned>(op.imm);
            W v;
            if ( op.c <= 3 ) {
                const bool is_u = op.c & 2;
                const s128 x = widen(vget<N>(zn, n_idx), is_u), y = widen(vget<N>(zm, m_idx), is_u);
                v = static_cast<W>(d + static_cast<W>(x > y ? x - y : y - x));
            }
            else if ( op.c <= 11 ) {
                const bool is_u = op.c & 2;
                const bool is_sub = op.c >= 8;
                const s128 p = widen(vget<N>(zn, n_idx), is_u) * widen(vget<N>(zm, m_idx), is_u);
                v = static_cast<W>(is_sub ? d - static_cast<W>(p) : d + static_cast<W>(p));
            }
            else {
                const s128 p = satQ(2 * widen(vget<N>(zn, n_idx), false) * widen(vget<N>(zm, m_idx), false), wbits, false, flags);
                const bool is_sub = op.c == 14 || op.c == 15 || op.c == 17;
                v = static_cast<W>(satQ(is_sub ? widen(d, false) - p : widen(d, false) + p, wbits, false, flags));
            }
            vset<W>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// SMULLB (0), SMULLT (1), UMULLB (2), UMULLT (3), SQDMULLB (4), SQDMULLT (5) by element. a = wide size; imm = index in
// each segment. Sources Zn, Zm.
inline void
mulLongIndexed(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byWide(op.a, [&](auto nz, auto wz) {
        using N = decltype(nz);
        using W = decltype(wz);
        const unsigned per_seg = 16 / sizeof(N);
        for ( unsigned e = 0; e < vl / sizeof(W); ++e ) {
            const unsigned m_idx = (2 * e - (2 * e) % per_seg) + static_cast<unsigned>(op.imm);
            const bool is_u = op.c == 2 || op.c == 3;
            const s128 x = widen(vget<N>(op.Vin(rf, 0), 2 * e + (op.c & 1)), is_u);
            const s128 y = widen(vget<N>(op.Vin(rf, 1), m_idx), is_u);
            W v = op.c >= 4 ? static_cast<W>(satQ(2 * x * y, sizeof(W) * 8, false, flags)) : static_cast<W>(x * y);
            vset<W>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// The same-size forms by element. c: 0 MLA, 1 MLS, 2 MUL, 3 SQDMULH, 4 SQRDMULH, 5 SQRDMLAH, 6 SQRDMLSH. Unindexed
// SQRDMLAH, SQRDMLSH with d & 1. Sources Zn, Zm, Zda when accumulating. imm = index in each segment.
inline void
sameIndexed(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned per_seg = 16 / sizeof(U);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 0), e);
            const U y = vget<U>(op.Vin(rf, 1), (op.d & 1) ? e : (e - e % per_seg) + static_cast<unsigned>(op.imm));
            U v;
            switch ( op.c ) {
            case 0: v = static_cast<U>(vget<U>(op.Vin(rf, 2), e) + static_cast<uint64_t>(x) * y); break;
            case 1: v = static_cast<U>(vget<U>(op.Vin(rf, 2), e) - static_cast<uint64_t>(x) * y); break;
            case 2: v = static_cast<U>(static_cast<uint64_t>(x) * y); break;
            case 3: v = satDoubleMulHigh(x, y, false, flags); break;
            case 4: v = satDoubleMulHigh(x, y, true, flags); break;
            case 5: v = satRoundDoubleMulAccum(vget<U>(op.Vin(rf, 2), e), x, y, false, flags); break;
            default: v = satRoundDoubleMulAccum(vget<U>(op.Vin(rf, 2), e), x, y, true, flags); break;
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// SDOT, UDOT (c 0, 1), USDOT (c 2), SUDOT (c 3). a = wide size (2 or 3). d & 1: indexed by imm in each segment. Sources
// Zda, Zn, Zm.
inline void
dotZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const bool     n_u = op.c == 1 || op.c == 2;
    const bool     m_u = op.c == 1 || op.c == 3;
    if ( op.a == 2 ) {
        for ( unsigned g = 0; g < vl / 4; ++g ) {
            uint32_t sum = vget<uint32_t>(op.Vin(rf, 0), g);
            const unsigned mg = (op.d & 1) ? (g - g % 4) + static_cast<unsigned>(op.imm) : g;
            for ( unsigned k = 0; k < 4; ++k ) {
                const int32_t x = n_u ? static_cast<int32_t>(op.Vin(rf, 1)[g * 4 + k]) : static_cast<int8_t>(op.Vin(rf, 1)[g * 4 + k]);
                const int32_t y = m_u ? static_cast<int32_t>(op.Vin(rf, 2)[mg * 4 + k]) : static_cast<int8_t>(op.Vin(rf, 2)[mg * 4 + k]);
                sum += static_cast<uint32_t>(x * y);
            }
            vset<uint32_t>(r, g, sum);
        }
    }
    else {
        for ( unsigned g = 0; g < vl / 8; ++g ) {
            uint64_t sum = vget<uint64_t>(op.Vin(rf, 0), g);
            const unsigned mg = (op.d & 1) ? (g - g % 2) + static_cast<unsigned>(op.imm) : g;
            for ( unsigned k = 0; k < 4; ++k ) {
                const int64_t x = op.c == 1 ? static_cast<int64_t>(vget<uint16_t>(op.Vin(rf, 1), g * 4 + k)) : static_cast<int16_t>(vget<uint16_t>(op.Vin(rf, 1), g * 4 + k));
                const int64_t y = op.c == 1 ? static_cast<int64_t>(vget<uint16_t>(op.Vin(rf, 2), mg * 4 + k)) : static_cast<int16_t>(vget<uint16_t>(op.Vin(rf, 2), mg * 4 + k));
                sum += static_cast<uint64_t>(x * y);
            }
            vset<uint64_t>(r, g, sum);
        }
    }
    writeZ(op, rf, r);
}

// CDOT: a = wide size, d = rot; c & 1: indexed by imm. Sources Zda, Zn, Zm.
inline void
cdot(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byWide(op.a == 3 ? 3 : 2, [&](auto, auto wz) {
        using W = decltype(wz);
        using S = std::make_signed_t<W>;
        (void)sizeof(S);
    });
    const auto run = [&](auto nz, auto wz) {
        using N = decltype(nz);
        using W = decltype(wz);
        using SN = std::make_signed_t<N>;
        const unsigned per_seg_groups = 16 / sizeof(W);
        for ( unsigned g = 0; g < vl / sizeof(W); ++g ) {
            W sum = vget<W>(op.Vin(rf, 0), g);
            const unsigned mg = (op.c & 1) ? (g - g % per_seg_groups) + static_cast<unsigned>(op.imm) : g;
            for ( unsigned k = 0; k < 2; ++k ) {
                const int64_t nr = static_cast<SN>(vget<N>(op.Vin(rf, 1), 4 * g + 2 * k)), ni = static_cast<SN>(vget<N>(op.Vin(rf, 1), 4 * g + 2 * k + 1));
                const int64_t mr = static_cast<SN>(vget<N>(op.Vin(rf, 2), 4 * mg + 2 * k)), mi = static_cast<SN>(vget<N>(op.Vin(rf, 2), 4 * mg + 2 * k + 1));
                int64_t v;
                switch ( op.d ) {
                case 0: v = nr * mr - ni * mi; break;
                case 1: v = nr * mi + ni * mr; break;
                case 2: v = nr * mr + ni * mi; break;
                default: v = nr * mi - ni * mr; break;
                }
                sum = static_cast<W>(sum + static_cast<W>(v));
            }
            vset<W>(r, g, sum);
        }
    };
    if ( op.a == 2 ) run(uint8_t {}, uint32_t {});
    else run(uint16_t {}, uint64_t {});
    writeZ(op, rf, r);
}

// CMLA (c & 1 = 0), SQRDCMLAH (c & 1 = 1). d = rot; c & 2: indexed by imm (a pair index in each segment). Sources Zda, Zn, Zm.
inline void
cmlaZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned pairs_per_seg = 8 / sizeof(U);
        for ( unsigned e = 0; e < vl / sizeof(U) / 2; ++e ) {
            const unsigned me = (op.c & 2) ? (e - e % pairs_per_seg) + static_cast<unsigned>(op.imm) : e;
            const U nr = vget<U>(op.Vin(rf, 1), 2 * e), ni = vget<U>(op.Vin(rf, 1), 2 * e + 1);
            const U mr = vget<U>(op.Vin(rf, 2), 2 * me), mi = vget<U>(op.Vin(rf, 2), 2 * me + 1);
            const U dr = vget<U>(op.Vin(rf, 0), 2 * e), di = vget<U>(op.Vin(rf, 0), 2 * e + 1);
            // rot 0: dr += nr*mr, di += nr*mi; 90: dr -= ni*mi, di += ni*mr; 180: dr -= nr*mr, di -= nr*mi; 270: dr += ni*mi, di -= ni*mr.
            const bool use_i = op.d & 1;
            const U s = use_i ? ni : nr;
            const U p1 = use_i ? mi : mr;
            const U p2 = use_i ? mr : mi;
            const bool sub_r = op.d == 1 || op.d == 2, sub_i = op.d == 2 || op.d == 3;
            U out_r, out_i;
            if ( op.c & 1 ) {
                out_r = satRoundDoubleMulAccum(dr, s, p1, sub_r, flags);
                out_i = satRoundDoubleMulAccum(di, s, p2, sub_i, flags);
            }
            else {
                const U pr = static_cast<U>(static_cast<uint64_t>(s) * p1), pi = static_cast<U>(static_cast<uint64_t>(s) * p2);
                out_r = static_cast<U>(sub_r ? dr - pr : dr + pr);
                out_i = static_cast<U>(sub_i ? di - pi : di + pi);
            }
            vset<U>(r, 2 * e, out_r);
            vset<U>(r, 2 * e + 1, out_i);
        }
    });
    writeZ(op, rf, r);
}

// Narrowing by subtraction high or shift. a = wide size. d & 1 = top (Zd source 2 or 1 holds the bottom elements).
// c: ADDHN 0, RADDHN 1, SUBHN 2, RSUBHN 3 (sources Zn, Zm, then Zd);
//    SHRN 4, RSHRN 5, SQSHRUN 6, SQRSHRUN 7, SQSHRN 8, SQRSHRN 9, UQSHRN 10, UQRSHRN 11 (sources Zn, then Zd; imm = shift);
//    SQXTN 12, UQXTN 13, SQXTUN 14.
inline void
narrowing(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    const bool     top = op.d & 1;
    byWide(op.a, [&](auto nz, auto wz) {
        using N = decltype(nz);
        using W = decltype(wz);
        const unsigned nbits = sizeof(N) * 8;
        const unsigned sh = static_cast<unsigned>(op.imm);
        const bool binary = op.c <= 3;
        if ( top ) std::memcpy(r, op.Vin(rf, binary ? 2 : 1), vl);
        else std::memset(r, 0, vl);
        for ( unsigned e = 0; e < vl / sizeof(W); ++e ) {
            const W x = vget<W>(op.Vin(rf, 0), e);
            N v;
            if ( binary ) {
                const W y = vget<W>(op.Vin(rf, 1), e);
                W s = op.c <= 1 ? static_cast<W>(x + y) : static_cast<W>(x - y);
                if ( op.c & 1 ) s = static_cast<W>(s + (W(1) << (nbits - 1)));
                v = static_cast<N>(s >> nbits);
            }
            else if ( op.c >= 12 ) {
                if ( op.c == 12 ) v = static_cast<N>(satQ(widen(x, false), nbits, false, flags));
                else if ( op.c == 13 ) v = static_cast<N>(satQ(widen(x, true), nbits, true, flags));
                else v = static_cast<N>(satQ(widen(x, false), nbits, true, flags));
            }
            else {
                const bool rounding = op.c & 1;
                bool in_u = false, out_u = false;
                if ( op.c <= 5 ) in_u = out_u = true;
                else if ( op.c <= 7 ) out_u = true;
                else if ( op.c >= 10 ) in_u = out_u = true;
                s128 w = widen(x, in_u);
                if ( rounding ) w += static_cast<s128>(1) << (sh - 1);
                if ( op.c <= 5 ) v = static_cast<N>(w >> sh);
                else v = static_cast<N>(satQ(w >> sh, nbits, out_u, flags));
            }
            vset<N>(r, 2 * e + (top ? 1 : 0), v);
        }
    });
    writeZ(op, rf, r);
}

// SSRA, USRA, SRSRA, URSRA (c 0, 1, 2, 3): sources Zda, Zn; imm = shift. SRI (c 4), SLI (c 5): sources Zd, Zn.
inline void
shiftAccum(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const unsigned sh = static_cast<unsigned>(op.imm);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U d = vget<U>(op.Vin(rf, 0), e), x = vget<U>(op.Vin(rf, 1), e);
            U v;
            if ( op.c <= 3 ) {
                const bool is_u = op.c & 1;
                s128 w = widen(x, is_u);
                if ( op.c >= 2 ) w += static_cast<s128>(1) << (sh - 1);
                v = static_cast<U>(d + static_cast<U>(w >> sh));
            }
            else if ( op.c == 4 ) {
                const U mask = sh >= bits ? U(0) : static_cast<U>(static_cast<U>(~U(0)) >> sh);
                const U shifted = sh >= bits ? U(0) : static_cast<U>(x >> sh);
                v = static_cast<U>((d & ~mask) | (shifted & mask));
            }
            else {
                const U mask = static_cast<U>(static_cast<U>(~U(0)) << sh);
                v = static_cast<U>((d & ~mask) | (static_cast<U>(x << sh) & mask));
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// SABA (d 0), UABA (d 1). Sources Zda, Zn, Zm. SCLAMP, UCLAMP are not in SVE2.
inline void
abaZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const s128 x = widen(vget<U>(op.Vin(rf, 1), e), op.d), y = widen(vget<U>(op.Vin(rf, 2), e), op.d);
            vset<U>(r, e, static_cast<U>(vget<U>(op.Vin(rf, 0), e) + static_cast<U>(x > y ? x - y : y - x)));
        }
    });
    writeZ(op, rf, r);
}

// ADCLB (0), ADCLT (1), SBCLB (2), SBCLT (3). a = 2 or 3. Sources Zda, Zn, Zm.
inline void
adcLong(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        const unsigned top = op.c & 1;
        for ( unsigned e = 0; e < vl / sizeof(U) / 2; ++e ) {
            const U d = vget<U>(op.Vin(rf, 0), 2 * e);
            U n = vget<U>(op.Vin(rf, 1), 2 * e + top);
            if ( op.c >= 2 ) n = static_cast<U>(~n);
            const unsigned carry_in = vget<U>(op.Vin(rf, 2), 2 * e + 1) & 1;
            const u128 sum = static_cast<u128>(d) + static_cast<u128>(n) + carry_in;
            vset<U>(r, 2 * e, static_cast<U>(sum));
            vset<U>(r, 2 * e + 1, static_cast<U>((sum >> bits) & 1));
        }
    });
    writeZ(op, rf, r);
}

// CADD (c 0), SQCADD (c 1): d = rot. Sources Zdn, Zm.
inline void
caddZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        for ( unsigned e = 0; e < vl / sizeof(U) / 2; ++e ) {
            const s128 nr = widen(vget<U>(op.Vin(rf, 0), 2 * e), false), ni = widen(vget<U>(op.Vin(rf, 0), 2 * e + 1), false);
            const s128 mr = widen(vget<U>(op.Vin(rf, 1), 2 * e), false), mi = widen(vget<U>(op.Vin(rf, 1), 2 * e + 1), false);
            const s128 a = op.d ? nr + mi : nr - mi;
            const s128 b = op.d ? ni - mr : ni + mr;
            vset<U>(r, 2 * e, op.c ? static_cast<U>(satQ(a, bits, false, flags)) : static_cast<U>(a));
            vset<U>(r, 2 * e + 1, op.c ? static_cast<U>(satQ(b, bits, false, flags)) : static_cast<U>(b));
        }
    });
    writeZ(op, rf, r);
}

// EORBT (c 0), EORTB (c 1). Sources Zd, Zn, Zm.
inline void
eorInter(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        std::memcpy(r, op.Vin(rf, 0), vl);
        for ( unsigned e = 0; e < vl / sizeof(U) / 2; ++e ) {
            if ( op.c == 0 ) vset<U>(r, 2 * e, static_cast<U>(vget<U>(op.Vin(rf, 1), 2 * e) ^ vget<U>(op.Vin(rf, 2), 2 * e + 1)));
            else vset<U>(r, 2 * e + 1, static_cast<U>(vget<U>(op.Vin(rf, 1), 2 * e + 1) ^ vget<U>(op.Vin(rf, 2), 2 * e)));
        }
    });
    writeZ(op, rf, r);
}

// SMMLA, UMMLA, USMMLA over 128-bit segments: c 0, 1, 2. Sources Zda, Zn, Zm.
inline void
mmlaZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const bool     n_u = op.c == 1 || op.c == 2;
    const bool     m_u = op.c == 1;
    for ( unsigned q = 0; q + 16 <= vl; q += 16 ) {
        for ( unsigned i = 0; i < 2; ++i ) {
            for ( unsigned j = 0; j < 2; ++j ) {
                uint32_t sum = vget<uint32_t>(op.Vin(rf, 0) + q, 2 * i + j);
                for ( unsigned k = 0; k < 8; ++k ) {
                    const uint8_t a = op.Vin(rf, 1)[q + 8 * i + k], b = op.Vin(rf, 2)[q + 8 * j + k];
                    const int32_t x = n_u ? static_cast<int32_t>(a) : static_cast<int8_t>(a);
                    const int32_t y = m_u ? static_cast<int32_t>(b) : static_cast<int8_t>(b);
                    sum += static_cast<uint32_t>(x * y);
                }
                vset<uint32_t>(r + q, 2 * i + j, sum);
            }
        }
    }
    writeZ(op, rf, r);
}

// BEXT (c 0), BDEP (c 1), BGRP (c 2). Sources Zn, Zm.
inline void
bitPerm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 0), e), m = vget<U>(op.Vin(rf, 1), e);
            U v = 0;
            if ( op.c == 0 ) {
                unsigned k = 0;
                for ( unsigned b = 0; b < bits; ++b ) {
                    if ( (m >> b) & 1 ) {
                        if ( (x >> b) & 1 ) v = static_cast<U>(v | (U(1) << k));
                        ++k;
                    }
                }
            }
            else if ( op.c == 1 ) {
                unsigned k = 0;
                for ( unsigned b = 0; b < bits; ++b ) {
                    if ( (m >> b) & 1 ) {
                        if ( (x >> k) & 1 ) v = static_cast<U>(v | (U(1) << b));
                        ++k;
                    }
                }
            }
            else {
                unsigned k = 0;
                for ( unsigned b = 0; b < bits; ++b ) {
                    if ( (m >> b) & 1 ) {
                        if ( (x >> b) & 1 ) v = static_cast<U>(v | (U(1) << k));
                        ++k;
                    }
                }
                for ( unsigned b = 0; b < bits; ++b ) {
                    if ( !((m >> b) & 1) ) {
                        if ( (x >> b) & 1 ) v = static_cast<U>(v | (U(1) << k));
                        ++k;
                    }
                }
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// MATCH (c 0), NMATCH (c 1). Sources Pg, Zn, Zm. Integer destination NZCV.
inline void
matchZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf);
    std::memset(r, 0, vl / 8);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned per_seg = 16 / sizeof(U);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            if ( !pbit(op.Vin(rf, 0), e * sizeof(U)) ) continue;
            const unsigned seg = e - e % per_seg;
            const U x = vget<U>(op.Vin(rf, 1), e);
            bool found = false;
            for ( unsigned k = 0; k < per_seg; ++k ) found = found || vget<U>(op.Vin(rf, 2), seg + k) == x;
            pset(r, e * sizeof(U), op.c ? !found : found);
        }
    });
    writeP(op, rf, r);
    op.setX(rf, 0, predTest(op.Vin(rf, 0), r, vl, 1u << op.a));
}

// HISTCNT: sources Pg, Zn, Zm; a = 2 or 3.
inline void
histcnt(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned n = vl / sizeof(U);
        for ( unsigned e = 0; e < n; ++e ) {
            U count = 0;
            if ( pbit(op.Vin(rf, 0), e * sizeof(U)) ) {
                const U x = vget<U>(op.Vin(rf, 1), e);
                for ( unsigned k = 0; k <= e; ++k ) {
                    if ( pbit(op.Vin(rf, 0), k * sizeof(U)) && vget<U>(op.Vin(rf, 2), k) == x ) ++count;
                }
            }
            vset<U>(r, e, count);
        }
    });
    writeZ(op, rf, r);
}

// HISTSEG: sources Zn, Zm.
inline void
histseg(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    for ( unsigned q = 0; q + 16 <= vl; q += 16 ) {
        for ( unsigned e = 0; e < 16; ++e ) {
            uint8_t count = 0;
            for ( unsigned k = 0; k < 16; ++k ) {
                if ( op.Vin(rf, 1)[q + k] == op.Vin(rf, 0)[q + e] ) ++count;
            }
            r[q + e] = count;
        }
    }
    writeZ(op, rf, r);
}

// The segment crypto: c: 0 AESE, 1 AESD, 2 AESMC, 3 AESIMC, 4 SM4E, 5 SM4EKEY, 6 RAX1 (64-bit elements).
inline void
cryptoZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    for ( unsigned q = 0; q + 16 <= vl; q += 16 ) {
        const uint8_t* a = op.Vin(rf, 0) + q;
        const uint8_t* b = op.c == 2 || op.c == 3 ? nullptr : op.Vin(rf, 1) + q;
        uint8_t*       d = r + q;
        switch ( op.c ) {
        case 0:
        case 1: {
            uint8_t s[16];
            for ( unsigned k = 0; k < 16; ++k ) s[k] = a[k] ^ b[k];
            A64Crypto::aesShiftAndSub(s, d, op.c == 1);
        } break;
        case 2:
        case 3:
            for ( unsigned col = 0; col < 4; ++col ) {
                const uint8_t* c = a + col * 4;
                for ( unsigned row = 0; row < 4; ++row ) {
                    const uint8_t a0 = c[row], a1 = c[(row + 1) % 4], a2 = c[(row + 2) % 4], a3 = c[(row + 3) % 4];
                    d[col * 4 + row] = op.c == 2 ? static_cast<uint8_t>(A64Crypto::gfMul(a0, 2) ^ A64Crypto::gfMul(a1, 3) ^ a2 ^ a3)
                                                 : static_cast<uint8_t>(A64Crypto::gfMul(a0, 14) ^ A64Crypto::gfMul(a1, 11) ^ A64Crypto::gfMul(a2, 13) ^ A64Crypto::gfMul(a3, 9));
                }
            }
            break;
        case 4:
        case 5: {
            uint32_t x[4], y[4], o[4];
            std::memcpy(x, a, 16);
            std::memcpy(y, b, 16);
            A64Crypto::sm4Rounds(x, y, o, op.c == 5);
            std::memcpy(d, o, 16);
        } break;
        default:
            for ( unsigned k = 0; k < 2; ++k ) {
                vset<uint64_t>(d, k, vget<uint64_t>(a, k) ^ A64Crypto::rol64(vget<uint64_t>(b, k), 1));
            }
            break;
        }
    }
    writeZ(op, rf, r);
}

} // namespace A64Sve2
} // namespace Vanadis
} // namespace SST

#endif
