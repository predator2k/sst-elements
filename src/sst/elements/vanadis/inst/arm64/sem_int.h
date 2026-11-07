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

// Semantic functions of the A64 integer instructions.
//
// Payload conventions, for every function here:
// * a is 1 for a 64-bit operation (the sf bit), 0 for a 32-bit one;
// * d holds HAS_RD and SET_FLAGS. A destination XZR is left out of the destination list, so the result
//   is destination 0 when HAS_RD is set and NZCV is the next destination when SET_FLAGS is set;
// * a result written for a 32-bit operation is zero-extended, as A64 defines.

#ifndef _H_VANADIS_ARM64_SEM_INT
#define _H_VANADIS_ARM64_SEM_INT

#include "inst/arm64/varm64inst.h"

namespace SST {
namespace Vanadis {
namespace A64Sem {

constexpr uint32_t HAS_RD    = 1;
constexpr uint32_t SET_FLAGS = 2;

// Second-operand kinds of the add/sub and logical semantic functions (imm2).
constexpr uint64_t OP2_IMM   = 0;
constexpr uint64_t OP2_SHIFT = 1;
constexpr uint64_t OP2_EXT   = 2;

inline void
wrRd(VanadisArm64Inst& op, VanadisRegisterFile* rf, uint64_t v)
{
    if ( op.d & HAS_RD ) op.setX(rf, 0, op.a ? v : (v & 0xFFFFFFFFull));
}

inline void
wrFlags(VanadisArm64Inst& op, VanadisRegisterFile* rf, uint64_t nzcv)
{
    if ( op.d & SET_FLAGS ) op.setX(rf, (op.d & HAS_RD) ? 1 : 0, nzcv);
}

inline uint64_t
a64Shift(uint64_t v, unsigned type, unsigned amount, bool sf)
{
    if ( sf ) {
        switch ( type ) {
        case 0: return v << amount;
        case 1: return v >> amount;
        case 2: return static_cast<uint64_t>(static_cast<int64_t>(v) >> amount);
        default: return amount ? (v >> amount) | (v << (64 - amount)) : v;
        }
    }
    const uint32_t w = static_cast<uint32_t>(v);
    switch ( type ) {
    case 0: return static_cast<uint32_t>(w << amount);
    case 1: return w >> amount;
    case 2: return static_cast<uint32_t>(static_cast<int32_t>(w) >> amount);
    default: return amount ? static_cast<uint32_t>((w >> amount) | (w << (32 - amount))) : w;
    }
}

inline uint64_t
a64Extend(uint64_t v, unsigned option, unsigned shift)
{
    uint64_t x = v;
    switch ( option & 7 ) {
    case 0: x = static_cast<uint8_t>(v); break;
    case 1: x = static_cast<uint16_t>(v); break;
    case 2: x = static_cast<uint32_t>(v); break;
    case 3: break;
    case 4: x = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v))); break;
    case 5: x = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v))); break;
    case 6: x = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v))); break;
    case 7: break;
    }
    return x << shift;
}

// The sum of two operands and a carry, with the flags of AddWithCarry().
inline uint64_t
a64AddWithCarry(uint64_t x, uint64_t y, unsigned carry, bool sf, uint64_t* nzcv)
{
    uint64_t result;
    uint64_t flags = 0;
    if ( sf ) {
        unsigned __int128 u = static_cast<unsigned __int128>(x) + y + carry;
        __int128          s = static_cast<__int128>(static_cast<int64_t>(x)) + static_cast<int64_t>(y) + carry;
        result              = static_cast<uint64_t>(u);
        if ( result >> 63 ) flags |= A64Flag::N;
        if ( result == 0 ) flags |= A64Flag::Z;
        if ( (u >> 64) != 0 ) flags |= A64Flag::C;
        if ( s != static_cast<int64_t>(result) ) flags |= A64Flag::V;
    }
    else {
        uint64_t u = static_cast<uint64_t>(static_cast<uint32_t>(x)) + static_cast<uint32_t>(y) + carry;
        int64_t  s = static_cast<int64_t>(static_cast<int32_t>(x)) + static_cast<int32_t>(y) + carry;
        result     = static_cast<uint32_t>(u);
        if ( result >> 31 ) flags |= A64Flag::N;
        if ( result == 0 ) flags |= A64Flag::Z;
        if ( (u >> 32) != 0 ) flags |= A64Flag::C;
        if ( s != static_cast<int32_t>(result) ) flags |= A64Flag::V;
    }
    *nzcv = flags;
    return result;
}

// X[d] = imm (ADR, ADRP, MOVZ, MOVN, and a temporary that holds an address).
inline void
movImm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    op.setX(rf, 0, op.imm);
}

