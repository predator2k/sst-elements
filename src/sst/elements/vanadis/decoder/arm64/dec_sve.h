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

// Decoder groups of the integer, predicate and permute instructions of SVE. Member functions of VanadisARM64Decoder;
// included at the end of decoder/varm64decoder.h.

#ifndef _H_VANADIS_ARM64_DEC_SVE
#define _H_VANADIS_ARM64_DEC_SVE

namespace SST {
namespace Vanadis {

inline uint16_t
a64Pred(uint32_t n)
{
    return static_cast<uint16_t>(A64Reg::P0 + n);
}

// DecodePredCount of the Arm architecture.
inline uint64_t
a64PredCount(uint32_t pattern, uint64_t elements)
{
    const uint64_t pow2 = elements ? (1ull << (63 - __builtin_clzll(elements))) : 0;
    switch ( pattern ) {
    case 0: return pow2;
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: return elements >= pattern ? pattern : 0;
    case 9: return elements >= 16 ? 16 : 0;
    case 10: return elements >= 32 ? 32 : 0;
    case 11: return elements >= 64 ? 64 : 0;
    case 12: return elements >= 128 ? 128 : 0;
    case 13: return elements >= 256 ? 256 : 0;
    case 29: return elements - elements % 4;
    case 30: return elements - elements % 3;
    case 31: return elements;
    default: return 0;
    }
}

inline bool
VanadisARM64Decoder::decodeSve(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    switch ( bits(ins, 31, 29) ) {
    case 0: return bit(ins, 24) ? decodeSvePermute(addr, ins, bundle) : decodeSveInt(addr, ins, bundle);
    case 1: return decodeSveCompareAndPred(addr, ins, bundle);
    case 2: return decodeSve2(addr, ins, bundle);
    case 3: return decodeSveFp(addr, ins, bundle);
    default: return decodeSveMem(addr, ins, bundle);
    }
}

// The 0x04 group: integer arithmetic.
inline bool
VanadisARM64Decoder::decodeSveInt(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Sve;
    const uint32_t size = bits(ins, 23, 22);
    const uint16_t pg = a64Pred(bits(ins, 12, 10));
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0), rm = bits(ins, 20, 16);
    if ( !bit(ins, 21) ) {
        const uint32_t top = bits(ins, 15, 13);
        const uint32_t code = bits(ins, 20, 16);
        switch ( top ) {
        case 0: {
            bool valid;
            switch ( code ) {
            case 0: case 1: case 3: case 8: case 9: case 10: case 11: case 12: case 13:
            case 0x10: case 0x12: case 0x13: case 0x18: case 0x19: case 0x1A: case 0x1B:
                valid = true;
                break;
            case 0x14: case 0x15: case 0x16: case 0x17: valid = size >= 2; break;
            default: valid = false; break;
            }
            if ( !valid ) return false;
            auto* i = op(bundle, addr, code >= 0x14 && code <= 0x17 ? "SVE.DIV" : "SVE.BIN", code >= 0x14 && code <= 0x17 ? INST_INT_DIV : INST_FP_ARITH,
                predBin, {}, {}, { pg, rd, rn }, { rd });
            tc(i, svePredBinClass(code));
            i->a = size;
            i->c = code;
            return true;
        }
        case 1: {
            if ( (ins & 0x003E0000) == 0x00100000 ) {
                auto* i = op(bundle, addr, "MOVPRFX.P", INST_FP_ARITH, movprfxPred, {}, {}, { pg, rd, rn }, { rd });
                tc(i, A64Tc::INT_INTERNAL);
                i->a = size;
                i->d = bit(ins, 16);
                return true;
            }
            bool valid;
            switch ( code ) {
            case 0: valid = size != 3; break;
            case 1: case 8: case 9: case 10: case 11: case 0x18: case 0x19: case 0x1A: valid = true; break;
            default: valid = false; break;
            }
            if ( !valid ) return false;
            auto* i = op(bundle, addr, "SVE.REDUCE", INST_FP_ARITH, reduceInt, {}, {}, { pg, rn }, { rd });
            tc(i, sveReduceClass(code, size));
            i->a = size;
            i->c = code;
            return true;
        }
        case 2:
        case 3: {
            auto* i = op(bundle, addr, "MLA.Z", INST_FP_ARITH, predMulAdd, {}, {}, { pg, rd, rn, rm }, { rd });
            tc(i, A64Tc::V_MLA);
            i->a = size;
            i->c = top == 2 ? 0 : 1;
            return true;
        }
        case 4: {
            if ( !bit(ins, 20) ) {
                const uint32_t tsz = (size << 2) | bits(ins, 9, 8);
                if ( tsz == 0 ) return false;
                const uint32_t a = 31 - __builtin_clz(tsz);
                const uint32_t esize = 8u << a;
                const uint32_t full = (tsz << 3) | bits(ins, 7, 5);
                const uint32_t opc = bits(ins, 19, 16);
                const bool left = opc == 3 || opc == 6 || opc == 7 || opc == 15;
                if ( !(opc <= 1 || opc == 3 || opc == 4 || opc == 6 || opc == 7 || opc == 12 || opc == 13 || opc == 15) ) return false;
                auto* i = op(bundle, addr, "SHIFT.PI", INST_FP_ARITH, predShiftImm, {}, {}, { pg, rd }, { rd });
                tc(i, (opc <= 3) ? A64Tc::V_SHIFT : A64Tc::V_SHIFT_COMPLEX);
                i->a = a;
                i->c = opc;
                i->imm = left ? full - esize : 2 * esize - full;
                return true;
            }
            const uint32_t opc = bits(ins, 18, 16);
            const bool wide = bit(ins, 19);
            if ( wide ? (size == 3 || !(opc <= 1 || opc == 3)) : !(opc <= 1 || opc == 3 || opc == 4 || opc == 5 || opc == 7) ) return false;
            auto* i = op(bundle, addr, "SHIFT.PV", INST_FP_ARITH, predShiftVec, {}, {}, { pg, rd, rn }, { rd });
            tc(i, A64Tc::V_SHIFT);
            i->a = size;
            i->c = opc;
            i->d = wide;
            return true;
        }
        case 5: {
            if ( code < 0x10 ) return false;
            bool valid = true;
            switch ( code ) {
            case 0x10: case 0x11: valid = size >= 1; break;
            case 0x12: case 0x13: valid = size >= 2; break;
            case 0x14: case 0x15: valid = size == 3; break;
            case 0x1C: case 0x1D: valid = size >= 1; break;
            case 0x1F: valid = false; break;
            default: break;
            }
            if ( !valid ) return false;
            auto* i = op(bundle, addr, "SVE.UN", INST_FP_ARITH, predUnary, {}, {}, { pg, rd, rn }, { rd });
            tc(i, code <= 0x15 ? A64Tc::V_SHIFT : (code >= 0x18 && code <= 0x1A) || code == 0x1E ? A64Tc::V_MISC : (code == 0x1C || code == 0x1D) ? A64Tc::FP_ALU : A64Tc::V_ALU);
            i->a = size;
            i->c = code;
            return true;
        }
        default: {   // 6, 7: MAD, MSB
            auto* i = op(bundle, addr, "MAD.Z", INST_FP_ARITH, predMulAdd, {}, {}, { pg, rd, rm, rn }, { rd });
            i->setAccumulatorFPRegIn(3, 0);
            tc(i, A64Tc::V_MLA);
            i->a = size;
            i->c = top == 6 ? 2 : 3;
            return true;
        }
        }
    }
    // bit 21 set
    const uint32_t b1510 = bits(ins, 15, 10);
    if ( bits(ins, 15, 13) == 0 ) {
        const uint32_t o = bits(ins, 12, 10);
        if ( o == 2 || o == 3 ) return false;
        auto* i = op(bundle, addr, "SVE.ARITH", INST_FP_ARITH, unpredArith, {}, {}, { rn, rm }, { rd });
        tc(i, A64Tc::V_ALU);
        i->a = size;
        i->c = o;
        return true;
    }
    if ( b1510 == 0b001100 ) {
        auto* i = op(bundle, addr, "SVE.LOGIC", INST_FP_ARITH, unpredArith, {}, {}, { rn, rm }, { rd });
        tc(i, A64Tc::V_ALU);
        i->c = 8 + size;
        return true;
    }
    if ( bits(ins, 15, 11) == 0b00111 || b1510 == 0b001101 ) return decodeSve2Logic(addr, ins, bundle);
    if ( bits(ins, 15, 12) == 0b0100 ) {
        // INDEX
        const uint32_t form = bits(ins, 11, 10);
        const int64_t  imm_n = sext(bits(ins, 9, 5), 5), imm_m = sext(bits(ins, 20, 16), 5);
        std::vector<uint16_t> in;
        uint32_t d = 0;
        if ( form & 1 ) {
            in.push_back(rz(rn));
            d |= 1;
        }
        if ( form & 2 ) {
            in.push_back(rz(rm));
            d |= 2;
        }
        auto* i = op(bundle, addr, "INDEX", INST_FP_ARITH, index, in, {}, {}, { rd });
        tc(i, in.empty() ? A64Tc::Z_INDEX_IMM : A64Tc::Z_INDEX_GP);
        i->a = size;
        i->d = d;
        i->imm = static_cast<uint64_t>(imm_n);
        i->imm2 = static_cast<uint64_t>(imm_m);
        return true;
    }
    if ( (ins & 0xFFA0F000) == 0x04205000 ) {
        // ADDVL, ADDPL, and RDVL below
        const uint64_t scale = bit(ins, 22) ? vector_bytes / 8 : vector_bytes;
        addImmTo(bundle, addr, rs(rd), rs(rm), sext(bits(ins, 10, 5), 6) * static_cast<int64_t>(scale));
        tc(pending_, A64Tc::P_COUNT);
        return true;
    }
    if ( (ins & 0xFFFFF800) == 0x04BF5000 ) {
        if ( rd == 31 ) {
            add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
            return true;
        }
        tc(op(bundle, addr, "RDVL", INST_INT_ARITH, A64Sem::movImm, {}, { rd }), A64Tc::P_COUNT)->imm =
            static_cast<uint64_t>(sext(bits(ins, 10, 5), 6) * static_cast<int64_t>(vector_bytes));
        return true;
    }
    if ( bits(ins, 15, 12) == 0b0110 || bits(ins, 15, 11) == 0b01110 ) return decodeSve2MulUnpred(addr, ins, bundle);
    if ( bits(ins, 15, 13) == 0b100 ) {
        const uint32_t o = bits(ins, 11, 10);
        if ( o == 2 ) return false;
        if ( !bit(ins, 12) ) {
            if ( size == 3 ) return false;
            auto* i = op(bundle, addr, "SHIFT.W", INST_FP_ARITH, unpredShift, {}, {}, { rn, rm }, { rd });
            tc(i, A64Tc::V_SHIFT);
            i->a = size;
            i->c = o;
            i->d = 1;
            return true;
        }
        const uint32_t tsz = (size << 2) | bits(ins, 20, 19);
        if ( tsz == 0 ) return false;
        const uint32_t a = 31 - __builtin_clz(tsz);
        const uint32_t esize = 8u << a;
        const uint32_t full = (tsz << 3) | bits(ins, 18, 16);
        auto* i = op(bundle, addr, "SHIFT.I", INST_FP_ARITH, unpredShift, {}, {}, { rn }, { rd });
        tc(i, A64Tc::V_SHIFT);
        i->a = a;
        i->c = o;
        i->imm = o == 3 ? full - esize : 2 * esize - full;
        return true;
    }
    if ( bits(ins, 15, 12) == 0b1010 ) {
        // ADR
        uint32_t a = 3, d = 0;
        if ( size >= 2 ) a = (size & 1) ? 3 : 2;
        else d = size == 0 ? 1 : 2;
        auto* i = op(bundle, addr, "ADR.Z", INST_FP_ARITH, adr, {}, {}, { rn, rm }, { rd });
        tc(i, A64Tc::V_ALU);
        i->a = a;
        i->c = bits(ins, 11, 10);
        i->d = d;
        return true;
    }
    if ( b1510 == 0b101111 ) {
        if ( (ins & 0xFFFFFC00) != 0x0420BC00 ) return false;
        // MOVPRFX fuses with the instruction after it (X925 guide, section 4.15): the copy takes no time.
        tc(op(bundle, addr, "MOVPRFX", INST_FP_ARITH, copyZ, {}, {}, { rn }, { rd }), A64Tc::INT_INTERNAL);
        return true;
    }
    if ( bits(ins, 15, 12) == 0b1011 ) return decodeSveFpMiscInt(addr, ins, bundle);
    if ( bits(ins, 15, 14) == 3 ) {
        // Element counts.
        const uint64_t elements = vector_bytes >> size;
        const uint64_t count = a64PredCount(bits(ins, 9, 5), elements) * (bits(ins, 19, 16) + 1);
        const bool     b20 = bit(ins, 20);
        if ( bits(ins, 15, 11) == 0b11100 && !b20 ) {
            // CNT
            if ( bit(ins, 10) ) return false;
            if ( rd == 31 ) {
                add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
                return true;
            }
            tc(op(bundle, addr, "CNT", INST_INT_ARITH, A64Sem::movImm, {}, { rd }), A64Tc::Z_COUNT_GP)->imm = count;
            return true;
        }
        if ( bits(ins, 15, 11) == 0b11100 ) {
            // INC, DEC of a general register
            auto* i = op(bundle, addr, "INC.X", INST_INT_ARITH, incScalarImm, { rz(rd) }, { rd == 31 ? A64Reg::T1 : rd });
            tc(i, A64Tc::Z_COUNT_GP);
            i->c = bit(ins, 10) ? 1 : 0;
            i->imm = count;
            return true;
        }
        if ( bits(ins, 15, 12) == 0b1111 ) {
            // SQINC, UQINC, SQDEC, UQDEC of a general register; bit 20 = 64-bit
            auto* i = op(bundle, addr, "SQINC.X", INST_INT_ARITH, incScalarImm, { rz(rd) }, { rd == 31 ? A64Reg::T1 : rd });
            tc(i, A64Tc::P_COUNT);
            const bool D = bit(ins, 11), U = bit(ins, 10);
            i->c = (U ? 4u : 2u) + (D ? 1u : 0u);
            i->d = b20 ? 0 : 1;
            i->imm = count;
            return true;
        }
        if ( bits(ins, 15, 12) == 0b1100 ) {
            if ( size == 0 ) return false;
            const bool D = bit(ins, 11), U = bit(ins, 10);
            auto* i = op(bundle, addr, "INC.Z", INST_FP_ARITH, incVec, {}, {}, { rd }, { rd });
            tc(i, A64Tc::V_ALU);
            i->a = size;
            if ( b20 ) {
                if ( D ) return false;
                i->c = U ? 1 : 0;
            }
            else {
                i->c = (U ? 4u : 2u) + (D ? 1u : 0u);
            }
            i->imm = count;
            return true;
        }
    }
    return false;
}

// The 0x05 group: logic immediates, DUP, permutes.
inline bool
VanadisARM64Decoder::decodeSvePermute(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Sve;
    const uint32_t size = bits(ins, 23, 22);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0), rm = bits(ins, 20, 16);
    const uint16_t pg3 = a64Pred(bits(ins, 12, 10));
    if ( !bit(ins, 21) ) {
        if ( (ins & 0xFF300000) == 0x05000000 || (ins & 0xFF300000) == 0x05100000 + 0xFFFFFFFF ) {}
        if ( (ins & 0xFF3C0000) == (ins & 0xFF000000) && bits(ins, 19, 18) == 0 && bits(ins, 21, 20) == 0 ) {
            // AND, EOR, ORR, DUPM immediate
            uint64_t wmask, tmask;
            if ( !decodeBitMasks(bit(ins, 17), bits(ins, 10, 5), bits(ins, 16, 11), true, &wmask, &tmask) ) return false;
            const uint32_t kind = size;   // 0 ORR, 1 EOR, 2 AND, 3 DUPM
            auto* i = op(bundle, addr, "LOGIC.I", INST_FP_ARITH, logicImm, {}, {},
                kind == 3 ? std::vector<uint16_t> {} : std::vector<uint16_t> { rd }, { rd });
            tc(i, A64Tc::V_ALU);
            i->c = kind;
            i->imm = wmask;
            return true;
        }
        if ( (ins & 0xFF300000) == 0x05100000 ) {
            // CPY immediate, FCPY
            const uint16_t pg = a64Pred(bits(ins, 19, 16));
            const uint32_t b1513 = bits(ins, 15, 13);
            const uint32_t imm8 = bits(ins, 12, 5);
            if ( b1513 == 0b110 ) {
                if ( size == 0 ) return false;
                const uint64_t v = size == 1 ? a64FpExpandImm(imm8, 5, 10) : size == 2 ? a64FpExpandImm(imm8, 8, 23) : a64FpExpandImm(imm8, 11, 52);
                auto* i = op(bundle, addr, "FCPY", INST_FP_ARITH, cpyPred, {}, {}, { pg, rd }, { rd });
                tc(i, A64Tc::V_ALU);
                i->a = size;
                i->d = 1;
                i->imm = v;
                return true;
            }
            if ( bit(ins, 15) ) return false;
            const bool sh = bit(ins, 13), merge = bit(ins, 14);
            if ( sh && size == 0 ) return false;
            const int64_t v = sext(imm8, 8) * (sh ? 256 : 1);
            auto* i = op(bundle, addr, "CPY.I", INST_FP_ARITH, cpyPred, {}, {},
                merge ? std::vector<uint16_t> { pg, rd } : std::vector<uint16_t> { pg }, { rd });
            tc(i, A64Tc::V_ALU);
            i->a = size;
            i->d = merge;
            i->imm = static_cast<uint64_t>(v);
            return true;
        }
        return false;
    }
    if ( bits(ins, 31, 21) == 0b00000101001 && bits(ins, 15, 13) == 0 ) {
        // EXT, destructive
        auto* i = op(bundle, addr, "EXT.Z", INST_FP_ARITH, extZ, {}, {}, { rd, rn }, { rd });
        tc(i, A64Tc::V_MISC);
        i->imm = (bits(ins, 20, 16) << 3) | bits(ins, 12, 10);
        return true;
    }
    if ( bits(ins, 31, 21) == 0b00000101011 && bits(ins, 15, 13) == 0 ) {
        // EXT, constructive
        auto* i = op(bundle, addr, "EXT.Z2", INST_FP_ARITH, extZ, {}, {}, { rn, static_cast<uint16_t>((rn + 1) % 32) }, { rd });
        tc(i, A64Tc::V_MISC);
        i->imm = (bits(ins, 20, 16) << 3) | bits(ins, 12, 10);
        return true;
    }
    if ( bits(ins, 15, 10) == 0b001000 ) {
        // DUP (indexed)
        const uint32_t imm2 = size, tsz = bits(ins, 20, 16);
        const uint32_t full = (imm2 << 5) | tsz;
        if ( tsz == 0 ) return false;
        const uint32_t a = __builtin_ctz(tsz);
        if ( a > 4 ) return false;
        if ( a == 4 ) {
            // A 128-bit element.
            auto* i = op(bundle, addr, "DUP.ZQ", INST_FP_ARITH, dupQuad, {}, {}, { rn }, { rd });
            tc(i, A64Tc::V_MISC);
            i->imm = full >> 5;
            return true;
        }
        auto* i = op(bundle, addr, "DUP.ZE", INST_FP_ARITH, dupIndexed, {}, {}, { rn }, { rd });
        tc(i, A64Tc::V_MISC);
        i->a = a;
        i->imm = full >> (a + 1);
        return true;
    }
    if ( bits(ins, 15, 10) == 0b001100 ) {
        auto* i = op(bundle, addr, "TBL.Z", INST_FP_ARITH, tableZ, {}, {}, { rn, rm }, { rd });
        tc(i, A64Tc::V_MISC);
        i->a = size;
        i->c = 0;
        return true;
    }
    if ( bits(ins, 15, 10) == 0b001010 ) {
        auto* i = op(bundle, addr, "TBL2.Z", INST_FP_ARITH, tableZ, {}, {}, { rn, static_cast<uint16_t>((rn + 1) % 32), rm }, { rd });
        tc(i, A64Tc::V_MISC);
        i->a = size;
        i->c = 1;
        return true;
    }
    if ( bits(ins, 15, 10) == 0b001011 ) {
        auto* i = op(bundle, addr, "TBX.Z", INST_FP_ARITH, tableZ, {}, {}, { rn, rm, rd }, { rd });
        tc(i, A64Tc::V_MISC);
        i->a = size;
        i->c = 2;
        return true;
    }
    if ( bits(ins, 15, 10) == 0b001110 ) {
        const uint32_t code = bits(ins, 20, 16);
        switch ( code ) {
        case 0: {
            auto* i = op(bundle, addr, "DUP.ZG", INST_FP_ARITH, dupGp, { rs(rn) }, {}, {}, { rd });
            tc(i, A64Tc::Z_DUP_GP);
            i->a = size;
            return true;
        }
        case 4: {
            auto* i = op(bundle, addr, "INSR.G", INST_FP_ARITH, insr, { rz(rn) }, {}, { rd }, { rd });
            tc(i, A64Tc::Z_INSR_GP);
            i->a = size;
            return true;
        }
        case 0x14: {
            auto* i = op(bundle, addr, "INSR.V", INST_FP_ARITH, insr, {}, {}, { rd, rn }, { rd });
            tc(i, A64Tc::V_MISC);
            i->a = size;
            i->d = 1;
            return true;
        }
        case 0x18: {
            auto* i = op(bundle, addr, "REV.Z", INST_FP_ARITH, revZ, {}, {}, { rn }, { rd });
            tc(i, A64Tc::V_MISC);
            i->a = size;
            return true;
        }
        case 0x10: case 0x11: case 0x12: case 0x13: {
            if ( size == 0 ) return false;
            auto* i = op(bundle, addr, "UNPK", INST_FP_ARITH, unpack, {}, {}, { rn }, { rd });
            tc(i, A64Tc::V_MISC);
            i->a = size - 1;
            i->c = code & 3;
            return true;
        }
        default: return false;
        }
    }
    if ( bits(ins, 15, 13) == 0b010 ) {
        // Permutes of predicates. Register operands are predicates.
        const uint16_t pd = a64Pred(bits(ins, 3, 0)), pn = a64Pred(bits(ins, 8, 5)), pm = a64Pred(bits(ins, 19, 16));
        if ( (ins & 0xFFFFFE10) == 0x05304000 || (ins & 0xFFFFFE10) == 0x05314000 ) {
            tc(op(bundle, addr, "PUNPK", INST_FP_ARITH, punpk, {}, {}, { pn }, { pd }), A64Tc::P_PERMUTE)->c = bit(ins, 16);
            return true;
        }
        if ( (ins & 0xFF3FFE10) == 0x05344000 ) {
            tc(op(bundle, addr, "REV.P", INST_FP_ARITH, revP, {}, {}, { pn }, { pd }), A64Tc::P_PERMUTE)->a = size;
            return true;
        }
        if ( (ins & 0xFF30E210) == 0x05204000 ) {
            const uint32_t kind = bits(ins, 12, 10);
            static constexpr int by_kind[8] = { 0, 1, 2, 3, 4, 5, -1, -1 };
            if ( by_kind[kind] < 0 ) return false;
            auto* i = op(bundle, addr, "PERM.P", INST_FP_ARITH, permuteP, {}, {}, { pn, pm }, { pd });
            tc(i, A64Tc::P_PERMUTE);
            i->a = size;
            i->c = static_cast<uint32_t>(by_kind[kind]);
            return true;
        }
        return false;
    }
    if ( bits(ins, 15, 13) == 0b011 ) {
        const uint32_t kind = bits(ins, 12, 10);
        if ( kind > 5 ) return false;
        auto* i = op(bundle, addr, "PERM.Z", INST_FP_ARITH, permuteZ, {}, {}, { rn, rm }, { rd });
        tc(i, A64Tc::V_MISC);
        i->a = size;
        i->c = kind;
        return true;
    }
    if ( (ins & 0xFFE0E000) == 0x05A00000 ) {
        // ZIP1, ZIP2, UZP1, UZP2 and, at 110 and 111, TRN1, TRN2 of quadwords
        const uint32_t kind = bits(ins, 12, 10);
        if ( kind == 4 || kind == 5 ) return false;
        auto* i = op(bundle, addr, "PERM.Q", INST_FP_ARITH, permuteZ, {}, {}, { rn, rm }, { rd });
        tc(i, A64Tc::V_MISC);
        i->c = kind >= 6 ? kind - 2 : kind;
        i->d = 1;
        return true;
    }
    if ( bits(ins, 15, 14) == 3 ) {
        auto* i = op(bundle, addr, "SEL.Z", INST_FP_ARITH, selZ, {}, {}, { a64Pred(bits(ins, 13, 10)), rn, rm }, { rd });
        tc(i, A64Tc::V_ALU);
        i->a = size;
        return true;
    }
    if ( bits(ins, 15, 13) == 0b100 ) {
        const uint32_t code = bits(ins, 20, 16);
        switch ( code ) {
        case 0x00: {
            auto* i = op(bundle, addr, "CPY.V", INST_FP_ARITH, cpyPred, {}, {}, { pg3, rd, rn }, { rd });
            tc(i, A64Tc::V_ALU);
            i->a = size;
            i->c = 2;
            i->d = 1;
            return true;
        }
        case 0x01: {
            if ( size < 2 ) return false;
            auto* i = op(bundle, addr, "COMPACT", INST_FP_ARITH, compact, {}, {}, { pg3, rn }, { rd });
            tc(i, A64Tc::Z_COMPACT);
            i->a = size;
            return true;
        }
        case 0x02: case 0x03: {
            auto* i = op(bundle, addr, "LAST.V", INST_FP_ARITH, lastElement, {}, {}, { pg3, rn }, { rd });
            tc(i, A64Tc::Z_LAST_V);
            i->a = size;
            i->c = code & 1;
            i->d = 1;
            return true;
        }
        case 0x04: case 0x05: case 0x06: {
            const uint32_t unit = code - 4;
            if ( size <= unit ) return false;
            auto* i = op(bundle, addr, "REV.E", INST_FP_ARITH, predRevInElem, {}, {}, { pg3, rd, rn }, { rd });
            tc(i, A64Tc::V_MISC);
            i->a = size;
            i->d = unit;
            return true;
        }
        case 0x07: {
            auto* i = op(bundle, addr, "RBIT.Z", INST_FP_ARITH, predUnary, {}, {}, { pg3, rd, rn }, { rd });
            tc(i, A64Tc::V_MISC);
            i->a = size;
            i->c = 0x20;
            return true;
        }
        case 0x08: case 0x09: {
            auto* i = op(bundle, addr, "CLAST.Z", INST_FP_ARITH, clast, {}, {}, { pg3, rd, rn }, { rd });
            tc(i, A64Tc::Z_COMPACT);
            i->a = size;
            i->c = code & 1;
            i->d = 2;
            return true;
        }
        case 0x0A: case 0x0B: {
            auto* i = op(bundle, addr, "CLAST.V", INST_FP_ARITH, clast, {}, {}, { pg3, rd, rn }, { rd });
            tc(i, A64Tc::Z_COMPACT);
            i->a = size;
            i->c = code & 1;
            i->d = 1;
            return true;
        }
        case 0x0C: {
            auto* i = op(bundle, addr, "SPLICE", INST_FP_ARITH, splice, {}, {}, { pg3, rd, rn }, { rd });
            tc(i, A64Tc::Z_COMPACT);
            i->a = size;
            return true;
        }
        case 0x0D: {
            auto* i = op(bundle, addr, "SPLICE.2", INST_FP_ARITH, splice, {}, {}, { pg3, rn, static_cast<uint16_t>((rn + 1) % 32) }, { rd });
            tc(i, A64Tc::Z_COMPACT);
            i->a = size;
            return true;
        }
        default: return false;
        }
    }
    if ( bits(ins, 15, 13) == 0b101 ) {
        const uint32_t code = bits(ins, 20, 16);
        const uint16_t rd_or_sink = rd == 31 ? A64Reg::T1 : rd;
        switch ( code ) {
        case 0x00: case 0x01: {
            auto* i = op(bundle, addr, "LAST.R", INST_INT_ARITH, lastElement, {}, { rd_or_sink }, { pg3, rn }, {});
            tc(i, A64Tc::Z_LAST_GP);
            i->a = size;
            i->c = code & 1;
            return true;
        }
        case 0x08: {
            auto* i = op(bundle, addr, "CPY.R", INST_FP_ARITH, cpyPred, { rs(rn) }, {}, { pg3, rd }, { rd });
            tc(i, A64Tc::Z_CPY_GP);
            i->a = size;
            i->c = 1;
            i->d = 1;
            return true;
        }
        case 0x10: case 0x11: {
            auto* i = op(bundle, addr, "CLAST.R", INST_INT_ARITH, clast, { rz(rd) }, { rd_or_sink }, { pg3, rn }, {});
            tc(i, A64Tc::Z_CLAST_GP);
            i->a = size;
            i->c = code & 1;
            i->d = 0;
            return true;
        }
        default: return false;
        }
    }
    return false;
}

