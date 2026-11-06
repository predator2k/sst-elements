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

// Micro-ops of the integer, predicate and permute instructions of SVE. The vector length is the width of the
// floating-point register class. Z<n> is its register n and P<n> register 32 + n, of which the low VL / 8 bytes hold
// one bit per vector byte. a = log2 of the element bytes, c = the operation code. A predicate operand comes first.

#ifndef _H_VANADIS_ARM64_SEM_SVE
#define _H_VANADIS_ARM64_SEM_SVE

#include "inst/arm64/sem_simd_int.h"

namespace SST {
namespace Vanadis {
namespace A64Sve {

using namespace A64Simd;

constexpr unsigned MAX_VL_BYTES = 256;

inline unsigned
vlOf(VanadisRegisterFile* rf)
{
    return rf->getFPRegWidth();
}

inline bool
pbit(const uint8_t* p, unsigned byte_index)
{
    return (p[byte_index / 8] >> (byte_index % 8)) & 1;
}

inline void
pset(uint8_t* p, unsigned byte_index, bool v)
{
    if ( v ) p[byte_index / 8] |= static_cast<uint8_t>(1u << (byte_index % 8));
}

// PredTest of the Arm architecture over elements of the given bytes: NZCV in bits 31..28.
inline uint64_t
predTest(const uint8_t* mask, const uint8_t* result, unsigned vl, unsigned ebytes)
{
    bool     n = false, seen = false, any = false, last = false;
    for ( unsigned b = 0; b < vl; b += ebytes ) {
        if ( !pbit(mask, b) ) continue;
        const bool r = pbit(result, b);
        if ( !seen ) {
            n = r;
            seen = true;
        }
        any = any || r;
        last = r;
    }
    return (n ? A64Flag::N : 0) | (any ? 0 : A64Flag::Z) | (last ? 0 : A64Flag::C);
}

// Writes a whole vector register, or a predicate with the bytes above VL / 8 zero.
inline void
writeZ(VanadisArm64Inst& op, VanadisRegisterFile* rf, const uint8_t* r, uint16_t pos = 0)
{
    std::memcpy(op.Vout(rf, pos), r, vlOf(rf));
}

inline void
writeP(VanadisArm64Inst& op, VanadisRegisterFile* rf, const uint8_t* r, uint16_t pos = 0)
{
    writeLow(op, rf, pos, r, vlOf(rf) / 8);
}

// One element of the predicated binary group; code is bits 20..16 of the encoding.
template <typename U>
inline U
predBinElem(uint32_t code, U x, U y)
{
    using S = std::make_signed_t<U>;
    const unsigned bits = sizeof(U) * 8;
    const bool     is_u = code & 1;
    switch ( code ) {
    case 0x00: return static_cast<U>(x + y);
    case 0x01: return static_cast<U>(x - y);
    case 0x03: return static_cast<U>(y - x);
    case 0x08: case 0x09: return widen(x, is_u) >= widen(y, is_u) ? x : y;
    case 0x0A: case 0x0B: return widen(x, is_u) <= widen(y, is_u) ? x : y;
    case 0x0C: case 0x0D: {
        const s128 a = widen(x, is_u), b = widen(y, is_u);
        return static_cast<U>(a > b ? a - b : b - a);
    }
    case 0x10: return static_cast<U>(static_cast<uint64_t>(x) * static_cast<uint64_t>(y));
    case 0x12: return static_cast<U>((widen(x, false) * widen(y, false)) >> bits);
    case 0x13: return static_cast<U>((static_cast<u128>(x) * static_cast<u128>(y)) >> bits);
    case 0x14: return y == 0 ? U(0) : static_cast<U>(widen(x, false) / widen(y, false));
    case 0x15: return y == 0 ? U(0) : static_cast<U>(x / y);
    case 0x16: return x == 0 ? U(0) : static_cast<U>(widen(y, false) / widen(x, false));
    case 0x17: return x == 0 ? U(0) : static_cast<U>(y / x);
    case 0x18: return static_cast<U>(x | y);
    case 0x19: return static_cast<U>(x ^ y);
    case 0x1A: return static_cast<U>(x & y);
    case 0x1B: return static_cast<U>(x & ~y);
    default: (void)sizeof(S); return x;
    }
}

// ADD, SUB, ... (predicated). Sources Pg, Zdn, Zm.
inline void
predBin(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint8_t* pg = op.Vin(rf, 0);
    const uint8_t* zn = op.Vin(rf, 1);
    const uint8_t* zm = op.Vin(rf, 2);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(zn, e);
            vset<U>(r, e, pbit(pg, e * sizeof(U)) ? predBinElem<U>(op.c, x, vget<U>(zm, e)) : x);
        }
    });
    writeZ(op, rf, r);
}