// Operand 2 of the add/sub and logical functions; source 1 is Rm when it is a register.
inline uint64_t
operand2(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    switch ( op.imm2 ) {
    case OP2_SHIFT: return a64Shift(op.X(rf, 1), (op.c >> 8) & 3, op.c & 0xFF, op.a);
    case OP2_EXT: return a64Extend(op.X(rf, 1), (op.c >> 8) & 7, op.c & 0xFF);
    default: return op.imm;
    }
}

// ADD, ADDS, SUB, SUBS with an immediate, shifted or extended operand 2. b: bit 0 is subtract.
inline void
addSub(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t y = operand2(op, rf);
    uint64_t nzcv;
    if ( op.b & 1 ) y = ~y;
    const uint64_t r = a64AddWithCarry(op.X(rf, 0), y, op.b & 1, op.a, &nzcv);
    wrRd(op, rf, r);
    wrFlags(op, rf, nzcv);
}

// ADC, ADCS, SBC, SBCS. Sources: Rn, Rm, NZCV. b: bit 0 is subtract.
inline void
addSubCarry(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t y = op.X(rf, 1);
    uint64_t nzcv;
    if ( op.b & 1 ) y = ~y;
    const uint64_t r = a64AddWithCarry(op.X(rf, 0), y, (op.X(rf, 2) & A64Flag::C) ? 1 : 0, op.a, &nzcv);
    wrRd(op, rf, r);
    wrFlags(op, rf, nzcv);
}

// AND, ORR, EOR, ANDS and, with c bit 16, the inverted operand 2 forms BIC, ORN, EON, BICS.
// b: 0 AND, 1 ORR, 2 EOR.
inline void
logicalOp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t y = operand2(op, rf);
    if ( op.c & (1u << 16) ) y = ~y;
    uint64_t x = op.X(rf, 0);
    uint64_t r = 0;
    switch ( op.b ) {
    case 0: r = x & y; break;
    case 1: r = x | y; break;
    default: r = x ^ y; break;
    }
    if ( !op.a ) r &= 0xFFFFFFFFull;
    wrRd(op, rf, r);
    uint64_t nzcv = 0;
    if ( (r >> (op.a ? 63 : 31)) & 1 ) nzcv |= A64Flag::N;
    if ( r == 0 ) nzcv |= A64Flag::Z;
    wrFlags(op, rf, nzcv);
}

// MOVK. Source: Rd. imm is the shifted 16-bit value, imm2 the mask of the bits it replaces.
inline void
movK(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    wrRd(op, rf, (op.X(rf, 0) & ~op.imm2) | op.imm);
}

// SBFM, BFM, UBFM. Sources: Rn and, for BFM, Rd. imm = wmask, imm2 = tmask; b: 0 SBFM, 1 BFM, 2 UBFM;
// c: immr, d high bits: imms in bits 16 and up (d low bits are the common flags).
inline void
bitfield(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned R    = op.c & 0x3F;
    const unsigned S    = op.d >> 16;
    const unsigned size = op.a ? 64 : 32;
    uint64_t       src  = op.X(rf, 0);
    if ( !op.a ) src &= 0xFFFFFFFFull;
    const uint64_t bot = R ? ((src >> R) | (src << (size - R))) : src;
    const uint64_t rot = op.a ? bot : (bot & 0xFFFFFFFFull);
    uint64_t       r;
    if ( op.b == 1 ) {
        const uint64_t dst = op.X(rf, 1);
        const uint64_t b2  = (dst & ~op.imm) | (rot & op.imm);
        r                  = (dst & ~op.imm2) | (b2 & op.imm2);
    }
    else {
        const uint64_t bits = rot & op.imm;
        uint64_t       top  = 0;
        if ( op.b == 0 && ((src >> S) & 1) ) top = ~0ull;
        r = (top & ~op.imm2) | (bits & op.imm2);
    }
    wrRd(op, rf, r);
}

// EXTR. Sources: Rn, Rm; c = lsb.
inline void
extr(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint64_t n = op.X(rf, 0), m = op.X(rf, 1);
    const unsigned lsb = op.c;
    uint64_t       r;
    if ( op.a ) {
        r = lsb ? ((m >> lsb) | (n << (64 - lsb))) : m;
    }
    else {
        const uint32_t nw = static_cast<uint32_t>(n), mw = static_cast<uint32_t>(m);
        r = lsb ? static_cast<uint32_t>((mw >> lsb) | (nw << (32 - lsb))) : mw;
    }
    wrRd(op, rf, r);
}

