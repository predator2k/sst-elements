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

// Decoder groups of the integer instructions of SVE2. Member functions of VanadisARM64Decoder; included at the end of
// decoder/varm64decoder.h.

#ifndef _H_VANADIS_ARM64_DEC_SVE2
#define _H_VANADIS_ARM64_DEC_SVE2

namespace SST {
namespace Vanadis {

// BCAX, BSL, BSL1N, BSL2N, EOR3, NBSL, XAR in the 0x04 group.
inline bool
VanadisARM64Decoder::decodeSve2Logic(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Sve2;
    const uint32_t size = bits(ins, 23, 22);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0), rm = bits(ins, 20, 16);
    if ( bits(ins, 15, 10) == 0b001101 ) {
        // XAR: tsz:imm3 gives the rotation
        const uint32_t tsz = (size << 2) | bits(ins, 20, 19);
        if ( tsz == 0 ) return false;
        const uint32_t a = 31 - __builtin_clz(tsz);
        const uint32_t full = (tsz << 3) | bits(ins, 18, 16);
        auto* i = op(bundle, addr, "XAR.Z", INST_FP_ARITH, xar, {}, {}, { rd, rn }, { rd });
        i->a = a;
        i->imm = (2u * (8u << a)) - full;
        return true;
    }
    const bool b10 = bit(ins, 10);
    uint32_t code;
    if ( !b10 ) {
        if ( size == 0 ) code = 0;
        else if ( size == 1 ) code = 1;
        else return false;
    }
    else {
        static constexpr uint32_t table[4] = { 2, 3, 4, 5 };
        code = table[size];
    }
    auto* i = op(bundle, addr, "TERN.Z", INST_FP_ARITH, ternaryLogic, {}, {}, { rd, rm, rn }, { rd });
    i->c = code;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSve2MulUnpred(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Sve2;
    const uint32_t size = bits(ins, 23, 22);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0), rm = bits(ins, 20, 16);
    const uint32_t k = bits(ins, 12, 10);
    switch ( bits(ins, 15, 10) >> 0 ) {
    case 0b011000: break;
    case 0b011001: if ( size != 0 ) return false; break;
    case 0b011010: case 0b011011: case 0b011100: case 0b011101: break;
    default: return false;
    }
    auto* i = op(bundle, addr, "MUL.ZU", INST_FP_ARITH, mulUnpred, {}, {}, { rn, rm }, { rd });
    i->a = size;
    i->c = (bits(ins, 15, 10) & 7);
    (void)k;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSve2(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Sve2;
    const uint32_t size = bits(ins, 23, 22);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0), rm = bits(ins, 20, 16);
    const uint16_t pg = a64Pred(bits(ins, 12, 10));
    const uint32_t b1510 = bits(ins, 15, 10);