// MLA (c 0), MLS (c 1): sources Pg, Zda, Zn, Zm. MAD (c 2), MSB (c 3): sources Pg, Zdn, Zm, Za.
inline void
predMulAdd(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint8_t* pg = op.Vin(rf, 0);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U d = vget<U>(op.Vin(rf, 1), e), n = vget<U>(op.Vin(rf, 2), e), m = vget<U>(op.Vin(rf, 3), e);
            U v = d;
            if ( pbit(pg, e * sizeof(U)) ) {
                switch ( op.c ) {
                case 0: v = static_cast<U>(d + static_cast<uint64_t>(n) * m); break;
                case 1: v = static_cast<U>(d - static_cast<uint64_t>(n) * m); break;
                case 2: v = static_cast<U>(m + static_cast<uint64_t>(d) * n); break;
                default: v = static_cast<U>(m - static_cast<uint64_t>(d) * n); break;
                }
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

template <typename U>
inline unsigned
countLeadingSign(U x)
{
    const unsigned bits = sizeof(U) * 8;
    const bool     top = (x >> (bits - 1)) & 1;
    unsigned       count = 0;
    for ( int k = bits - 2; k >= 0 && (((x >> k) & 1) != 0) == top; --k ) ++count;
    return count;
}

template <typename U>
inline unsigned
countLeadingZero(U x)
{
    unsigned count = 0;
    for ( int k = sizeof(U) * 8 - 1; k >= 0 && !((x >> k) & 1); --k ) ++count;
    return count;
}

template <typename U>
inline U
reverseBits(U x)
{
    U r = 0;
    for ( unsigned k = 0; k < sizeof(U) * 8; ++k ) {
        if ( (x >> k) & 1 ) r = static_cast<U>(r | (U(1) << (sizeof(U) * 8 - 1 - k)));
    }
    return r;
}

// ABS, NEG, SXT, UXT, CLS, CLZ, CNT, CNOT, FABS, FNEG, NOT, RBIT (predicated, merging). c = bits 20..16, RBIT 0x20.
// Sources Pg, Zd, Zn.
inline void
predUnary(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint8_t* pg = op.Vin(rf, 0);
    const uint8_t* zd = op.Vin(rf, 1);
    const uint8_t* zn = op.Vin(rf, 2);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        using S = std::make_signed_t<U>;
        const U sign_bit = static_cast<U>(U(1) << (sizeof(U) * 8 - 1));
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(zn, e);
            U v = vget<U>(zd, e);
            if ( pbit(pg, e * sizeof(U)) ) {
                switch ( op.c ) {
                case 0x10: v = static_cast<U>(static_cast<int64_t>(static_cast<int8_t>(x))); break;
                case 0x11: v = static_cast<U>(static_cast<uint8_t>(x)); break;
                case 0x12: v = static_cast<U>(static_cast<int64_t>(static_cast<int16_t>(x))); break;
                case 0x13: v = static_cast<U>(static_cast<uint16_t>(x)); break;
                case 0x14: v = static_cast<U>(static_cast<int64_t>(static_cast<int32_t>(x))); break;
                case 0x15: v = static_cast<U>(static_cast<uint32_t>(x)); break;
                case 0x16: v = static_cast<S>(x) < 0 ? static_cast<U>(U(0) - x) : x; break;
                case 0x17: v = static_cast<U>(U(0) - x); break;
                case 0x18: v = static_cast<U>(countLeadingSign(x)); break;
                case 0x19: v = static_cast<U>(countLeadingZero(x)); break;
                case 0x1A: v = static_cast<U>(__builtin_popcountll(x)); break;
                case 0x1B: v = x == 0 ? U(1) : U(0); break;
                case 0x1C: v = static_cast<U>(x & ~sign_bit); break;
                case 0x1D: v = static_cast<U>(x ^ sign_bit); break;
                case 0x1E: v = static_cast<U>(~x); break;
                default: v = reverseBits(x); break;
                }
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// REVB, REVH, REVW (predicated): the bytes or halfwords of each element reversed. d = log2 of the swap unit. Sources
// Pg, Zd, Zn.
inline void
predRevInElem(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const unsigned ebytes = 1u << op.a, unit = 1u << op.d;
    std::memcpy(r, op.Vin(rf, 1), vl);
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        if ( !pbit(op.Vin(rf, 0), e * ebytes) ) continue;
        const uint8_t* s = op.Vin(rf, 2) + e * ebytes;
        for ( unsigned k = 0; k < ebytes / unit; ++k ) {
            std::memcpy(r + e * ebytes + k * unit, s + ebytes - (k + 1) * unit, unit);
        }
    }
    writeZ(op, rf, r);
}

// Reductions. c: 0 SADDV, 1 UADDV, 8 SMAXV, 9 UMAXV, 10 SMINV, 11 UMINV, 0x18 ORV, 0x19 EORV, 0x1A ANDV. Sources Pg, Zn.
inline void
reduceInt(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned vl = vlOf(rf);
    const uint8_t* pg = op.Vin(rf, 0);
    const uint8_t* zn = op.Vin(rf, 1);
    uint64_t       out = 0;
    unsigned       out_bytes = 1u << op.a;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        const bool     is_u = op.c & 1;
        const U        ones = static_cast<U>(~U(0));
        s128 sum = 0;
        s128 best = 0;
        switch ( op.c ) {
        case 8: best = signedMin(bits); break;
        case 9: best = 0; break;
        case 10: best = signedMax(bits); break;
        case 11: best = static_cast<s128>(ones); break;
        default: break;
        }
        U bitwise = op.c == 0x1A ? ones : U(0);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            if ( !pbit(pg, e * sizeof(U)) ) continue;
            const U x = vget<U>(zn, e);
            switch ( op.c ) {
            case 0: case 1: sum += widen(x, is_u); break;
            case 8: case 9: if ( widen(x, is_u) > best ) best = widen(x, is_u); break;
            case 10: case 11: if ( widen(x, is_u) < best ) best = widen(x, is_u); break;
            case 0x18: bitwise = static_cast<U>(bitwise | x); break;
            case 0x19: bitwise = static_cast<U>(bitwise ^ x); break;
            default: bitwise = static_cast<U>(bitwise & x); break;
            }
        }
        if ( op.c <= 1 ) {
            out = static_cast<uint64_t>(sum);
            out_bytes = 8;
        }
        else if ( op.c >= 0x18 ) {
            out = bitwise;
        }
        else {
            out = static_cast<uint64_t>(static_cast<u128>(best));
        }
    });
    writeLow(op, rf, 0, &out, out_bytes);
}

// MOVPRFX (predicated). d = merging. Sources Pg, Zd, Zn. MOVPRFX (unpredicated) uses copyZ.
inline void
movprfxPred(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        const uint8_t* src = pbit(op.Vin(rf, 0), e * ebytes) ? op.Vin(rf, 2) : (op.d ? op.Vin(rf, 1) : nullptr);
        if ( src ) std::memcpy(r + e * ebytes, src + e * ebytes, ebytes);
        else std::memset(r + e * ebytes, 0, ebytes);
    }
    writeZ(op, rf, r);
}

inline void
copyZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    writeZ(op, rf, op.Vin(rf, 0));
}