// CSEL, CSINC, CSINV, CSNEG. Sources: Rn, Rm, NZCV. b: 0 CSEL, 1 CSINC, 2 CSINV, 3 CSNEG; c = cond.
inline void
condSelect(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t r;
    if ( a64ConditionHolds(op.X(rf, 2), op.c) ) {
        r = op.X(rf, 0);
    }
    else {
        const uint64_t m = op.X(rf, 1);
        switch ( op.b ) {
        case 0: r = m; break;
        case 1: r = m + 1; break;
        case 2: r = ~m; break;
        default: r = 0 - m; break;
        }
    }
    wrRd(op, rf, r);
}

// CCMN, CCMP. Sources: Rn, NZCV and, for the register form, Rm in position 2.
// b: bit 0 is subtract (CCMP), bit 1 is the register form; c = cond; imm = imm5, imm2 = nzcv on false.
inline void
condCompare(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t flags = op.imm2;
    if ( a64ConditionHolds(op.X(rf, 1), op.c) ) {
        uint64_t y = (op.b & 2) ? op.X(rf, 2) : op.imm;
        if ( op.b & 1 ) y = ~y;
        a64AddWithCarry(op.X(rf, 0), y, op.b & 1, op.a, &flags);
    }
    wrFlags(op, rf, flags);
}

// RBIT, REV16, REV (32 in 32, 32 in 64, 64), CLZ, CLS; b selects.
enum class Dp1Op : uint32_t { RBIT, REV16, REV32, REV64, CLZ, CLS, CTZ, CNT, ABS };

inline uint64_t
bitReverse(uint64_t v, unsigned size)
{
    uint64_t r = 0;
    for ( unsigned i = 0; i < size; ++i )
        if ( (v >> i) & 1 ) r |= 1ull << (size - 1 - i);
    return r;
}

inline void
dataProc1(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned size = op.a ? 64 : 32;
    uint64_t       v    = op.X(rf, 0);
    if ( !op.a ) v &= 0xFFFFFFFFull;
    uint64_t r = 0;
    switch ( static_cast<Dp1Op>(op.b) ) {
    case Dp1Op::RBIT: r = bitReverse(v, size); break;
    case Dp1Op::REV16:
        r = ((v & 0x00FF00FF00FF00FFull) << 8) | ((v >> 8) & 0x00FF00FF00FF00FFull);
        break;
    case Dp1Op::REV32:
        r = op.a ? ((static_cast<uint64_t>(__builtin_bswap32(static_cast<uint32_t>(v >> 32))) << 32)
                    | __builtin_bswap32(static_cast<uint32_t>(v)))
                 : __builtin_bswap32(static_cast<uint32_t>(v));
        break;
    case Dp1Op::REV64: r = __builtin_bswap64(v); break;
    case Dp1Op::CLZ:
        r = v ? static_cast<uint64_t>(__builtin_clzll(v) - (64 - size)) : size;
        break;
    case Dp1Op::CLS:
    {
        const unsigned sign = (v >> (size - 1)) & 1;
        unsigned       n    = 0;
        for ( int i = static_cast<int>(size) - 2; i >= 0 && ((v >> i) & 1) == sign; --i ) ++n;
        r = n;
    } break;
    case Dp1Op::CTZ: r = v ? static_cast<uint64_t>(__builtin_ctzll(v)) : size; break;
    case Dp1Op::CNT: r = __builtin_popcountll(v); break;
    case Dp1Op::ABS:
        if ( op.a ) r = (static_cast<int64_t>(v) < 0) ? (0 - v) : v;
        else r = (static_cast<int32_t>(v) < 0) ? (0 - static_cast<uint32_t>(v)) : v;
        break;
    }
    wrRd(op, rf, r);
}

// UDIV, SDIV, LSLV, LSRV, ASRV, RORV. b selects.
enum class Dp2Op : uint32_t { UDIV, SDIV, LSLV, LSRV, ASRV, RORV };

