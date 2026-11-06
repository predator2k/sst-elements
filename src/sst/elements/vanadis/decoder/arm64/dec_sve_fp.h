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

// Decoder groups of the floating-point instructions of SVE and SVE2. Member functions of VanadisARM64Decoder; included
// at the end of decoder/varm64decoder.h.

#ifndef _H_VANADIS_ARM64_DEC_SVE_FP
#define _H_VANADIS_ARM64_DEC_SVE_FP

namespace SST {
namespace Vanadis {

// FTSSEL, FEXPA, in the integer 0x04 group.
inline bool
VanadisARM64Decoder::decodeSveFpMiscInt(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64SveFp;
    const uint32_t size = bits(ins, 23, 22);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0), rm = bits(ins, 20, 16);
    if ( size == 0 ) return false;
    if ( (ins & 0xFF20FC00) == 0x0420B000 ) {
        auto* i = op(bundle, addr, "FTSSEL", INST_FP_ARITH, trigMisc, {}, {}, { rn, rm }, { rd });
        i->a = size;
        i->c = 0;
        return true;
    }
    if ( (ins & 0xFF3FFC00) == 0x0420B800 ) {
        auto* i = op(bundle, addr, "FEXPA", INST_FP_ARITH, trigMisc, {}, {}, { rn }, { rd });
        i->a = size;
        i->c = 1;
        return true;
    }
    return false;
}

inline bool
VanadisARM64Decoder::decodeSveFp(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64SveFp;
    const uint32_t size = bits(ins, 23, 22);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0), rm = bits(ins, 20, 16);
    const uint16_t pg = a64Pred(bits(ins, 12, 10));
    const uint16_t fpcr = A64Reg::FPCR;
    const auto unary_like = [&](const char* name, VanadisArm64Inst::Semantics fn, uint32_t a, uint32_t c, uint32_t d) {
        auto* i = op(bundle, addr, name, INST_FP_ARITH, fn, { fpcr }, {}, { pg, rd, rn }, { rd });
        i->a = a;
        i->c = c;
        i->d = d;
        return true;
    };