// Immediate logic. c: 0 ORR, 1 EOR, 2 AND, 3 DUPM; imm = 64-bit pattern. Source Zdn except DUPM. Also DUP (immediate)
// with a pattern that repeats by element.
inline void
logicImm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    for ( unsigned k = 0; k < vl / 8; ++k ) {
        uint64_t v = op.imm;
        if ( op.c == 0 ) v |= vget<uint64_t>(op.Vin(rf, 0), k);
        else if ( op.c == 1 ) v ^= vget<uint64_t>(op.Vin(rf, 0), k);
        else if ( op.c == 2 ) v &= vget<uint64_t>(op.Vin(rf, 0), k);
        vset<uint64_t>(r, k, v);
    }
    writeZ(op, rf, r);
}

// Immediate arithmetic, unpredicated. c: 0 ADD, 1 SUB, 3 SUBR, 4 SQADD, 5 UQADD, 6 SQSUB, 7 UQSUB, 8 SMAX, 9 UMAX,
// 10 SMIN, 11 UMIN, 16 MUL. imm = the operand (sign-extended where signed). Source Zdn.
inline void
arithImm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        const U        y = static_cast<U>(op.imm);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 0), e);
            U       v = x;
            switch ( op.c ) {
            case 0: v = static_cast<U>(x + y); break;
            case 1: v = static_cast<U>(x - y); break;
            case 3: v = static_cast<U>(y - x); break;
            case 4: v = static_cast<U>(satQ(widen(x, false) + static_cast<s128>(y), bits, false, flags)); break;
            case 5: v = static_cast<U>(satQ(widen(x, true) + static_cast<s128>(y), bits, true, flags)); break;
            case 6: v = static_cast<U>(satQ(widen(x, false) - static_cast<s128>(y), bits, false, flags)); break;
            case 7: v = static_cast<U>(satQ(widen(x, true) - static_cast<s128>(y), bits, true, flags)); break;
            case 8: v = widen(x, false) >= widen(y, false) ? x : y; break;
            case 9: v = widen(x, true) >= widen(y, true) ? x : y; break;
            case 10: v = widen(x, false) <= widen(y, false) ? x : y; break;
            case 11: v = widen(x, true) <= widen(y, true) ? x : y; break;
            default: v = static_cast<U>(static_cast<uint64_t>(x) * static_cast<uint64_t>(y)); break;
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
    (void)flags;   // SVE saturation does not touch FPSR.QC.
}

// Unpredicated three-register arithmetic. c: 0 ADD, 1 SUB, 4 SQADD, 5 UQADD, 6 SQSUB, 7 UQSUB; logic: 8 AND, 9 ORR,
// 10 EOR, 11 BIC. Sources Zn, Zm.
inline void
unpredArith(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint32_t       flags = 0;
    byElem(op.c >= 8 ? 3 : op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 0), e), y = vget<U>(op.Vin(rf, 1), e);
            U       v = 0;
            switch ( op.c ) {
            case 0: v = static_cast<U>(x + y); break;
            case 1: v = static_cast<U>(x - y); break;
            case 4: v = static_cast<U>(satQ(widen(x, false) + widen(y, false), bits, false, flags)); break;
            case 5: v = static_cast<U>(satQ(widen(x, true) + widen(y, true), bits, true, flags)); break;
            case 6: v = static_cast<U>(satQ(widen(x, false) - widen(y, false), bits, false, flags)); break;
            case 7: v = static_cast<U>(satQ(widen(x, true) - widen(y, true), bits, true, flags)); break;
            case 8: v = static_cast<U>(x & y); break;
            case 9: v = static_cast<U>(x | y); break;
            case 10: v = static_cast<U>(x ^ y); break;
            default: v = static_cast<U>(x & ~y); break;
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// ASR, LSR, LSL by a count: 0, 1, 3 in code; counts at or above the element size give 0 or the sign.
template <typename U>
inline U
shiftBy(uint32_t code, U x, uint64_t count)
{
    using S = std::make_signed_t<U>;
    const unsigned bits = sizeof(U) * 8;
    switch ( code ) {
    case 0: return count >= bits ? static_cast<U>(static_cast<S>(x) >> (bits - 1)) : static_cast<U>(static_cast<S>(x) >> count);
    case 1: return count >= bits ? U(0) : static_cast<U>(x >> count);
    default: return count >= bits ? U(0) : static_cast<U>(x << count);
    }
}

// Predicated shift by vector. c: 0 ASR, 1 LSR, 3 LSL, 4 ASRR, 5 LSRR, 7 LSLR. d = wide (the counts are 64-bit elements).
// Sources Pg, Zdn, Zm.
inline void
predShiftVec(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 1), e);
            U       v = x;
            if ( pbit(op.Vin(rf, 0), e * sizeof(U)) ) {
                if ( op.d ) {
                    v = shiftBy<U>(op.c, x, vget<uint64_t>(op.Vin(rf, 2), e * sizeof(U) / 8));
                }
                else {
                    const U y = vget<U>(op.Vin(rf, 2), e);
                    if ( op.c >= 4 ) v = shiftBy<U>(op.c - 4, y, x);
                    else v = shiftBy<U>(op.c, x, y);
                }
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// Predicated shift by immediate. c: 0 ASR, 1 LSR, 3 LSL, 4 ASRD, 6 SQSHL, 7 UQSHL, 12 SRSHR, 13 URSHR, 15 SQSHLU.
// imm = the count. Sources Pg, Zdn.
inline void
predShiftImm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const unsigned sh = static_cast<unsigned>(op.imm);
    uint32_t       flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 1), e);
            U       v = x;
            if ( pbit(op.Vin(rf, 0), e * sizeof(U)) ) {
                switch ( op.c ) {
                case 0: case 1: case 3: v = shiftBy<U>(op.c, x, sh); break;
                case 4: {
                    s128 w = widen(x, false);
                    if ( w < 0 ) w += (static_cast<s128>(1) << sh) - 1;
                    v = static_cast<U>(w >> sh);
                } break;
                case 6: v = static_cast<U>(satQ(widen(x, false) * (static_cast<s128>(1) << sh), bits, false, flags)); break;
                case 7: v = static_cast<U>(satQ(widen(x, true) * (static_cast<s128>(1) << sh), bits, true, flags)); break;
                case 12: v = static_cast<U>((widen(x, false) + (static_cast<s128>(1) << (sh - 1))) >> sh); break;
                case 13: v = static_cast<U>((widen(x, true) + (static_cast<s128>(1) << (sh - 1))) >> sh); break;
                default: v = static_cast<U>(satQ(widen(x, false) * (static_cast<s128>(1) << sh), bits, true, flags)); break;
                }
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// Unpredicated shift. c: 0 ASR, 1 LSR, 3 LSL. d = 1: wide, the counts are the 64-bit elements of Zm (source 1);
// otherwise imm is the count.
inline void
unpredShift(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const uint64_t count = op.d ? vget<uint64_t>(op.Vin(rf, 1), e * sizeof(U) / 8) : op.imm;
            vset<U>(r, e, shiftBy<U>(op.c, vget<U>(op.Vin(rf, 0), e), count));
        }
    });
    writeZ(op, rf, r);
}