inline void
dataProc2(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t n = op.X(rf, 0), m = op.X(rf, 1);
    uint64_t r = 0;
    switch ( static_cast<Dp2Op>(op.b) ) {
    case Dp2Op::UDIV:
        if ( op.a ) r = m ? n / m : 0;
        else r = static_cast<uint32_t>(m) ? static_cast<uint32_t>(n) / static_cast<uint32_t>(m) : 0;
        break;
    case Dp2Op::SDIV:
        if ( op.a ) {
            const int64_t sn = static_cast<int64_t>(n), sm = static_cast<int64_t>(m);
            if ( sm == 0 ) r = 0;
            else if ( sm == -1 ) r = 0 - n;
            else r = static_cast<uint64_t>(sn / sm);
        }
        else {
            const int32_t sn = static_cast<int32_t>(n), sm = static_cast<int32_t>(m);
            if ( sm == 0 ) r = 0;
            else if ( sm == -1 ) r = 0u - static_cast<uint32_t>(n);
            else r = static_cast<uint32_t>(sn / sm);
        }
        break;
    case Dp2Op::LSLV: r = a64Shift(n, 0, m & (op.a ? 63 : 31), op.a); break;
    case Dp2Op::LSRV: r = a64Shift(n, 1, m & (op.a ? 63 : 31), op.a); break;
    case Dp2Op::ASRV: r = a64Shift(n, 2, m & (op.a ? 63 : 31), op.a); break;
    case Dp2Op::RORV: r = a64Shift(n, 3, m & (op.a ? 63 : 31), op.a); break;
    }
    wrRd(op, rf, r);
}

// CRC32B/H/W/X, CRC32CB/H/W/X. Sources: Rn (the accumulator), Rm. b = bytes, c = polynomial (reflected).
inline void
crc32(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t crc  = static_cast<uint32_t>(op.X(rf, 0));
    uint64_t data = op.X(rf, 1);
    for ( uint32_t i = 0; i < op.b; ++i ) {
        crc ^= static_cast<uint8_t>(data >> (8 * i));
        for ( int k = 0; k < 8; ++k ) crc = (crc >> 1) ^ ((crc & 1) ? op.c : 0);
    }
    wrRd(op, rf, crc);
}

// MADD, MSUB, SMADDL, SMSUBL, UMADDL, UMSUBL, SMULH, UMULH. Sources: Rn, Rm, Ra. b selects.
enum class Dp3Op : uint32_t { MADD, MSUB, SMADDL, SMSUBL, UMADDL, UMSUBL, SMULH, UMULH };

inline void
dataProc3(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint64_t n = op.X(rf, 0), m = op.X(rf, 1), ra = op.X(rf, 2);
    uint64_t       r = 0;
    switch ( static_cast<Dp3Op>(op.b) ) {
    case Dp3Op::MADD: r = ra + n * m; break;
    case Dp3Op::MSUB: r = ra - n * m; break;
    case Dp3Op::SMADDL:
        r = ra + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(n)) * static_cast<int32_t>(m));
        break;
    case Dp3Op::SMSUBL:
        r = ra - static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(n)) * static_cast<int32_t>(m));
        break;
    case Dp3Op::UMADDL: r = ra + static_cast<uint64_t>(static_cast<uint32_t>(n)) * static_cast<uint32_t>(m); break;
    case Dp3Op::UMSUBL: r = ra - static_cast<uint64_t>(static_cast<uint32_t>(n)) * static_cast<uint32_t>(m); break;
    case Dp3Op::SMULH:
        r = static_cast<uint64_t>(
            (static_cast<__int128>(static_cast<int64_t>(n)) * static_cast<int64_t>(m)) >> 64);
        break;
    case Dp3Op::UMULH:
        r = static_cast<uint64_t>((static_cast<unsigned __int128>(n) * m) >> 64);
        break;
    }
    wrRd(op, rf, r);
}

// Move one register to another, zero-extending when a is 0 (the 32-bit result of a sign-extending load
// and the result of an LSE operation).
inline void
movReg(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    wrRd(op, rf, op.X(rf, 0));
}

// Base plus scaled, extended index into a temporary. Sources: Rn, Rm; c = extend option and shift.
inline void
addrRegOffset(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    op.setX(rf, 0, op.X(rf, 0) + a64Extend(op.X(rf, 1), (op.c >> 8) & 7, op.c & 0xFF));
}

// Base plus immediate (the write-back of a pre- or post-indexed access).
inline void
addImm(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    op.setX(rf, 0, op.X(rf, 0) + op.imm);
}

// NZCV moved to or from a general register, and the flag-manipulation instructions.
inline void
mrsNzcv(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    op.setX(rf, 0, op.X(rf, 0) & 0xF0000000ull);
}

inline void
msrNzcv(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    op.setX(rf, 0, op.X(rf, 0) & 0xF0000000ull);
}

inline void
cfinv(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    op.setX(rf, 0, op.X(rf, 0) ^ A64Flag::C);
}

// AXFLAG and XAFLAG (FEAT_FlagM2).
inline void
axflag(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint64_t f = op.X(rf, 0);
    const bool     n = f & A64Flag::N, z = f & A64Flag::Z, c = f & A64Flag::C, v = f & A64Flag::V;
    uint64_t       r = 0;
    if ( z || v ) r |= A64Flag::Z;
    if ( c && !v ) r |= A64Flag::C;
    (void)n;
    op.setX(rf, 0, r);
}