// The 0x24 and 0x25 groups: compares, predicates, WHILE, counts, wide immediates.
inline bool
VanadisARM64Decoder::decodeSveCompareAndPred(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Sve;
    const uint32_t size = bits(ins, 23, 22);
    const uint16_t zn = bits(ins, 9, 5), zm = bits(ins, 20, 16);
    const uint16_t pd = a64Pred(bits(ins, 3, 0));
    const uint16_t pg3 = a64Pred(bits(ins, 12, 10));
    const bool     b4 = bit(ins, 4);
    if ( !bit(ins, 24) ) {
        if ( !bit(ins, 21) ) {
            // Compare vectors.
            const uint32_t top = bits(ins, 15, 13);
            int code = -1;
            bool wide = false;
            switch ( top ) {
            case 0: code = b4 ? 7 : 6; break;   // HS, HI
            case 1: code = b4 ? 1 : 0; wide = true; break;   // EQ, NE wide
            case 2: code = b4 ? 3 : 2; wide = true; break;   // GE, GT wide
            case 3: code = b4 ? 5 : 4; wide = true; break;   // LT, LE wide
            case 4: code = b4 ? 3 : 2; break;   // GE, GT
            case 5: code = b4 ? 1 : 0; break;   // EQ, NE
            case 6: code = b4 ? 7 : 6; wide = true; break;   // HS, HI wide
            default: code = b4 ? 9 : 8; wide = true; break;   // LO, LS wide
            }
            if ( wide && size == 3 ) return false;
            auto* i = op(bundle, addr, "CMP.Z", INST_FP_ARITH, compareInt, {}, { A64Reg::NZCV }, { pg3, zn, zm }, { pd });
            tc(i, A64Tc::Z_CMP);
            i->a = size;
            i->c = static_cast<uint32_t>(code);
            i->d = wide;
            return true;
        }
        // Compare with an unsigned immediate.
        const bool lt = bit(ins, 13);
        auto* i = op(bundle, addr, "CMP.UI", INST_FP_ARITH, compareInt, {}, { A64Reg::NZCV }, { pg3, zn }, { pd });
        tc(i, A64Tc::Z_CMP);
        i->a = size;
        i->c = lt ? (b4 ? 9 : 8) : (b4 ? 7 : 6);
        i->d = 2;
        i->imm = bits(ins, 20, 14);
        return true;
    }
    if ( !bit(ins, 21) ) {
        const uint32_t top = bits(ins, 15, 14);
        if ( top == 1 ) {
            // AND, BIC, EOR, SEL, ORR, ORN, NOR, NAND of predicates
            if ( b4 && bit(ins, 9) && !bit(ins, 23) && bit(ins, 22) ) return false;   // SELS does not exist
            const bool S = bit(ins, 22);
            const uint32_t code = (static_cast<uint32_t>(bit(ins, 23)) << 2) | (static_cast<uint32_t>(bit(ins, 9)) << 1) | (b4 ? 1u : 0u);
            if ( (ins & 0xFF00C000) == 0x25004000 && bits(ins, 21, 20) == 0 ) {
                auto* i = op(bundle, addr, "PLOGIC", INST_FP_ARITH, predLogic, {}, S ? std::vector<uint16_t> { A64Reg::NZCV } : std::vector<uint16_t> {},
                    { a64Pred(bits(ins, 13, 10)), a64Pred(bits(ins, 8, 5)), a64Pred(bits(ins, 19, 16)) }, { pd });
                tc(i, A64Tc::P_LOGIC);
                i->c = code;
                i->d = S;
                return true;
            }
            // BRK group.
            if ( (ins & 0xFF3FC200) == 0x25104000 && bits(ins, 23, 22) != 0xFF ) {
                const bool S2 = bit(ins, 22), B = bit(ins, 23);
                const uint16_t pg = a64Pred(bits(ins, 13, 10)), pn = a64Pred(bits(ins, 8, 5));
                if ( S2 && b4 ) return false;
                auto* i = op(bundle, addr, "BRK", INST_FP_ARITH, brk, {}, S2 ? std::vector<uint16_t> { A64Reg::NZCV } : std::vector<uint16_t> {},
                    b4 ? std::vector<uint16_t> { pg, pn, pd } : std::vector<uint16_t> { pg, pn }, { pd });
                tc(i, A64Tc::P_BREAK);
                i->c = B;
                i->d = (b4 ? 1u : 0u) | (S2 ? 2u : 0u);
                return true;
            }
            if ( (ins & 0xFFBFC210) == 0x25184000 ) {
                const bool S2 = bit(ins, 22);
                auto* i = op(bundle, addr, "BRKN", INST_FP_ARITH, brkn, {}, S2 ? std::vector<uint16_t> { A64Reg::NZCV } : std::vector<uint16_t> {},
                    { a64Pred(bits(ins, 13, 10)), a64Pred(bits(ins, 8, 5)), pd }, { pd });
                tc(i, A64Tc::P_BREAK_PROPAGATE);
                i->d = S2 ? 2 : 0;
                return true;
            }
            return false;
        }
        if ( top == 3 ) {
            if ( (ins & 0xFFB0C200) == 0x2500C000 ) {
                // BRKPA, BRKPB
                const bool S2 = bit(ins, 22);
                auto* i = op(bundle, addr, "BRKP", INST_FP_ARITH, brkp, {}, S2 ? std::vector<uint16_t> { A64Reg::NZCV } : std::vector<uint16_t> {},
                    { a64Pred(bits(ins, 13, 10)), a64Pred(bits(ins, 8, 5)), a64Pred(bits(ins, 19, 16)) }, { pd });
                tc(i, A64Tc::P_BREAK_PROPAGATE);
                i->c = b4;
                i->d = S2 ? 2 : 0;
                return true;
            }
            if ( (ins & 0xFFFFFE10) == 0x2558C000 ) {
                tc(op(bundle, addr, "PFIRST", INST_FP_ARITH, pfirst, {}, { A64Reg::NZCV }, { a64Pred(bits(ins, 8, 5)), pd }, { pd }), A64Tc::P_FIRST);
                return true;
            }
            if ( (ins & 0xFF3FFE10) == 0x2519C400 ) {
                tc(op(bundle, addr, "PNEXT", INST_FP_ARITH, pnext, {}, { A64Reg::NZCV }, { a64Pred(bits(ins, 8, 5)), pd }, { pd }), A64Tc::P_FIRST)->a = size;
                return true;
            }
            if ( (ins & 0xFFFFC21F) == 0x2550C000 ) {
                tc(op(bundle, addr, "PTEST", INST_FP_ARITH, ptest, {}, { A64Reg::NZCV }, { a64Pred(bits(ins, 13, 10)), a64Pred(bits(ins, 8, 5)) }, {}), A64Tc::P_TEST);
                return true;
            }
            if ( (ins & 0xFF3EFC10) == 0x2518E000 ) {
                const bool S2 = bit(ins, 16);
                auto* i = op(bundle, addr, "PTRUE", INST_FP_ARITH, predInit, {}, S2 ? std::vector<uint16_t> { A64Reg::NZCV } : std::vector<uint16_t> {}, {}, { pd });
                tc(i, A64Tc::P_SET);
                i->a = size;
                i->d = S2;
                i->imm = a64PredCount(bits(ins, 9, 5), vector_bytes >> size);
                return true;
            }
            if ( (ins & 0xFFFFFFF0) == 0x2518E400 ) {
                auto* i = op(bundle, addr, "PFALSE", INST_FP_ARITH, predInit, {}, {}, {}, { pd });
                tc(i, A64Tc::P_SET);
                i->imm = 0;
                return true;
            }
            if ( (ins & 0xFFBFFE10) == 0x2518F000 ) {
                const bool S2 = bit(ins, 22);
                auto* i = op(bundle, addr, "RDFFR.P", INST_FP_ARITH, rdffrPred, {}, S2 ? std::vector<uint16_t> { A64Reg::NZCV } : std::vector<uint16_t> {},
                    { a64Pred(bits(ins, 8, 5)), A64Reg::FFR }, { pd });
                tc(i, A64Tc::P_FFR_READ);
                i->d = S2;
                return true;
            }
            if ( (ins & 0xFFFFFFF0) == 0x2519F000 ) {
                tc(op(bundle, addr, "RDFFR", INST_FP_ARITH, copyP, {}, {}, { A64Reg::FFR }, { pd }), A64Tc::P_FFR_READ);
                return true;
            }
            return false;
        }
        // Compare with a signed immediate: bits 15..13 are 000, 001 or 100.
        const uint32_t b1513 = bits(ins, 15, 13);
        int code;
        if ( b1513 == 0 ) code = b4 ? 3 : 2;
        else if ( b1513 == 1 ) code = b4 ? 5 : 4;
        else if ( b1513 == 4 ) code = b4 ? 1 : 0;
        else return false;
        auto* i = op(bundle, addr, "CMP.SI", INST_FP_ARITH, compareInt, {}, { A64Reg::NZCV }, { pg3, zn }, { pd });
        tc(i, A64Tc::Z_CMP);
        i->a = size;
        i->c = static_cast<uint32_t>(code);
        i->d = 2;
        i->imm = static_cast<uint64_t>(sext(bits(ins, 20, 16), 5));
        return true;
    }
    // 0x25, bit 21 set
    const uint32_t b1514 = bits(ins, 15, 14);
    if ( b1514 == 0 ) {
        const uint32_t rn = bits(ins, 9, 5), rm = bits(ins, 20, 16);
        if ( (ins & 0xFF20EC00) == 0x25200000 || (ins & 0xFF20EC00) == 0x25200400 || (ins & 0xFF20EC00) == 0x25200800 || (ins & 0xFF20EC00) == 0x25200C00 ) {
            // WHILE: bit 12 = 64-bit operands (sf)
        }
        if ( bits(ins, 15, 13) == 0 ) {
            const bool sf = bit(ins, 12), lt = bit(ins, 10), U = bit(ins, 11);
            const uint32_t kind = lt ? (b4 ? 1u : 0u) : (b4 ? 3u : 2u);
            auto* i = op(bundle, addr, "WHILE", INST_FP_ARITH, whilePred, { rz(rn), rz(rm) }, { A64Reg::NZCV }, {}, { pd });
            tc(i, A64Tc::P_WHILE);
            i->a = size;
            i->c = (U ? 8u : 0u) | kind;
            i->d = sf ? 0 : 1;
            return true;
        }
        if ( (ins & 0xFF20FC00) == 0x25203000 ) {
            auto* i = op(bundle, addr, "WHILEW", INST_FP_ARITH, whileConflict, { rz(rn), rz(rm) }, { A64Reg::NZCV }, {}, { pd });
            tc(i, A64Tc::P_WHILE);
            i->a = size;
            i->c = b4;
            return true;
        }
        if ( (ins & 0xFFA0FC0F) == 0x25A02000 ) {
            // bits 3..0 are 0, bit 4 selects NE.
            auto* i = op(bundle, addr, "CTERM", INST_INT_ARITH, cterm, { rz(rn), rz(rm), A64Reg::NZCV }, { A64Reg::NZCV });
            tc(i, A64Tc::P_WHILE);
            i->c = b4;
            i->d = bit(ins, 22) ? 0 : 1;
            return true;
        }
        return false;
    }
    if ( b1514 == 2 ) {
        const uint32_t rdn = bits(ins, 4, 0);
        const uint16_t rd16 = rdn;
        const uint16_t pm = a64Pred(bits(ins, 8, 5));
        if ( (ins & 0xFF3FC200) == 0x25208000 ) {
            auto* i = op(bundle, addr, "CNTP", INST_INT_ARITH, cntp, {}, { rdn == 31 ? A64Reg::T1 : rd16 },
                { a64Pred(bits(ins, 13, 10)), pm }, {});
            tc(i, A64Tc::P_COUNT);
            i->a = size;
            return true;
        }
        if ( (ins & 0xFFFFFE1F) == 0x25289000 ) {
            tc(op(bundle, addr, "WRFFR", INST_FP_ARITH, copyP, {}, {}, { pm }, { A64Reg::FFR }), A64Tc::P_FFR_WRITE);
            return true;
        }
        if ( ins == 0x252C9000 ) {
            auto* i = op(bundle, addr, "SETFFR", INST_FP_ARITH, predInit, {}, {}, {}, { A64Reg::FFR });
            tc(i, A64Tc::P_FFR_WRITE);
            i->imm = vector_bytes;
            return true;
        }
        if ( (ins & 0xFF38F000) == 0x25288000 ) {
            // SQINCP, UQINCP, SQDECP, UQDECP, INCP, DECP: bits 18..16 = INC kind, bit 11 = scalar, bit 10 = 64-bit
            const uint32_t kind = bits(ins, 18, 16);
            const bool scalar = bit(ins, 11), is64 = bit(ins, 10);
            uint32_t c;
            switch ( kind ) {
            case 0: c = 2; break;
            case 1: c = 4; break;
            case 2: c = 3; break;
            case 3: c = 5; break;
            case 4: c = 0; break;
            case 5: c = 1; break;
            default: return false;
            }
            if ( bits(ins, 15, 12) != 0b1000 ) return false;
            if ( scalar ) {
                auto* i = op(bundle, addr, "INCP.X", INST_INT_ARITH, incpScalar, { rz(rdn) }, { rdn == 31 ? A64Reg::T1 : rd16 }, { pm }, {});
                tc(i, A64Tc::P_COUNT);
                i->a = size;
                i->c = c;
                i->d = (kind <= 3 && !is64) ? 1 : 0;
                return true;
            }
            if ( size == 0 ) return false;
            auto* i = op(bundle, addr, "INCP.Z", INST_FP_ARITH, incVec, {}, {}, { rd16, pm }, { rd16 });
            tc(i, A64Tc::P_COUNT_VECTOR);
            i->a = size;
            i->c = c;
            i->d = 2;
            return true;
        }
        return false;
    }
    if ( b1514 == 3 ) {
        const uint16_t rd = bits(ins, 4, 0);
        const uint32_t imm8 = bits(ins, 12, 5);
        const bool     sh = bit(ins, 13);
        const uint32_t k = bits(ins, 20, 16);
        // Arithmetic with an immediate, DUP, FDUP, MUL, SMAX...
        if ( k <= 7 ) {
            if ( k == 2 ) return false;
            if ( sh && size == 0 ) return false;
            auto* i = op(bundle, addr, "ARITH.I", INST_FP_ARITH, arithImm, {}, {}, { rd }, { rd });
            tc(i, A64Tc::V_ALU);
            i->a = size;
            i->c = k;
            i->imm = static_cast<uint64_t>(imm8) << (sh ? 8 : 0);
            return true;
        }
        if ( k >= 8 && k <= 11 ) {
            if ( sh ) return false;
            auto* i = op(bundle, addr, "MINMAX.I", INST_FP_ARITH, arithImm, {}, {}, { rd }, { rd });
            tc(i, A64Tc::V_ALU);
            i->a = size;
            i->c = k;
            i->imm = (k & 1) ? static_cast<uint64_t>(imm8) : static_cast<uint64_t>(sext(imm8, 8));
            return true;
        }
        if ( k == 0x10 ) {
            if ( sh ) return false;
            auto* i = op(bundle, addr, "MUL.I", INST_FP_ARITH, arithImm, {}, {}, { rd }, { rd });
            tc(i, A64Tc::V_MUL);
            i->a = size;
            i->c = 16;
            i->imm = static_cast<uint64_t>(sext(imm8, 8));
            return true;
        }
        if ( k == 0x18 ) {
            if ( sh && size == 0 ) return false;
            const int64_t v = sext(imm8, 8) * (sh ? 256 : 1);
            uint64_t pat = 0;
            const unsigned esize = 8u << size;
            for ( unsigned b = 0; b < 64; b += esize ) {
                const uint64_t mask = esize == 64 ? ~0ull : ((1ull << esize) - 1);
                pat |= (static_cast<uint64_t>(v) & mask) << b;
            }
            auto* i = op(bundle, addr, "DUP.I", INST_FP_ARITH, logicImm, {}, {}, {}, { rd });
            tc(i, A64Tc::V_ALU);
            i->c = 3;
            i->imm = pat;
            return true;
        }
        if ( k == 0x19 ) {
            if ( sh || size == 0 ) return false;
            const uint64_t v = size == 1 ? a64FpExpandImm(imm8, 5, 10) : size == 2 ? a64FpExpandImm(imm8, 8, 23) : a64FpExpandImm(imm8, 11, 52);
            uint64_t pat = 0;
            const unsigned esize = 8u << size;
            for ( unsigned b = 0; b < 64; b += esize ) pat |= v << b;
            auto* i = op(bundle, addr, "FDUP", INST_FP_ARITH, logicImm, {}, {}, {}, { rd });
            tc(i, A64Tc::V_ALU);
            i->c = 3;
            i->imm = pat;
            return true;
        }
    }
    return false;
}

} // namespace Vanadis
} // namespace SST

#endif