// Integer compares writing a predicate. c: 0 EQ, 1 NE, 2 GE, 3 GT, 4 LT, 5 LE, 6 HS, 7 HI, 8 LO, 9 LS. d: 1 = wide
// (Zm has 64-bit elements), 2 = immediate in imm. Integer destination NZCV. Sources Pg, Zn, Zm.
inline void
compareInt(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf);
    const uint8_t* pg = op.Vin(rf, 0);
    std::memset(r, 0, vl / 8);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const bool is_u = op.c >= 6;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            if ( !pbit(pg, e * sizeof(U)) ) continue;
            const s128 x = widen(vget<U>(op.Vin(rf, 1), e), is_u);
            s128 y;
            if ( op.d & 2 ) {
                y = is_u ? static_cast<s128>(op.imm) : static_cast<s128>(static_cast<int64_t>(op.imm));
            }
            else if ( op.d & 1 ) {
                const uint64_t w = vget<uint64_t>(op.Vin(rf, 2), e * sizeof(U) / 8);
                y = widen(w, is_u);
            }
            else {
                y = widen(vget<U>(op.Vin(rf, 2), e), is_u);
            }
            bool v = false;
            switch ( op.c ) {
            case 0: v = x == y; break;
            case 1: v = x != y; break;
            case 2: case 6: v = x >= y; break;
            case 3: case 7: v = x > y; break;
            case 4: case 8: v = x < y; break;
            default: v = x <= y; break;
            }
            pset(r, e * sizeof(U), v);
        }
    });
    writeP(op, rf, r);
    op.setX(rf, 0, predTest(pg, r, vl, 1u << op.a));
}

// Predicate logic: c = (op << 2) | (o2 << 1) | o3 of the encoding: AND, BIC, EOR, SEL, ORR, ORN, NOR, NAND. d = sets
// flags. Sources Pg, Pn, Pm. NZCV is integer destination 0 when d.
inline void
predLogic(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned pl = vlOf(rf) / 8;
    const uint8_t* g = op.Vin(rf, 0);
    const uint8_t* n = op.Vin(rf, 1);
    const uint8_t* m = op.Vin(rf, 2);
    for ( unsigned k = 0; k < pl; ++k ) {
        uint8_t v = 0;
        switch ( op.c ) {
        case 0: v = g[k] & n[k] & m[k]; break;
        case 1: v = g[k] & n[k] & ~m[k]; break;
        case 2: v = g[k] & (n[k] ^ m[k]); break;
        case 3: v = (g[k] & n[k]) | (~g[k] & m[k]); break;
        case 4: v = g[k] & (n[k] | m[k]); break;
        case 5: v = g[k] & (n[k] | ~m[k]); break;
        case 6: v = g[k] & static_cast<uint8_t>(~(n[k] | m[k])); break;
        default: v = g[k] & static_cast<uint8_t>(~(n[k] & m[k])); break;
        }
        r[k] = v;
    }
    writeP(op, rf, r);
    if ( op.d ) op.setX(rf, 0, predTest(g, r, pl * 8, 1));
}

// PTRUE, PTRUES, PFALSE: imm = active element count; a = element size. d = sets flags.
inline void
predInit(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    std::memset(r, 0, vl / 8);
    for ( uint64_t e = 0; e < op.imm; ++e ) pset(r, static_cast<unsigned>(e * ebytes), true);
    writeP(op, rf, r);
    if ( op.d ) op.setX(rf, 0, predTest(r, r, vl, ebytes));
}

inline void
copyP(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    writeP(op, rf, op.Vin(rf, 0));
}

// RDFFR (predicated): sources Pg, FFR. d = sets flags. WRFFR, SETFFR and unpredicated RDFFR use copyP or predInit.
inline void
rdffrPred(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned pl = vlOf(rf) / 8;
    for ( unsigned k = 0; k < pl; ++k ) r[k] = op.Vin(rf, 0)[k] & op.Vin(rf, 1)[k];
    writeP(op, rf, r);
    if ( op.d ) op.setX(rf, 0, predTest(op.Vin(rf, 0), r, pl * 8, 1));
}

// PTEST: sources Pg, Pn. Integer destination NZCV.
inline void
ptest(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    op.setX(rf, 0, predTest(op.Vin(rf, 0), op.Vin(rf, 1), vlOf(rf), 1));
}

// PFIRST: sources Pg, Pdn. Destinations Pdn, NZCV.
inline void
pfirst(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf);
    std::memcpy(r, op.Vin(rf, 1), vl / 8);
    for ( unsigned b = 0; b < vl; ++b ) {
        if ( pbit(op.Vin(rf, 0), b) ) {
            pset(r, b, true);
            break;
        }
    }
    writeP(op, rf, r);
    op.setX(rf, 0, predTest(op.Vin(rf, 0), r, vl, 1));
}

// PNEXT: sources Pg, Pdn. Destinations Pdn, NZCV. a = element size.
inline void
pnext(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    const uint8_t* g = op.Vin(rf, 0);
    const uint8_t* dn = op.Vin(rf, 1);
    std::memset(r, 0, vl / 8);
    // The first active element after the last true element of Pdn.
    int start = -1;
    for ( unsigned b = 0; b < vl; b += ebytes ) {
        if ( pbit(dn, b) ) start = static_cast<int>(b);
    }
    for ( unsigned b = start < 0 ? 0 : start + ebytes; b < vl; b += ebytes ) {
        if ( pbit(g, b) ) {
            pset(r, b, true);
            break;
        }
    }
    writeP(op, rf, r);
    op.setX(rf, 0, predTest(g, r, vl, ebytes));
}