    if ( !bit(ins, 24) ) {
        // 0x44
        if ( !bit(ins, 21) ) {
            const uint32_t top = bits(ins, 15, 13);
            if ( top == 4 ) {
                const uint32_t code = bits(ins, 20, 16);
                if ( code < 2 || code == 4 || code == 5 ) return false;
                auto* i = op(bundle, addr, "SVE2.PB", INST_FP_ARITH, predBinary, {}, {}, { pg, rd, rn }, { rd });
                i->a = size;
                i->c = code;
                return true;
            }
            if ( top == 5 ) {
                const uint32_t code = bits(ins, 20, 16);
                switch ( code ) {
                case 0x04: case 0x05: {
                    if ( size == 0 ) return false;
                    auto* i = op(bundle, addr, "ADALP", INST_FP_ARITH, adalp, {}, {}, { pg, rd, rn }, { rd });
                    i->a = size;
                    i->d = code & 1;
                    return true;
                }
                case 0x00: case 0x01: {
                    if ( size != 2 ) return false;
                    auto* i = op(bundle, addr, "UEST.Z", INST_FP_ARITH, predUnary2, {}, {}, { pg, rd, rn }, { rd });
                    i->a = 2;
                    i->c = code;
                    return true;
                }
                case 0x08: case 0x09: {
                    auto* i = op(bundle, addr, "SQABS.Z", INST_FP_ARITH, predUnary2, {}, {}, { pg, rd, rn }, { rd });
                    i->a = size;
                    i->c = code;
                    return true;
                }
                case 0x11: case 0x14: case 0x15: case 0x16: case 0x17: {
                    auto* i = op(bundle, addr, "SVE2.PAIR", INST_FP_ARITH, predPairwise, {}, {}, { pg, rd, rn }, { rd });
                    i->a = size;
                    i->c = code;
                    return true;
                }
                default: return false;
                }
            }
            if ( (ins & 0xFFA0FC00) == 0x44800000 || (ins & 0xFFA0FC00) == 0x44800400 ) {
                auto* i = op(bundle, addr, "DOT.Z", INST_FP_ARITH, dotZ, {}, {}, { rd, rn, rm }, { rd });
                i->a = bit(ins, 22) ? 3 : 2;
                i->c = bit(ins, 10);
                return true;
            }
            if ( (ins & 0xFFE0FC00) == 0x44807800 ) {
                auto* i = op(bundle, addr, "USDOT.Z", INST_FP_ARITH, dotZ, {}, {}, { rd, rn, rm }, { rd });
                i->a = 2;
                i->c = 2;
                return true;
            }
            if ( (ins & 0xFF20F800) == 0x44000800 ) {
                if ( size == 0 ) return false;
                auto* i = op(bundle, addr, "QDMLBT", INST_FP_ARITH, accumLong, {}, {}, { rd, rn, rm }, { rd });
                i->a = size;
                i->c = 16 + bit(ins, 10);
                return true;
            }
            if ( (ins & 0xFF20F000) == 0x44001000 ) {
                if ( size < 2 ) return false;
                auto* i = op(bundle, addr, "CDOT.Z", INST_FP_ARITH, cdot, {}, {}, { rd, rn, rm }, { rd });
                i->a = size;
                i->d = bits(ins, 11, 10);
                return true;
            }
            if ( (ins & 0xFF20F000) == 0x44002000 || (ins & 0xFF20F000) == 0x44003000 ) {
                auto* i = op(bundle, addr, "CMLA.Z", INST_FP_ARITH, cmlaZ, {}, {}, { rd, rn, rm }, { rd });
                i->a = size;
                i->c = bit(ins, 12);
                i->d = bits(ins, 11, 10);
                return true;
            }
            if ( (ins & 0xFF20E000) == 0x44004000 ) {
                // SMLALB, UMLALB, SMLSLB, UMLSLB: bits 12..10
                if ( size == 0 ) return false;
                const uint32_t k = bits(ins, 12, 10);
                static constexpr uint32_t table[8] = { 4, 5, 6, 7, 8, 9, 10, 11 };
                auto* i = op(bundle, addr, "MLAL.Z", INST_FP_ARITH, accumLong, {}, {}, { rd, rn, rm }, { rd });
                i->a = size;
                i->c = table[k];
                return true;
            }
            if ( (ins & 0xFF20F000) == 0x44006000 ) {
                if ( size == 0 ) return false;
                auto* i = op(bundle, addr, "QDMLAL.Z", INST_FP_ARITH, accumLong, {}, {}, { rd, rn, rm }, { rd });
                i->a = size;
                i->c = 12 + bits(ins, 11, 10);
                return true;
            }
            if ( (ins & 0xFF20F800) == 0x44007000 ) {
                auto* i = op(bundle, addr, "QRDMLAH.Z", INST_FP_ARITH, sameIndexed, {}, {}, { rn, rm, rd }, { rd });
                i->a = size;
                i->c = bit(ins, 10) ? 6 : 5;
                i->d = 1;
                return true;
            }
            return false;
        }
        // Indexed forms: bit 21 set.
        const uint32_t k15 = bits(ins, 15, 10);
        uint32_t a;
        uint64_t index;
        uint16_t zm;
        if ( !bit(ins, 23) ) {
            a = 1;
            index = (static_cast<uint32_t>(bit(ins, 22)) << 2) | bits(ins, 20, 19);
            zm = bits(ins, 18, 16);
        }
        else if ( !bit(ins, 22) ) {
            a = 2;
            index = bits(ins, 20, 19);
            zm = bits(ins, 18, 16);
        }
        else {
            a = 3;
            index = bit(ins, 20);
            zm = bits(ins, 19, 16);
        }
        const auto same = [&](uint32_t code, bool accum) {
            auto* i = op(bundle, addr, "SAME.ZE", INST_FP_ARITH, sameIndexed, {}, {},
                accum ? std::vector<uint16_t> { rn, zm, rd } : std::vector<uint16_t> { rn, zm }, { rd });
            i->a = a;
            i->c = code;
            i->imm = index;
            return true;
        };
        switch ( k15 ) {
        case 0b000010: return same(0, true);    // MLA
        case 0b000011: return same(1, true);    // MLS
        case 0b111110: return same(2, false);   // MUL
        case 0b111100: return same(3, false);   // SQDMULH
        case 0b111101: return same(4, false);   // SQRDMULH
        case 0b000100: return same(5, true);    // SQRDMLAH
        case 0b000101: return same(6, true);    // SQRDMLSH
        default: break;
        }
        if ( k15 == 0 || k15 == 1 ) {
            // SDOT, UDOT by element: S from B with bit 22 = 0 (index 2 bits), D from H with bit 22 = 1 (index 1 bit)
            if ( !bit(ins, 23) ) return false;
            auto* i = op(bundle, addr, "DOT.ZE", INST_FP_ARITH, dotZ, {}, {}, { rd, rn, zm }, { rd });
            i->a = a;
            i->c = bit(ins, 10);
            i->d = 1;
            i->imm = index;
            return true;
        }
        if ( k15 == 0b000110 || k15 == 0b000111 ) {
            if ( (ins & 0xFFE00000) != 0x44A00000 ) return false;
            auto* i = op(bundle, addr, "USDOT.ZE", INST_FP_ARITH, dotZ, {}, {}, { rd, rn, zm }, { rd });
            i->a = 2;
            i->c = bit(ins, 10) ? 3 : 2;
            i->d = 1;
            i->imm = index;
            return true;
        }
        if ( !bit(ins, 23) ) return false;
        // The wide size is S from H (bit 22 = 0) or D from S (bit 22 = 1).
        const uint32_t wa = bit(ins, 22) ? 3 : 2;
        const uint64_t index_long = bit(ins, 22) ? ((static_cast<uint32_t>(bit(ins, 20)) << 1) | static_cast<uint32_t>(bit(ins, 11)))
                                                 : ((bits(ins, 20, 19) << 1) | static_cast<uint32_t>(bit(ins, 11)));
        const uint32_t b1512 = bits(ins, 15, 12);
        const bool b10 = bit(ins, 10);
        if ( (b1512 == 0b1000 || b1512 == 0b1010 || b1512 == 0b1001 || b1512 == 0b1011) ) {
            // SMLALB/T, SMLSLB/T, UMLALB/T, UMLSLB/T
            const bool is_u = bit(ins, 12), is_sub = bit(ins, 13);
            auto* i = op(bundle, addr, "MLAL.ZE", INST_FP_ARITH, accumLong, {}, {}, { rd, rn, zm }, { rd });
            i->a = wa;
            i->c = (is_sub ? 8u : 4u) + (is_u ? 2u : 0u) + (b10 ? 1u : 0u);
            i->d = 1;
            i->imm = index_long;
            return true;
        }
        if ( b1512 == 0b0010 || b1512 == 0b0011 ) {
            auto* i = op(bundle, addr, "QDMLAL.ZE", INST_FP_ARITH, accumLong, {}, {}, { rd, rn, zm }, { rd });
            i->a = wa;
            i->c = (b1512 == 0b0011 ? 14u : 12u) + (b10 ? 1u : 0u);
            i->d = 1;
            i->imm = index_long;
            return true;
        }
        if ( b1512 == 0b1100 || b1512 == 0b1101 || b1512 == 0b1110 ) {
            auto* i = op(bundle, addr, "MULL.ZE", INST_FP_ARITH, mulLongIndexed, {}, {}, { rn, zm }, { rd });
            i->a = wa;
            i->c = (b1512 == 0b1100 ? 0u : b1512 == 0b1101 ? 2u : 4u) + (b10 ? 1u : 0u);
            i->imm = index_long;
            return true;
        }
        const uint32_t rot = bits(ins, 11, 10);
        // CDOT, CMLA, SQRDCMLAH by element: bit 22 selects the size, the pair index is 2 or 1 bits.
        const uint16_t zm_c = bit(ins, 22) ? bits(ins, 19, 16) : bits(ins, 18, 16);
        const uint64_t idx_c = bit(ins, 22) ? bit(ins, 20) : bits(ins, 20, 19);
        if ( b1512 == 0b0100 ) {
            auto* i = op(bundle, addr, "CDOT.ZE", INST_FP_ARITH, cdot, {}, {}, { rd, rn, zm_c }, { rd });
            i->a = wa;
            i->c = 1;
            i->d = rot;
            i->imm = idx_c;
            return true;
        }
        if ( b1512 == 0b0110 || b1512 == 0b0111 ) {
            auto* i = op(bundle, addr, "CMLA.ZE", INST_FP_ARITH, cmlaZ, {}, {}, { rd, rn, zm_c }, { rd });
            i->a = bit(ins, 22) ? 2 : 1;
            i->c = (b1512 == 0b0111 ? 1u : 0u) | 2u;
            i->d = rot;
            i->imm = idx_c;
            return true;
        }
        return false;
    }