    if ( !bit(ins, 24) ) {
        // The 0x64 group.
        if ( (ins & 0xFFA0FC00) == 0x64200000 || (ins & 0xFFA0FC00) == 0x64200400 || (ins & 0xFFE0F800) == 0x64A00000 || (ins & 0xFFE0F800) == 0x64E00000 ||
             (ins & 0xFFA0FC00) == 0x64202000 || (ins & 0xFFE0FC00) == 0x64A02000 || (ins & 0xFFE0FC00) == 0x64E02000 ) {
            // FMLA, FMLS, FMUL by element
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
            const bool is_mul = bit(ins, 13);
            const bool sub = bit(ins, 10);
            if ( is_mul && sub ) return false;
            auto* i = op(bundle, addr, "FMUL.ZE", INST_FP_ARITH, fpIndexed, { fpcr }, {},
                is_mul ? std::vector<uint16_t> { rn, zm } : std::vector<uint16_t> { rn, zm, rd }, { rd });
            i->a = a;
            i->c = is_mul ? 2 : (sub ? 1 : 0);
            i->imm = index;
            return true;
        }
        if ( (ins & 0xFF208000) == 0x64000000 ) {
            if ( size == 0 ) return false;
            auto* i = op(bundle, addr, "FCMLA.Z", INST_FP_ARITH, fcmlaZ, { fpcr }, {}, { pg, rd, rn, rm }, { rd });
            i->a = size;
            i->d = bits(ins, 14, 13);
            return true;
        }
        if ( (ins & 0xFF3EE000) == 0x64008000 ) {
            if ( size == 0 ) return false;
            auto* i = op(bundle, addr, "FCADD.Z", INST_FP_ARITH, fcaddZ, { fpcr }, {}, { pg, rd, rn }, { rd });
            i->a = size;
            i->d = bit(ins, 16);
            return true;
        }
        if ( (ins & 0xFFE0F000) == 0x64A01000 || (ins & 0xFFE0F000) == 0x64E01000 ) {
            const bool is_s = bit(ins, 22);
            const uint16_t zm = is_s ? bits(ins, 19, 16) : bits(ins, 18, 16);
            auto* i = op(bundle, addr, "FCMLA.ZE", INST_FP_ARITH, fcmlaZ, { fpcr }, {}, { rd, rn, zm }, { rd });
            i->a = is_s ? 2 : 1;
            i->c = 1;
            i->d = bits(ins, 11, 10);
            i->imm = is_s ? bit(ins, 20) : bits(ins, 20, 19);
            return true;
        }
        if ( (ins & 0xFF3FE000) == 0x64108000 || (ins & 0xFF3CE000) == 0x64148000 ) {
            // FADDP, FMAXNMP, FMINNMP, FMAXP, FMINP
            if ( size == 0 ) return false;
            auto* i = op(bundle, addr, "FPAIR.Z", INST_FP_ARITH, fpPairwiseZ, { fpcr }, {}, { pg, rd, rn }, { rd });
            i->a = size;
            const uint32_t k = bits(ins, 18, 16);
            i->c = k == 0 ? 0 : k;
            return true;
        }
        if ( (ins & 0xFFE0D800) == 0x64A08000 || (ins & 0xFFE0D800) == 0x64E08000 ) {
            // FMLALB, FMLALT, FMLSLB, FMLSLT; BFMLALB, BFMLALT, BFMLSLB, BFMLSLT
            const bool is_bf = bit(ins, 22), s = bit(ins, 13);
            if ( is_bf && s ) return false;
            auto* i = op(bundle, addr, "FMLAL.Z", INST_FP_ARITH, is_bf ? bfmlalZ : fmlalZ, { fpcr }, {}, { rd, rn, rm }, { rd });
            i->c = s;
            i->d = bit(ins, 10);
            return true;
        }
        if ( (ins & 0xFFE0D000) == 0x64A04000 || (ins & 0xFFE0D000) == 0x64E04000 ) {
            const bool is_bf = bit(ins, 22), s = bit(ins, 13);
            if ( is_bf && s ) return false;
            const uint16_t zm = bits(ins, 18, 16);
            auto* i = op(bundle, addr, "FMLAL.ZE", INST_FP_ARITH, is_bf ? bfmlalZ : fmlalZ, { fpcr }, {}, { rd, rn, zm }, { rd });
            i->c = s;
            i->d = bit(ins, 10) | 2;
            i->imm = (bits(ins, 20, 19) << 1) | static_cast<uint32_t>(bit(ins, 11));
            return true;
        }
        if ( (ins & 0xFFE0FC00) == 0x64608000 ) {
            op(bundle, addr, "BFDOT.Z", INST_FP_ARITH, bfdotZ, {}, {}, { rd, rn, rm }, { rd });
            return true;
        }
        if ( (ins & 0xFFE0FC00) == 0x64604000 ) {
            auto* i = op(bundle, addr, "BFDOT.ZE", INST_FP_ARITH, bfdotZ, {}, {}, { rd, rn, static_cast<uint16_t>(bits(ins, 18, 16)) }, { rd });
            i->d = 1;
            i->imm = bits(ins, 20, 19);
            return true;
        }
        if ( (ins & 0xFFE0FC00) == 0x6460E400 ) {
            op(bundle, addr, "BFMMLA.Z", INST_FP_ARITH, bfmmlaZ, {}, {}, { rd, rn, rm }, { rd });
            return true;
        }
        if ( (ins & 0xFFA0FC00) == 0x64A0E400 ) {
            auto* i = op(bundle, addr, "FMMLA.Z", INST_FP_ARITH, fmmlaZ, { fpcr }, {}, { rd, rn, rm }, { rd });
            i->a = bit(ins, 22) ? 3 : 2;
            return true;
        }
        if ( (ins & 0xFFFFE000) == 0x6489A000 || (ins & 0xFFFFE000) == 0x64CBA000 ) return unary_like("FCVTLT", fpTopConvert, bit(ins, 22) ? 3 : 2, 0, 0);
        if ( (ins & 0xFFFFE000) == 0x6488A000 || (ins & 0xFFFFE000) == 0x64CAA000 ) return unary_like("FCVTNT", fpTopConvert, bit(ins, 22) ? 3 : 2, 1, 0);
        if ( (ins & 0xFFFFE000) == 0x640AA000 ) return unary_like("FCVTXNT", fpTopConvert, 3, 2, 0);
        if ( (ins & 0xFFFFE000) == 0x648AA000 ) return unary_like("BFCVTNT", fpTopConvert, 2, 3, 0);
        return false;
    }
    // The 0x65 group.
    if ( bit(ins, 21) ) {
        if ( size == 0 ) return false;
        if ( !bit(ins, 15) ) {
            auto* i = op(bundle, addr, "FMLA.Z", INST_FP_ARITH, predFpMulAdd, { fpcr }, {}, { pg, rd, rn, rm }, { rd });
            i->a = size;
            i->c = bits(ins, 14, 13);
            return true;
        }
        auto* i = op(bundle, addr, "FMAD.Z", INST_FP_ARITH, predFpMulAdd, { fpcr }, {}, { pg, rd, rn, rm }, { rd });
        i->a = size;
        i->c = 4 + bits(ins, 14, 13);
        return true;
    }
    const uint32_t b1513 = bits(ins, 15, 13);
    if ( bits(ins, 15, 13) == 0 ) {
        // Unpredicated
        const uint32_t k = bits(ins, 12, 10);
        if ( size == 0 || !(k <= 3 || k == 6 || k == 7) ) return false;
        auto* i = op(bundle, addr, "FARITH.Z", INST_FP_ARITH, unpredFp, { fpcr }, {}, { rn, rm }, { rd });
        i->a = size;
        i->c = k;
        return true;
    }
    if ( (ins & 0xFF3EFC00) == 0x650E3000 ) {
        if ( size == 0 ) return false;
        auto* i = op(bundle, addr, "FEST.Z", INST_FP_ARITH, estimateZ, { fpcr }, {}, { rn }, { rd });
        i->a = size;
        i->c = bit(ins, 16);
        return true;
    }
    if ( b1513 == 1 ) {
        if ( size == 0 ) return false;
        const uint32_t k = bits(ins, 20, 16);
        if ( k == 0x18 ) {
            auto* i = op(bundle, addr, "FADDA", INST_FP_ARITH, fadda, { fpcr }, {}, { pg, rd, rn }, { rd });
            i->a = size;
            return true;
        }
        if ( k == 0 || (k >= 4 && k <= 7) ) {
            auto* i = op(bundle, addr, "FREDUCE", INST_FP_ARITH, fpReduceZ, { fpcr }, {}, { pg, rn }, { rd });
            i->a = size;
            i->c = k;
            return true;
        }
        if ( k >= 0x10 && k <= 0x13 ) {
            // Compare with zero: bits 17..16 and bit 4
            const bool b4 = bit(ins, 4);
            uint32_t code;
            switch ( k & 3 ) {
            case 0: code = b4 ? 1 : 0; break;
            case 1: code = b4 ? 8 : 7; break;
            case 2: if ( b4 ) return false; code = 2; break;
            default: if ( b4 ) return false; code = 3; break;
            }
            auto* i = op(bundle, addr, "FCMP.Z0", INST_FP_ARITH, fpCompareZ, { fpcr }, {}, { pg, rn }, { a64Pred(bits(ins, 3, 0)) });
            i->a = size;
            i->c = code;
            i->d = 1;
            return true;
        }
        return false;
    }
    if ( b1513 == 2 || b1513 == 3 || b1513 == 6 || b1513 == 7 ) {
        // Compare vectors (bit 15:14 = 01 or 11, bit 13 selects)
        const bool b4 = bit(ins, 4);
        const uint32_t k = (b1513 << 1) | (b4 ? 1u : 0u);
        int code;
        switch ( k ) {
        case 0b0100: code = 0; break;   // FCMGE
        case 0b0101: code = 1; break;   // FCMGT
        case 0b0110: code = 2; break;   // FCMEQ
        case 0b0111: code = 3; break;   // FCMNE
        case 0b1100: code = 4; break;   // FCMUO
        case 0b1101: code = 5; break;   // FACGE
        case 0b1111: code = 6; break;   // FACGT
        default: return false;
        }
        if ( size == 0 ) return false;
        auto* i = op(bundle, addr, "FCMP.Z", INST_FP_ARITH, fpCompareZ, { fpcr }, {},
            { a64Pred(bits(ins, 12, 10)), rn, rm }, { a64Pred(bits(ins, 3, 0)) });
        i->a = size;
        i->c = static_cast<uint32_t>(code);
        return true;
    }
    if ( b1513 == 4 ) {
        if ( size == 0 ) return false;
        const uint32_t k20 = bits(ins, 20, 19);
        if ( k20 == 0 || k20 == 1 ) {
            // FADD .. FDIV (vectors)
            const uint32_t code = bits(ins, 19, 16);
            if ( code == 11 || code >= 14 ) return false;
            auto* i = op(bundle, addr, code == 12 || code == 13 ? "FDIV.Z" : "FBIN.Z", code == 12 || code == 13 ? INST_FP_DIV : INST_FP_ARITH, predFpBin, { fpcr }, {}, { pg, rd, rn }, { rd });
            i->a = size;
            i->c = code;
            return true;
        }
        if ( k20 == 3 && bits(ins, 9, 6) == 0 ) {
            // With an immediate.
            const uint32_t code = bits(ins, 18, 16);
            const bool i1 = bit(ins, 5);
            const uint32_t F = size == 1 ? 10 : size == 2 ? 23 : 52;
            const uint32_t E = size == 1 ? 5 : size == 2 ? 8 : 11;
            const uint64_t bias = (1ull << (E - 1)) - 1;
            const uint64_t half = (bias - 1) << F, one = bias << F, two = (bias + 1) << F;
            uint64_t v;
            switch ( code ) {
            case 0: case 1: case 3: v = i1 ? one : half; break;
            case 2: v = i1 ? two : half; break;
            default: v = i1 ? one : 0; break;
            }
            auto* i = op(bundle, addr, "FBIN.ZI", INST_FP_ARITH, predFpBin, { fpcr }, {}, { pg, rd }, { rd });
            i->a = size;
            i->c = code;
            i->d = 1;
            i->imm = v;
            return true;
        }
        if ( (ins & 0xFF38FC00) == 0x65108000 ) {
            auto* i = op(bundle, addr, "FTMAD", INST_FP_ARITH, ftmad, { fpcr }, {}, { rd, rn }, { rd });
            i->a = size;
            i->imm = bits(ins, 18, 16);
            return true;
        }
        return false;
    }
    if ( b1513 == 5 ) {
        const uint32_t k = bits(ins, 20, 16);
        if ( k <= 7 ) {
            if ( size == 0 || k == 5 ) return false;
            return unary_like("FRINT.Z", predFpUnary, size, k, 0);
        }
        if ( k == 0x0C || k == 0x0D ) {
            if ( size == 0 ) return false;
            return unary_like("FUN.Z", predFpUnary, size, k == 0x0C ? 8 : 9, 0);
        }
        if ( (ins & 0xFFF8E000) == 0x6510A000 ) {
            // FRINT32Z, FRINT32X, FRINT64Z, FRINT64X: bit 18 = 64, bit 17 = D, bit 16 = X
            const uint32_t code = (bit(ins, 18) ? 12u : 10u) + (bit(ins, 16) ? 1u : 0u);
            return unary_like("FRINTN.Z", predFpUnary, bit(ins, 17) ? 3 : 2, code, 0);
        }
        if ( (ins & 0xFFF9E000) == 0x6518A000 ) {
            const uint32_t sz = bits(ins, 18, 17);
            if ( sz == 0 ) return false;
            return unary_like("FLOGB.Z", predFpUnary, sz, 14, 0);
        }
        switch ( ins & 0xFFFFE000 ) {
        case 0x6588A000: return unary_like("FCVT.Z", fpConvertZ, 2, 0x21, 0);
        case 0x6589A000: return unary_like("FCVT.Z", fpConvertZ, 2, 0x12, 0);
        case 0x65C8A000: return unary_like("FCVT.Z", fpConvertZ, 3, 0x31, 0);
        case 0x65C9A000: return unary_like("FCVT.Z", fpConvertZ, 3, 0x13, 0);
        case 0x65CAA000: return unary_like("FCVT.Z", fpConvertZ, 3, 0x32, 0);
        case 0x65CBA000: return unary_like("FCVT.Z", fpConvertZ, 3, 0x23, 0);
        case 0x650AA000: return unary_like("FCVTX.Z", fpConvertZ, 3, 0, 1);
        case 0x658AA000: return unary_like("BFCVT.Z", bfConvertZ, 2, 0, 0);
        default: break;
        }
        // FCVTZS, FCVTZU: bit 16 is unsigned
        const uint32_t conv = (ins & 0xFFFEE000);
        const uint32_t d = bit(ins, 16) ? 0 : 1;
        switch ( conv ) {
        case 0x655AA000: return unary_like("FCVTZ.Z", fpToIntZ, 1, 0x11, d);
        case 0x655CA000: return unary_like("FCVTZ.Z", fpToIntZ, 2, 0x12, d);
        case 0x655EA000: return unary_like("FCVTZ.Z", fpToIntZ, 3, 0x13, d);
        case 0x659CA000: return unary_like("FCVTZ.Z", fpToIntZ, 2, 0x22, d);
        case 0x65DCA000: return unary_like("FCVTZ.Z", fpToIntZ, 3, 0x23, d);
        case 0x65D8A000: return unary_like("FCVTZ.Z", fpToIntZ, 3, 0x32, d);
        case 0x65DEA000: return unary_like("FCVTZ.Z", fpToIntZ, 3, 0x33, d);
        case 0x6552A000: return unary_like("SCVTF.Z", intToFpZ, 1, 0x11, d);
        case 0x6554A000: return unary_like("SCVTF.Z", intToFpZ, 2, 0x21, d);
        case 0x6556A000: return unary_like("SCVTF.Z", intToFpZ, 3, 0x31, d);
        case 0x6594A000: return unary_like("SCVTF.Z", intToFpZ, 2, 0x22, d);
        case 0x65D0A000: return unary_like("SCVTF.Z", intToFpZ, 3, 0x23, d);
        case 0x65D4A000: return unary_like("SCVTF.Z", intToFpZ, 3, 0x32, d);
        case 0x65D6A000: return unary_like("SCVTF.Z", intToFpZ, 3, 0x33, d);
        default: return false;
        }
    }
    return false;
}

} // namespace Vanadis
} // namespace SST

#endif