// BRKA, BRKB (c 0, 1). Sources Pg, Pn; with merging (d & 1) also Pd. d & 2 = sets flags.
inline void
brk(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf);
    const uint8_t* g = op.Vin(rf, 0);
    const uint8_t* n = op.Vin(rf, 1);
    bool           broken = false;
    if ( op.d & 1 ) std::memcpy(r, op.Vin(rf, 2), vl / 8);
    else std::memset(r, 0, vl / 8);
    for ( unsigned b = 0; b < vl; ++b ) {
        if ( !pbit(g, b) ) continue;
        const bool set = pbit(n, b);
        bool v;
        if ( op.c == 0 ) {
            v = !broken;
            broken = broken || set;
        }
        else {
            broken = broken || set;
            v = !broken;
        }
        r[b / 8] = static_cast<uint8_t>((r[b / 8] & ~(1u << (b % 8))) | (v ? (1u << (b % 8)) : 0u));
    }
    writeP(op, rf, r);
    if ( op.d & 2 ) op.setX(rf, 0, predTest(g, r, vl, 1));
}

// BRKN: sources Pg, Pn, Pdm. d & 2 = sets flags. If the last active element of Pn is true, Pdm unchanged, else zero.
inline void
brkn(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf);
    const uint8_t* g = op.Vin(rf, 0);
    bool           last = false;
    for ( unsigned b = 0; b < vl; ++b ) {
        if ( pbit(g, b) ) last = pbit(op.Vin(rf, 1), b);
    }
    if ( last ) std::memcpy(r, op.Vin(rf, 2), vl / 8);
    else std::memset(r, 0, vl / 8);
    writeP(op, rf, r);
    if ( op.d & 2 ) {
        uint8_t all[MAX_VL_BYTES / 8];
        std::memset(all, 0xFF, vl / 8);
        op.setX(rf, 0, predTest(all, r, vl, 1));
    }
}

// BRKPA, BRKPB (c 0, 1). Sources Pg, Pn, Pm. d & 2 = sets flags.
inline void
brkp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf);
    const uint8_t* g = op.Vin(rf, 0);
    bool           last = true;
    for ( unsigned b = 0; b < vl; ++b ) {
        if ( pbit(g, b) ) last = pbit(op.Vin(rf, 1), b);
    }
    std::memset(r, 0, vl / 8);
    for ( unsigned b = 0; b < vl; ++b ) {
        if ( !pbit(g, b) ) continue;
        const bool m = pbit(op.Vin(rf, 2), b);
        if ( op.c == 0 ) {
            pset(r, b, last);
            last = last && !m;
        }
        else {
            last = last && !m;
            pset(r, b, last);
        }
    }
    writeP(op, rf, r);
    if ( op.d & 2 ) op.setX(rf, 0, predTest(g, r, vl, 1));
}

// CNTP: sources Pg, Pn; integer destination Rd. a = element size.
inline void
cntp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    uint64_t       count = 0;
    for ( unsigned b = 0; b < vl; b += ebytes ) {
        if ( pbit(op.Vin(rf, 0), b) && pbit(op.Vin(rf, 1), b) ) ++count;
    }
    op.setX(rf, 0, count);
}

// The saturating scalar forms of INC, DEC. c: 0 add, 1 subtract, 2 SQADD, 3 SQSUB, 4 UQADD, 5 UQSUB. d & 1: 32-bit
// saturation; the result of a signed 32-bit form is sign-extended, of an unsigned one zero-extended.
inline uint64_t
countedScalar(uint64_t x, uint64_t count, uint32_t code, bool narrow)
{
    uint32_t flags = 0;
    const unsigned bits = narrow ? 32 : 64;
    switch ( code ) {
    case 0: return x + count;
    case 1: return x - count;
    case 2: case 3: {
        const s128 v = narrow ? static_cast<s128>(static_cast<int32_t>(x)) : static_cast<s128>(static_cast<int64_t>(x));
        const s128 s = satQ(code == 2 ? v + static_cast<s128>(count) : v - static_cast<s128>(count), bits, false, flags);
        return static_cast<uint64_t>(static_cast<int64_t>(s));
    }
    default: {
        const s128 v = narrow ? static_cast<s128>(static_cast<uint32_t>(x)) : static_cast<s128>(x);
        return static_cast<uint64_t>(satQ(code == 4 ? v + static_cast<s128>(count) : v - static_cast<s128>(count), bits, true, flags));
    }
    }
}

// INCP, DECP and the saturating forms on a general register. Sources Pm (fp); Rdn (int). c as countedScalar, d & 1 = W.
inline void
incpScalar(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    uint64_t       count = 0;
    for ( unsigned b = 0; b < vl; b += ebytes ) {
        if ( pbit(op.Vin(rf, 0), b) ) ++count;
    }
    op.setX(rf, 0, countedScalar(op.X(rf, 0), count, op.c, op.d & 1));
}

// INC, DEC of a general register by imm.
inline void
incScalarImm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    op.setX(rf, 0, countedScalar(op.X(rf, 0), op.imm, op.c, op.d & 1));
}

// INC, DEC of a vector: c as countedScalar; imm = the count, or Pm is source 1 (d & 2).
inline void
incVec(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    uint64_t       count = op.imm;
    if ( op.d & 2 ) {
        count = 0;
        for ( unsigned b = 0; b < vl; b += 1u << op.a ) {
            if ( pbit(op.Vin(rf, 1), b) ) ++count;
        }
    }
    uint32_t flags = 0;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        const unsigned bits = sizeof(U) * 8;
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            const U x = vget<U>(op.Vin(rf, 0), e);
            U v = x;
            switch ( op.c ) {
            case 0: v = static_cast<U>(x + count); break;
            case 1: v = static_cast<U>(x - count); break;
            case 2: v = static_cast<U>(satQ(widen(x, false) + static_cast<s128>(count), bits, false, flags)); break;
            case 3: v = static_cast<U>(satQ(widen(x, false) - static_cast<s128>(count), bits, false, flags)); break;
            case 4: v = static_cast<U>(satQ(widen(x, true) + static_cast<s128>(count), bits, true, flags)); break;
            default: v = static_cast<U>(satQ(widen(x, true) - static_cast<s128>(count), bits, true, flags)); break;
            }
            vset<U>(r, e, v);
        }
    });
    writeZ(op, rf, r);
}