inline void
xaflag(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint64_t f = op.X(rf, 0);
    const bool     z = f & A64Flag::Z, c = f & A64Flag::C;
    uint64_t       r = 0;
    if ( !c && !z ) r |= A64Flag::N;
    if ( c && z ) r |= A64Flag::Z;
    if ( c || z ) r |= A64Flag::C;
    if ( !c && z ) r |= A64Flag::V;
    op.setX(rf, 0, r);
}

// RMIF. Sources: Rn, NZCV. c = rotate right, imm = mask of bits 3..0 of the rotated value.
inline void
rmif(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint64_t v   = op.X(rf, 0);
    const unsigned s   = op.c;
    const uint64_t rot = s ? ((v >> s) | (v << (64 - s))) : v;
    uint64_t       f   = op.X(rf, 1);
    for ( int i = 0; i < 4; ++i ) {
        if ( op.imm & (1u << i) ) {
            const uint64_t bit = 1ull << (28 + i);
            f                  = (f & ~bit) | (((rot >> i) & 1) ? bit : 0);
        }
    }
    op.setX(rf, 0, f);
}

// SETF8, SETF16. Sources: Rn, NZCV. c = 8 or 16.
inline void
setf(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint32_t v   = static_cast<uint32_t>(op.X(rf, 0));
    const unsigned msb = op.c - 1;
    uint64_t       f   = op.X(rf, 1) & A64Flag::C;
    if ( (v >> msb) & 1 ) f |= A64Flag::N;
    if ( (v & ((1ull << op.c) - 1)) == 0 ) f |= A64Flag::Z;
    if ( ((v >> op.c) ^ (v >> msb)) & 1 ) f |= A64Flag::V;
    op.setX(rf, 0, f);
}

// LSE and exclusive helpers: the value an atomic operation stores. Sources: loaded value, Rs.
// b: 0 ADD, 1 CLR, 2 EOR, 3 SET, 4 SMAX, 5 SMIN, 6 UMAX, 7 UMIN, 8 SWP; c = operand bytes.
inline void
atomicOp(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint64_t mem = op.X(rf, 0), s = op.X(rf, 1);
    const unsigned bits = op.c * 8;
    const uint64_t mask = bits == 64 ? ~0ull : ((1ull << bits) - 1);
    const uint64_t um = mem & mask, us = s & mask;
    const int64_t  sm = bits == 64 ? static_cast<int64_t>(um) : static_cast<int64_t>(um << (64 - bits)) >> (64 - bits);
    const int64_t  ss = bits == 64 ? static_cast<int64_t>(us) : static_cast<int64_t>(us << (64 - bits)) >> (64 - bits);
    uint64_t       r  = 0;
    switch ( op.b ) {
    case 0: r = um + us; break;
    case 1: r = um & ~us; break;
    case 2: r = um ^ us; break;
    case 3: r = um | us; break;
    case 4: r = sm > ss ? um : us; break;
    case 5: r = sm < ss ? um : us; break;
    case 6: r = um > us ? um : us; break;
    case 7: r = um < us ? um : us; break;
    default: r = us; break;
    }
    op.setX(rf, 0, r & mask);
}

// The value CAS stores. Sources: loaded value, Rs (compare), Rt (new); c = operand bytes.
inline void
casValue(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned bits = op.c * 8;
    const uint64_t mask = bits == 64 ? ~0ull : ((1ull << bits) - 1);
    const uint64_t mem  = op.X(rf, 0) & mask;
    op.setX(rf, 0, (mem == (op.X(rf, 1) & mask)) ? (op.X(rf, 2) & mask) : mem);
}

// The result of a read-modify-write: sources are the store-conditional status, the loaded value and the old Rd. A
// failed store leaves Rd as it was.
inline void
rmwCommit(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned bits = op.c * 8;
    const uint64_t mask = bits == 64 ? ~0ull : ((1ull << bits) - 1);
    op.setX(rf, 0, op.X(rf, 0) == 0 ? (op.X(rf, 1) & mask) : op.X(rf, 2));
}

// A loaded value masked to its width and written to a register (the LSE result in Rs or Rt).
inline void
zeroExtendTo(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const unsigned bits = op.c * 8;
    const uint64_t mask = bits == 64 ? ~0ull : ((1ull << bits) - 1);
    op.setX(rf, 0, op.X(rf, 0) & mask);
}

} // namespace A64Sem
} // namespace Vanadis
} // namespace SST

#endif