    // 0x45
    if ( !bit(ins, 21) ) {
        const uint32_t b1513 = bits(ins, 15, 13);
        if ( b1513 <= 3 ) {
            // Widening long, wide and multiply-long
            const uint32_t k = bits(ins, 15, 11) * 2 + (bit(ins, 10) ? 1 : 0);
            uint32_t code;
            const uint32_t group = bits(ins, 15, 11);
            switch ( group ) {
            case 0b00000: code = 0; break;
            case 0b00001: code = 2; break;
            case 0b00010: code = 4; break;
            case 0b00011: code = 6; break;
            case 0b00110: code = 12; break;
            case 0b00111: code = 14; break;
            case 0b01000: code = 16; break;
            case 0b01001: code = 18; break;
            case 0b01010: code = 20; break;
            case 0b01011: code = 22; break;
            case 0b01100: code = 24; break;
            case 0b01101: code = 26; break;
            case 0b01110: code = 28; break;
            case 0b01111: code = 30; break;
            default: return false;
            }
            (void)k;
            if ( group == 0b01101 && size == 0 ) {
                auto* i = op(bundle, addr, "PMULL.Q", INST_FP_ARITH, pmull64, {}, {}, { rn, rm }, { rd });
                i->c = bit(ins, 10);
                return true;
            }
            if ( size == 0 ) return false;
            auto* i = op(bundle, addr, "WIDEN.Z", INST_FP_ARITH, widening, {}, {}, { rn, rm }, { rd });
            i->a = size;
            i->c = code + (bit(ins, 10) ? 1 : 0);
            return true;
        }
        switch ( b1510 ) {
        case 0b100000:
        case 0b100010:
        case 0b100011: {
            if ( size == 0 ) return false;
            auto* i = op(bundle, addr, "WIDEN.BT", INST_FP_ARITH, widening, {}, {}, { rn, rm }, { rd });
            i->a = size;
            i->c = b1510 == 0b100000 ? 32 : b1510 == 0b100010 ? 34 : 35;
            return true;
        }
        case 0b100100:
        case 0b100101: {
            auto* i = op(bundle, addr, "EORBT", INST_FP_ARITH, eorInter, {}, {}, { rd, rn, rm }, { rd });
            i->a = size;
            i->c = b1510 & 1;
            return true;
        }
        case 0b100110: {
            uint32_t code;
            if ( size == 0 ) code = 0;
            else if ( size == 2 ) code = 2;
            else if ( size == 3 ) code = 1;
            else return false;
            auto* i = op(bundle, addr, "MMLA.Z", INST_FP_ARITH, mmlaZ, {}, {}, { rd, rn, rm }, { rd });
            i->c = code;
            return true;
        }
        case 0b101100:
        case 0b101101:
        case 0b101110: {
            auto* i = op(bundle, addr, "BITPERM", INST_FP_ARITH, bitPerm, {}, {}, { rn, rm }, { rd });
            i->a = size;
            i->c = b1510 & 3;
            return true;
        }
        default: break;
        }
        if ( bits(ins, 15, 12) == 0b1010 ) {
            // SSHLLB, SSHLLT, USHLLB, USHLLT: tsh:imm3
            const uint32_t tsh = (static_cast<uint32_t>(bit(ins, 22)) << 2) | bits(ins, 20, 19);
            if ( tsh == 0 || bit(ins, 23) ) return false;
            const uint32_t narrow = 31 - __builtin_clz(tsh);
            const uint32_t full = (tsh << 3) | bits(ins, 18, 16);
            auto* i = op(bundle, addr, "SHLL.Z", INST_FP_ARITH, shiftLong, {}, {}, { rn }, { rd });
            i->a = narrow + 1;
            i->c = bits(ins, 11, 10);
            i->imm = full - (8u << narrow);
            return true;
        }
        if ( bits(ins, 15, 12) == 0b1100 ) {
            // SABALB, SABALT, UABALB, UABALT
            if ( size == 0 ) return false;
            auto* i = op(bundle, addr, "ABAL.Z", INST_FP_ARITH, accumLong, {}, {}, { rd, rn, rm }, { rd });
            i->a = size;
            i->c = bits(ins, 11, 10);
            return true;
        }
        if ( (ins & 0xFFA0F800) == 0x4500D000 || (ins & 0xFFA0F800) == 0x4580D000 ) {
            auto* i = op(bundle, addr, "ADCL.Z", INST_FP_ARITH, adcLong, {}, {}, { rd, rn, rm }, { rd });
            i->a = bit(ins, 22) ? 3 : 2;
            i->c = (bit(ins, 23) ? 2u : 0u) + (bit(ins, 10) ? 1u : 0u);
            return true;
        }
        if ( (ins & 0xFF3EF800) == 0x4500D800 ) {
            auto* i = op(bundle, addr, "CADD.Z", INST_FP_ARITH, caddZ, {}, {}, { rd, rn }, { rd });
            i->a = size;
            i->c = bit(ins, 16);
            i->d = bit(ins, 10);
            return true;
        }
        if ( bits(ins, 15, 12) == 0b1110 || bits(ins, 15, 11) == 0b11110 ) {
            // SSRA, USRA, SRSRA, URSRA, SRI, SLI: tsz:imm3
            const uint32_t tsz = (size << 2) | bits(ins, 20, 19);
            if ( tsz == 0 ) return false;
            const uint32_t a = 31 - __builtin_clz(tsz);
            const uint32_t esize = 8u << a;
            const uint32_t full = (tsz << 3) | bits(ins, 18, 16);
            const bool is_ins = bits(ins, 15, 12) == 0b1111;
            auto* i = op(bundle, addr, "SHIFT.AZ", INST_FP_ARITH, shiftAccum, {}, {}, { rd, rn }, { rd });
            i->a = a;
            if ( is_ins ) {
                i->c = bit(ins, 10) ? 5 : 4;
                i->imm = bit(ins, 10) ? full - esize : 2 * esize - full;
            }
            else {
                i->c = bits(ins, 11, 10) == 0 ? 0 : bits(ins, 11, 10) == 1 ? 1 : bits(ins, 11, 10) == 2 ? 2 : 3;
                i->imm = 2 * esize - full;
            }
            return true;
        }
        if ( bits(ins, 15, 11) == 0b11111 ) {
            auto* i = op(bundle, addr, "ABA.Z", INST_FP_ARITH, abaZ, {}, {}, { rd, rn, rm }, { rd });
            i->a = size;
            i->d = bit(ins, 10);
            return true;
        }
        return false;
    }
    // 0x45, bit 21 set
    const uint32_t b1513 = bits(ins, 15, 13);
    if ( b1513 <= 1 ) {
        // Shift narrowing: tsz = bit 22 : bits 20..19 ; bit 23 = 0
        if ( bit(ins, 23) ) return false;
        const uint32_t tsz = (static_cast<uint32_t>(bit(ins, 22)) << 2) | bits(ins, 20, 19);
        if ( tsz == 0 ) return false;
        const uint32_t narrow = 31 - __builtin_clz(tsz);
        const uint32_t full = (tsz << 3) | bits(ins, 18, 16);
        static constexpr int table[8] = { 6, 7, 4, 5, 8, 9, 10, 11 };
        const int code = table[bits(ins, 13, 11)];
        const bool top = bit(ins, 10);
        auto* i = op(bundle, addr, "NARROW.ZS", INST_FP_ARITH, narrowing, {}, {},
            top ? std::vector<uint16_t> { rn, rd } : std::vector<uint16_t> { rn }, { rd });
        i->a = narrow + 1;
        i->c = static_cast<uint32_t>(code);
        i->d = top;
        i->imm = 2 * (8u << narrow) - full;
        return true;
    }
    if ( b1513 == 2 ) {
        // SQXTN, UQXTN, SQXTUN
        if ( bit(ins, 23) || bits(ins, 18, 16) != 0 ) return false;
        const uint32_t tsz = (static_cast<uint32_t>(bit(ins, 22)) << 2) | bits(ins, 20, 19);
        if ( tsz == 0 ) return false;
        const uint32_t narrow = 31 - __builtin_clz(tsz);
        const uint32_t k = bits(ins, 12, 11);
        if ( k == 3 ) return false;
        const bool top = bit(ins, 10);
        auto* i = op(bundle, addr, "NARROW.ZX", INST_FP_ARITH, narrowing, {}, {},
            top ? std::vector<uint16_t> { rn, rd } : std::vector<uint16_t> { rn }, { rd });
        i->a = narrow + 1;
        i->c = 12 + k;
        i->d = top;
        return true;
    }
    if ( b1513 == 3 ) {
        // ADDHN, RADDHN, SUBHN, RSUBHN
        if ( size == 0 ) return false;
        const bool top = bit(ins, 10);
        auto* i = op(bundle, addr, "NARROW.ZB", INST_FP_ARITH, narrowing, {}, {},
            top ? std::vector<uint16_t> { rn, rm, rd } : std::vector<uint16_t> { rn, rm }, { rd });
        i->a = size;
        i->c = bits(ins, 12, 11);
        i->d = top;
        return true;
    }
    if ( b1513 == 4 ) {
        // MATCH, NMATCH
        if ( size > 1 ) return false;
        auto* i = op(bundle, addr, "MATCH", INST_FP_ARITH, matchZ, {}, { A64Reg::NZCV }, { pg, rn, rm }, { a64Pred(bits(ins, 3, 0)) });
        i->a = size;
        i->c = bit(ins, 4);
        return true;
    }
    if ( (ins & 0xFF20FC00) == 0x4520A000 ) {
        if ( size != 0 ) return false;
        op(bundle, addr, "HISTSEG", INST_FP_ARITH, histseg, {}, {}, { rn, rm }, { rd });
        return true;
    }
    if ( (ins & 0xFF20E000) == 0x4520C000 ) {
        if ( size < 2 ) return false;
        op(bundle, addr, "HISTCNT", INST_FP_ARITH, histcnt, {}, {}, { pg, rn, rm }, { rd })->a = size;
        return true;
    }
    if ( b1513 == 7 ) {
        const auto crypto = [&](uint32_t code, std::vector<uint16_t> src) {
            op(bundle, addr, "CRYPTO.Z", INST_FP_ARITH, cryptoZ, {}, {}, src, { rd })->c = code;
            return true;
        };
        switch ( ins & 0xFFFFFC00 ) {
        case 0x4522E000: return crypto(0, { rd, rn });
        case 0x4522E400: return crypto(1, { rd, rn });
        case 0x4523E000: return crypto(4, { rd, rn });
        default: break;
        }
        if ( (ins & 0xFFFFFFE0) == 0x4520E000 ) return crypto(2, { rd });
        if ( (ins & 0xFFFFFFE0) == 0x4520E400 ) return crypto(3, { rd });
        if ( (ins & 0xFFE0FC00) == 0x4520F000 ) return crypto(5, { rn, rm });
        if ( (ins & 0xFFE0FC00) == 0x4520F400 ) return crypto(6, { rn, rm });
        return false;
    }
    return false;
}

} // namespace Vanadis
} // namespace SST

#endif