// WHILE*: integer sources Rn, Rm; destination NZCV; a = element size. c = (U << 3) | (lt-style kind): see the decoder:
// 0 LT, 1 LE, 2 GE, 3 GT with U = 0 and 8 LO, 9 LS, 10 HS, 11 HI. d & 1 = 32-bit operands.
inline void
whilePred(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    const unsigned elements = vl / ebytes;
    const bool     is_u = op.c & 8, narrow = op.d & 1;
    const uint32_t kind = op.c & 7;
    s128 a, b;
    if ( is_u ) {
        a = narrow ? static_cast<s128>(static_cast<uint32_t>(op.X(rf, 0))) : static_cast<s128>(op.X(rf, 0));
        b = narrow ? static_cast<s128>(static_cast<uint32_t>(op.X(rf, 1))) : static_cast<s128>(op.X(rf, 1));
    }
    else {
        a = narrow ? static_cast<s128>(static_cast<int32_t>(op.X(rf, 0))) : static_cast<s128>(static_cast<int64_t>(op.X(rf, 0)));
        b = narrow ? static_cast<s128>(static_cast<int32_t>(op.X(rf, 1))) : static_cast<s128>(static_cast<int64_t>(op.X(rf, 1)));
    }
    std::memset(r, 0, vl / 8);
    const bool downward = kind >= 2;
    bool last = true;
    for ( unsigned e = 0; e < elements; ++e ) {
        const s128 cur = downward ? a - e : a + e;
        bool cond = false;
        switch ( kind ) {
        case 0: cond = cur < b; break;
        case 1: cond = cur <= b; break;
        case 2: cond = cur >= b; break;
        default: cond = cur > b; break;
        }
        last = last && cond;
        const unsigned idx = downward ? elements - 1 - e : e;
        pset(r, idx * ebytes, last);
    }
    writeP(op, rf, r);
    uint8_t all[MAX_VL_BYTES / 8];
    std::memset(all, 0xFF, vl / 8);
    op.setX(rf, 0, predTest(all, r, vl, ebytes));
}

// WHILEWR (c 0), WHILERW (c 1): integer sources Rn, Rm; destination NZCV.
inline void
whileConflict(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    const unsigned elements = vl / ebytes;
    const uint64_t op1 = op.X(rf, 0), op2 = op.X(rf, 1);
    s128 diff = static_cast<s128>(op2) - static_cast<s128>(op1);
    if ( op.c == 1 ) diff = diff < 0 ? -diff : diff;
    std::memset(r, 0, vl / 8);
    const s128 limit = diff / static_cast<s128>(ebytes);
    const bool always = op.c == 0 ? limit <= 0 : limit == 0;
    for ( unsigned e = 0; e < elements; ++e ) {
        pset(r, e * ebytes, always || static_cast<s128>(e) < limit);
    }
    writeP(op, rf, r);
    uint8_t all[MAX_VL_BYTES / 8];
    std::memset(all, 0xFF, vl / 8);
    op.setX(rf, 0, predTest(all, r, vl, ebytes));
}

// CTERMEQ, CTERMNE: sources Rn, Rm, NZCV in. c = NE. d & 1 = 32-bit.
inline void
cterm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t a = op.X(rf, 0), b = op.X(rf, 1);
    if ( op.d & 1 ) {
        a &= 0xFFFFFFFFull;
        b &= 0xFFFFFFFFull;
    }
    const uint64_t in = op.X(rf, 2);
    const bool     term = (a == b) != static_cast<bool>(op.c);
    uint64_t       out = in & (A64Flag::Z | A64Flag::C);
    if ( term ) out |= A64Flag::N;
    else if ( !(in & A64Flag::C) ) out |= A64Flag::V;
    op.setX(rf, 0, out);
}

// INDEX: imm = start and imm2 = step when immediate; d & 1: start in X source 0; d & 2: step in X source (1 or 0).
inline void
index(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    unsigned       pos = 0;
    const uint64_t start = (op.d & 1) ? op.X(rf, pos++) : op.imm;
    const uint64_t step = (op.d & 2) ? op.X(rf, pos++) : op.imm2;
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) vset<U>(r, e, static_cast<U>(start + step * e));
    });
    writeZ(op, rf, r);
}

// DUP (general register): integer source 0.
inline void
dupGp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    const uint64_t v = op.X(rf, 0);
    for ( unsigned e = 0; e < vl / ebytes; ++e ) std::memcpy(r + e * ebytes, &v, ebytes);
    writeZ(op, rf, r);
}

// DUP (indexed): imm = element index; an index at or beyond the vector gives zero.
inline void
dupIndexed(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    if ( op.imm >= vl / ebytes ) {
        std::memset(r, 0, vl);
    }
    else {
        for ( unsigned e = 0; e < vl / ebytes; ++e ) std::memcpy(r + e * ebytes, op.Vin(rf, 0) + op.imm * ebytes, ebytes);
    }
    writeZ(op, rf, r);
}

// DUP (indexed) of a 128-bit element: imm = the quadword index; beyond the vector gives zero.
inline void
dupQuad(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    if ( (op.imm + 1) * 16 > vl ) {
        std::memset(r, 0, vl);
    }
    else {
        for ( unsigned k = 0; (k + 1) * 16 <= vl; ++k ) std::memcpy(r + k * 16, op.Vin(rf, 0) + op.imm * 16, 16);
    }
    writeZ(op, rf, r);
}

// INSR: source Zdn; a value in the low bytes of an integer source (d = 0) or a vector source 1 (d = 1).
inline void
insr(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    std::memcpy(r + ebytes, op.Vin(rf, 0), vl - ebytes);
    if ( op.d ) {
        std::memcpy(r, op.Vin(rf, 1), ebytes);
    }
    else {
        const uint64_t v = op.X(rf, 0);
        std::memcpy(r, &v, ebytes);
    }
    writeZ(op, rf, r);
}

// REV (vector).
inline void
revZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    const unsigned n = vl / ebytes;
    for ( unsigned e = 0; e < n; ++e ) std::memcpy(r + e * ebytes, op.Vin(rf, 0) + (n - 1 - e) * ebytes, ebytes);
    writeZ(op, rf, r);
}

// SUNPK, UUNPK. a = narrow size. c: 0 signed low, 1 signed high, 2 unsigned low, 3 unsigned high.
inline void
unpack(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const bool     is_u = op.c >= 2, high = op.c & 1;
    byElem(op.a, [&](auto zero) {
        using N = decltype(zero);
        if constexpr ( sizeof(N) < 8 ) {
            using W = std::conditional_t<sizeof(N) == 1, uint16_t, std::conditional_t<sizeof(N) == 2, uint32_t, uint64_t>>;
            const unsigned n = vl / sizeof(W);
            for ( unsigned e = 0; e < n; ++e ) {
                vset<W>(r, e, static_cast<W>(widen(vget<N>(op.Vin(rf, 0), e + (high ? n : 0)), is_u)));
            }
        }
    });
    writeZ(op, rf, r);
}

// TBL (one register, c 0), TBL (two registers, c 1), TBX (c 2). Sources: table registers, indices Zm; TBX then Zd.
inline void
tableZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    const unsigned n = vl / ebytes;
    const unsigned tables = op.c == 1 ? 2 : 1;
    const uint8_t* zm = op.Vin(rf, tables);
    for ( unsigned e = 0; e < n; ++e ) {
        uint64_t idx = 0;
        std::memcpy(&idx, zm + e * ebytes, ebytes);
        if ( idx < n * tables ) {
            std::memcpy(r + e * ebytes, op.Vin(rf, static_cast<unsigned>(idx / n)) + (idx % n) * ebytes, ebytes);
        }
        else if ( op.c == 2 ) {
            std::memcpy(r + e * ebytes, op.Vin(rf, 2) + e * ebytes, ebytes);
        }
        else {
            std::memset(r + e * ebytes, 0, ebytes);
        }
    }
    writeZ(op, rf, r);
}

// ZIP1 (0), ZIP2 (1), UZP1 (2), UZP2 (3), TRN1 (4), TRN2 (5) over elements of ebytes, on the first bytes of two sources.
inline void
permuteBytes(uint8_t* r, const uint8_t* a, const uint8_t* b, unsigned total, unsigned ebytes, uint32_t code)
{
    const unsigned n = total / ebytes;
    const unsigned part = code & 1;
    switch ( code >> 1 ) {
    case 0: {
        const unsigned base = part ? n / 2 : 0;
        for ( unsigned e = 0; e < n / 2; ++e ) {
            std::memcpy(r + 2 * e * ebytes, a + (base + e) * ebytes, ebytes);
            std::memcpy(r + (2 * e + 1) * ebytes, b + (base + e) * ebytes, ebytes);
        }
    } break;
    case 1:
        for ( unsigned e = 0; e < n / 2; ++e ) {
            std::memcpy(r + e * ebytes, a + (2 * e + part) * ebytes, ebytes);
            std::memcpy(r + (n / 2 + e) * ebytes, b + (2 * e + part) * ebytes, ebytes);
        }
        break;
    default:
        for ( unsigned e = 0; e < n / 2; ++e ) {
            std::memcpy(r + 2 * e * ebytes, a + (2 * e + part) * ebytes, ebytes);
            std::memcpy(r + (2 * e + 1) * ebytes, b + (2 * e + part) * ebytes, ebytes);
        }
        break;
    }
}

// ZIP, UZP, TRN of vectors. d = 1: 128-bit elements (the bytes beyond a whole even count of quadwords are zero).
inline void
permuteZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    if ( op.d ) {
        const unsigned quads = (vl / 16) & ~1u;
        std::memset(r, 0, vl);
        if ( quads ) permuteBytes(r, op.Vin(rf, 0), op.Vin(rf, 1), quads * 16, 16, op.c);
    }
    else {
        permuteBytes(r, op.Vin(rf, 0), op.Vin(rf, 1), vl, 1u << op.a, op.c);
    }
    writeZ(op, rf, r);
}

// Bits of a predicate as bytes, one byte per bit, to reuse the byte permutes on predicates of elements of ebytes.
inline void
permuteP(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        a[MAX_VL_BYTES], b[MAX_VL_BYTES], t[MAX_VL_BYTES], r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    for ( unsigned k = 0; k < vl; ++k ) {
        a[k] = pbit(op.Vin(rf, 0), k);
        b[k] = pbit(op.Vin(rf, 1), k);
    }
    permuteBytes(t, a, b, vl, ebytes, op.c);
    std::memset(r, 0, vl / 8);
    for ( unsigned k = 0; k < vl; ++k ) pset(r, k, t[k]);
    writeP(op, rf, r);
}

// REV (predicate): a = element size.
inline void
revP(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    const unsigned n = vl / ebytes;
    std::memset(r, 0, vl / 8);
    for ( unsigned e = 0; e < n; ++e ) {
        for ( unsigned k = 0; k < ebytes; ++k ) pset(r, e * ebytes + k, pbit(op.Vin(rf, 0), (n - 1 - e) * ebytes + k));
    }
    writeP(op, rf, r);
}

// PUNPKLO (c 0), PUNPKHI (c 1).
inline void
punpk(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf);
    std::memset(r, 0, vl / 8);
    for ( unsigned e = 0; e < vl / 2; ++e ) pset(r, e * 2, pbit(op.Vin(rf, 0), e + (op.c ? vl / 2 : 0)));
    writeP(op, rf, r);
}

// SEL (vectors): sources Pg, Zn, Zm.
inline void
selZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        std::memcpy(r + e * ebytes, op.Vin(rf, pbit(op.Vin(rf, 0), e * ebytes) ? 1 : 2) + e * ebytes, ebytes);
    }
    writeZ(op, rf, r);
}

// CPY: predicated copy of imm (c 0) or of an integer (c 1) or SIMD (c 2) source. d = merging. Sources Pg, Zd (merging)
// and Vn (c 2).
inline void
cpyPred(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    uint64_t v = op.imm;
    if ( op.c == 1 ) v = op.X(rf, 0);
    else if ( op.c == 2 ) std::memcpy(&v, op.Vin(rf, op.d ? 2 : 1), ebytes);
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        if ( pbit(op.Vin(rf, 0), e * ebytes) ) {
            std::memcpy(r + e * ebytes, &v, ebytes);
        }
        else if ( op.d ) {
            std::memcpy(r + e * ebytes, op.Vin(rf, 1) + e * ebytes, ebytes);
        }
        else {
            std::memset(r + e * ebytes, 0, ebytes);
        }
    }
    writeZ(op, rf, r);
}

// The position of the last active element, -1 when none.
inline int
lastActive(const uint8_t* pg, unsigned elements, unsigned ebytes)
{
    int last = -1;
    for ( unsigned e = 0; e < elements; ++e ) {
        if ( pbit(pg, e * ebytes) ) last = static_cast<int>(e);
    }
    return last;
}

// LASTA, LASTB. d: 0 to general register, 1 to SIMD register. c: 0 LASTA, 1 LASTB. Sources Pg, Zn.
inline void
lastElement(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a, n = vl / ebytes;
    int idx = lastActive(op.Vin(rf, 0), n, ebytes);
    if ( op.c == 0 ) idx = (idx + 1) % static_cast<int>(n);
    else if ( idx < 0 ) idx = static_cast<int>(n) - 1;
    uint64_t v = 0;
    std::memcpy(&v, op.Vin(rf, 1) + idx * ebytes, ebytes);
    if ( op.d ) writeLow(op, rf, 0, &v, ebytes);
    else op.setX(rf, 0, v);
}

// CLASTA, CLASTB to a vector (d 2), SIMD scalar (d 1) or general register (d 0). c: 0 CLASTA, 1 CLASTB. Sources Pg,
// Zdn or Vdn, Zm; integer source Rdn for d 0.
inline void
clast(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a, n = vl / ebytes;
    const uint8_t* pg = op.Vin(rf, 0);
    const uint8_t* zm = op.Vin(rf, op.d == 0 ? 1 : 2);
    int idx = lastActive(pg, n, ebytes);
    if ( idx >= 0 && op.c == 0 ) idx = (idx + 1) % static_cast<int>(n);
    if ( op.d == 2 ) {
        uint8_t r[MAX_VL_BYTES];
        if ( idx < 0 ) {
            std::memcpy(r, op.Vin(rf, 1), vl);
        }
        else {
            for ( unsigned e = 0; e < n; ++e ) std::memcpy(r + e * ebytes, zm + idx * ebytes, ebytes);
        }
        writeZ(op, rf, r);
    }
    else if ( op.d == 1 ) {
        uint64_t v = 0;
        std::memcpy(&v, idx < 0 ? op.Vin(rf, 1) : zm + idx * ebytes, ebytes);
        writeLow(op, rf, 0, &v, ebytes);
    }
    else {
        uint64_t v;
        if ( idx < 0 ) {
            v = op.X(rf, 0);
            if ( ebytes < 8 ) v &= (1ull << (ebytes * 8)) - 1;
        }
        else {
            v = 0;
            std::memcpy(&v, zm + idx * ebytes, ebytes);
        }
        op.setX(rf, 0, v);
    }
}

// COMPACT: sources Pg, Zn.
inline void
compact(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a;
    unsigned       k = 0;
    std::memset(r, 0, vl);
    for ( unsigned e = 0; e < vl / ebytes; ++e ) {
        if ( pbit(op.Vin(rf, 0), e * ebytes) ) std::memcpy(r + (k++) * ebytes, op.Vin(rf, 1) + e * ebytes, ebytes);
    }
    writeZ(op, rf, r);
}

// SPLICE: sources Pg, Zdn (first), Zm (second, or Zn+1 in the constructive form).
inline void
splice(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf), ebytes = 1u << op.a, n = vl / ebytes;
    const uint8_t* pg = op.Vin(rf, 0);
    int first = -1, last = -1;
    for ( unsigned e = 0; e < n; ++e ) {
        if ( pbit(pg, e * ebytes) ) {
            if ( first < 0 ) first = static_cast<int>(e);
            last = static_cast<int>(e);
        }
    }
    unsigned k = 0;
    if ( first >= 0 ) {
        for ( int e = first; e <= last; ++e ) std::memcpy(r + (k++) * ebytes, op.Vin(rf, 1) + e * ebytes, ebytes);
    }
    for ( unsigned e = 0; k < n; ++e ) std::memcpy(r + (k++) * ebytes, op.Vin(rf, 2) + e * ebytes, ebytes);
    writeZ(op, rf, r);
}

// EXT: sources Zdn, Zm (or the two registers of the constructive form); imm = byte position, at or beyond VL means 0.
inline void
extZ(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        c[2 * MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const unsigned pos = op.imm >= vl ? 0 : static_cast<unsigned>(op.imm);
    std::memcpy(c, op.Vin(rf, 0), vl);
    std::memcpy(c + vl, op.Vin(rf, 1), vl);
    writeZ(op, rf, c + pos);
}

// ADR: sources Zn, Zm. a = element size (2 or 3). c = shift. d: 0 same size, 1 signed 32-bit offsets, 2 unsigned.
inline void
adr(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    byElem(op.a, [&](auto zero) {
        using U = decltype(zero);
        for ( unsigned e = 0; e < vl / sizeof(U); ++e ) {
            uint64_t off = vget<U>(op.Vin(rf, 1), e);
            if ( op.d == 1 ) off = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(off)));
            else if ( op.d == 2 ) off &= 0xFFFFFFFFull;
            vset<U>(r, e, static_cast<U>(vget<U>(op.Vin(rf, 0), e) + (off << op.c)));
        }
    });
    writeZ(op, rf, r);
}

// FIRSTP is not implemented: it belongs to SVE2.1.

} // namespace A64Sve
} // namespace Vanadis
} // namespace SST

#endif
