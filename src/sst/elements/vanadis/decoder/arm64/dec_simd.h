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

// Decoder groups of Advanced SIMD and the cryptographic extensions. Member functions of VanadisARM64Decoder; included
// at the end of decoder/varm64decoder.h. A scalar form uses the micro-op of the vector form with one element.

#ifndef _H_VANADIS_ARM64_DEC_SIMD
#define _H_VANADIS_ARM64_DEC_SIMD

namespace SST {
namespace Vanadis {

inline bool
VanadisARM64Decoder::decodeAdvSimd(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    if ( bit(ins, 31) ) return decodeCryptoHigh(addr, ins, bundle);
    const bool     scalar = bit(ins, 30) && bits(ins, 28, 25) == 0b1111;
    const uint32_t top = bits(ins, 28, 24);
    const bool     b10 = bit(ins, 10), b15 = bit(ins, 15), b21 = bit(ins, 21);
    const uint32_t lo2 = bits(ins, 11, 10);
    if ( scalar ) {
        if ( top == 0b11111 ) {
            if ( b10 ) return !bit(ins, 23) && decodeSimdShiftImm(addr, ins, bundle, true);
            return decodeSimdIndexed(addr, ins, bundle, true);
        }
        if ( bits(ins, 22, 17) == 0b111100 && lo2 == 2 ) return decodeSimdMiscFp16(addr, ins, bundle, true);
        if ( !b21 ) {
            if ( bits(ins, 23, 21) == 0 && !b15 && b10 ) return decodeSimdCopy(addr, ins, bundle, true);
            if ( bits(ins, 22, 21) == 0b10 && bits(ins, 15, 14) == 0 && b10 ) return decodeSimdThreeSameFp16(addr, ins, bundle, true);
            if ( b15 && b10 ) return decodeSimdThreeExt(addr, ins, bundle, true);
            if ( !b15 && lo2 == 0 ) return decodeCryptoSha3Reg(addr, ins, bundle);
            return false;
        }
        const uint32_t k = bits(ins, 20, 17);
        if ( lo2 == 2 ) {
            if ( k == 0 ) return decodeSimdMisc(addr, ins, bundle, true);
            if ( k == 0b1000 ) return decodeSimdScalarPair(addr, ins, bundle);
            if ( k == 0b0100 ) return decodeCryptoSha2Reg(addr, ins, bundle);
            return false;
        }
        if ( lo2 == 0 ) return decodeSimdThreeDiff(addr, ins, bundle, true);
        if ( b10 ) return decodeSimdThreeSame(addr, ins, bundle, true);
        return false;
    }
    if ( top == 0b01111 ) {
        if ( b10 ) {
            if ( bit(ins, 23) ) return false;
            if ( bits(ins, 22, 19) == 0 ) return decodeSimdModifiedImm(addr, ins, bundle);
            return decodeSimdShiftImm(addr, ins, bundle, false);
        }
        return decodeSimdIndexed(addr, ins, bundle, false);
    }
    if ( top != 0b01110 ) return false;
    if ( bits(ins, 22, 17) == 0b111100 && lo2 == 2 ) return decodeSimdMiscFp16(addr, ins, bundle, false);
    if ( !b21 ) {
        if ( bits(ins, 23, 21) == 0 && !b15 && b10 ) return decodeSimdCopy(addr, ins, bundle, false);
        if ( bits(ins, 22, 21) == 0b10 && bits(ins, 15, 14) == 0 && b10 ) return decodeSimdThreeSameFp16(addr, ins, bundle, false);
        if ( b15 && b10 ) return decodeSimdThreeExt(addr, ins, bundle, false);
        if ( !b15 && !b10 ) {
            if ( bit(ins, 29) ) return decodeSimdExt(addr, ins, bundle);
            return bit(ins, 11) ? decodeSimdPermute(addr, ins, bundle) : decodeSimdTable(addr, ins, bundle);
        }
        return false;
    }
    const uint32_t k = bits(ins, 20, 17);
    if ( lo2 == 2 ) {
        if ( k == 0 ) return decodeSimdMisc(addr, ins, bundle, false);
        if ( k == 0b1000 ) return decodeSimdAcross(addr, ins, bundle);
        if ( k == 0b0100 ) return decodeCryptoAes(addr, ins, bundle);
        return false;
    }
    if ( lo2 == 0 ) return decodeSimdThreeDiff(addr, ins, bundle, false);
    if ( b10 ) return decodeSimdThreeSame(addr, ins, bundle, false);
    return false;
}

inline bool
VanadisARM64Decoder::decodeSimdThreeSame(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t size = bits(ins, 23, 22), opcode = bits(ins, 15, 11);
    const uint16_t rm = bits(ins, 20, 16), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( opcode >= 24 ) {
        // Floating point: size<1> selects the second operation of the opcode, size<0> the format.
        const uint32_t s = size >> 1, sz = size & 1;
        const uint32_t code = (static_cast<uint32_t>(U) << 6) | (s << 5) | opcode;
        if ( (code == 0x1D || code == 0x3D || code == 0x59 || code == 0x79) ) {
            // FMLAL, FMLSL, FMLAL2, FMLSL2
            if ( scalar || sz ) return false;
            auto* i = fpOp(bundle, addr, "FMLAL", INST_FP_ARITH, simdFpMulAddLong, { A64Reg::FPCR }, {}, { rn, rm, rd }, { rd });
            tc(i, A64Tc::FP_MLA);
            i->b = Q ? 16 : 8;
            i->c = s;
            i->d = U ? 1 : 0;
            markMultiplyAdd(i, 2);
            return true;
        }
        if ( !scalar && sz && !Q ) return false;
        bool valid;
        if ( scalar ) {
            valid = code == 0x1B || code == 0x1C || code == 0x1F || code == 0x3F || code == 0x5C || code == 0x5D || code == 0x7A ||
                    code == 0x7C || code == 0x7D;
        }
        else {
            switch ( code ) {
            case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1E: case 0x1F:
            case 0x38: case 0x39: case 0x3A: case 0x3E: case 0x3F:
            case 0x58: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            case 0x78: case 0x7A: case 0x7C: case 0x7D: case 0x7E:
                valid = true;
                break;
            default: valid = false; break;
            }
        }
        if ( !valid ) return false;
        const uint32_t a = sz ? 3 : 2;
        const bool     needs_d = code == 0x19 || code == 0x39;
        auto* i = fpOp(bundle, addr, code == 0x5F ? "FDIV.V" : "FP3S", code == 0x5F ? INST_FP_DIV : INST_FP_ARITH, simdFpThreeSame, { A64Reg::FPCR }, {},
            needs_d ? std::vector<uint16_t> { rn, rm, rd } : std::vector<uint16_t> { rn, rm }, { rd });
        tc(i, fpThreeSameClass(code, a, scalar ? (1u << a) : (Q ? 16u : 8u)));
        i->a = a;
        i->b = scalar ? (1u << a) : (Q ? 16 : 8);
        i->c = code;
        if ( code == 0x5B || code == 0x1B ) i->setExtraExecuteCycles(fp_mul_extra_cycles);    // FMUL, FMULX
        if ( code == 0x19 || code == 0x39 ) markMultiplyAdd(i, 2);    // FMLA, FMLS
        return true;
    }
    bool valid = true;
    if ( scalar ) {
        switch ( opcode ) {
        case 1: case 5: case 9: case 11: break;
        case 6: case 7: case 8: case 10: case 16: case 17: valid = size == 3; break;
        case 22: valid = size == 1 || size == 2; break;
        default: valid = false; break;
        }
    }
    else {
        if ( size == 3 && !Q && opcode != 3 ) return false;
        switch ( opcode ) {
        case 0: case 2: case 4: case 12: case 13: case 14: case 15: case 18: case 20: case 21: valid = size != 3; break;
        case 19: valid = U ? size == 0 : size != 3; break;
        case 22: valid = size == 1 || size == 2; break;
        case 23: valid = !U; break;
        default: break;
        }
    }
    if ( !valid ) return false;
    const bool needs_d = (opcode == 3 && U && size != 0) || opcode == 15 || opcode == 18;
    auto* i = fpOp(bundle, addr, "INT3S", INST_FP_ARITH, simdThreeSame, {}, {},
        needs_d ? std::vector<uint16_t> { rn, rm, rd } : std::vector<uint16_t> { rn, rm }, { rd });
    tc(i, intThreeSameClass(opcode, U, size));
    i->a = opcode == 3 ? 0 : size;
    i->b = scalar ? (1u << size) : (Q ? 16 : 8);
    i->c = (static_cast<uint32_t>(U) << 5) | opcode;
    i->d = opcode == 3 ? size : 0;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdThreeSameFp16(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t s = bit(ins, 23), opcode = bits(ins, 13, 11);
    const uint16_t rm = bits(ins, 20, 16), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    const uint32_t code = (static_cast<uint32_t>(U) << 6) | (s << 5) | (24 + opcode);
    bool valid;
    if ( scalar ) {
        valid = code == 0x1B || code == 0x1C || code == 0x1F || code == 0x3F || code == 0x5C || code == 0x5D || code == 0x7A ||
                code == 0x7C || code == 0x7D;
    }
    else {
        switch ( code ) {
        case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1E: case 0x1F:
        case 0x38: case 0x39: case 0x3A: case 0x3E: case 0x3F:
        case 0x58: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        case 0x78: case 0x7A: case 0x7C: case 0x7D: case 0x7E:
            valid = true;
            break;
        default: valid = false; break;
        }
    }
    if ( !valid ) return false;
    const bool needs_d = code == 0x19 || code == 0x39;
    auto* i = fpOp(bundle, addr, code == 0x5F ? "FDIV.H" : "FP3S.H", code == 0x5F ? INST_FP_DIV : INST_FP_ARITH, simdFpThreeSame, { A64Reg::FPCR }, {},
        needs_d ? std::vector<uint16_t> { rn, rm, rd } : std::vector<uint16_t> { rn, rm }, { rd });
    tc(i, fpThreeSameClass(code, 1, scalar ? 2u : (Q ? 16u : 8u)));
    i->a = 1;
    i->b = scalar ? 2 : (Q ? 16 : 8);
    i->c = code;
    if ( code == 0x5B || code == 0x1B ) i->setExtraExecuteCycles(fp_mul_extra_cycles);    // FMUL, FMULX
    if ( code == 0x19 || code == 0x39 ) markMultiplyAdd(i, 2);    // FMLA, FMLS
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdThreeExt(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t size = bits(ins, 23, 22), opcode = bits(ins, 14, 11);
    const uint16_t rm = bits(ins, 20, 16), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    const uint32_t reg_bytes = Q ? 16 : 8;
    if ( U && opcode <= 1 ) {
        if ( size != 1 && size != 2 ) return false;
        auto* i = fpOp(bundle, addr, "SQRDMLAH", INST_FP_ARITH, simdSatMulAccum, {}, {}, { rn, rm, rd }, { rd });
        tc(i, A64Tc::V_MLA);
        i->a = size;
        i->b = scalar ? (1u << size) : reg_bytes;
        i->c = opcode;
        return true;
    }
    if ( scalar ) return false;
    if ( opcode == 2 || (opcode == 3 && !U) ) {
        if ( size != 2 ) return false;
        auto* i = op(bundle, addr, "DOT", INST_FP_ARITH, simdDot, {}, {}, { rn, rm, rd }, { rd });
        tc(i, A64Tc::V_DOT);
        i->b = reg_bytes;
        i->c = opcode == 3 ? 2 : (U ? 1 : 0);
        return true;
    }
    if ( opcode == 4 || (opcode == 5 && !U) ) {
        if ( size != 2 || !Q ) return false;
        auto* i = op(bundle, addr, "MMLA", INST_FP_ARITH, simdMmla, {}, {}, { rn, rm, rd }, { rd });
        tc(i, A64Tc::V_MMLA);
        i->c = opcode == 5 ? 2 : (U ? 1 : 0);
        return true;
    }
    if ( !U ) return false;
    if ( opcode >= 8 && opcode <= 11 ) {
        if ( size == 0 || (size == 3 && !Q) ) return false;
        auto* i = fpOp(bundle, addr, "FCMLA", INST_FP_ARITH, simdFcmla, { A64Reg::FPCR }, {}, { rn, rm, rd }, { rd });
        tc(i, A64Tc::FP_COMPLEX_MLA);
        i->a = size;
        i->b = reg_bytes;
        i->d = opcode & 3;
        return true;
    }
    if ( opcode == 12 || opcode == 14 ) {
        if ( size == 0 || (size == 3 && !Q) ) return false;
        auto* i = fpOp(bundle, addr, "FCADD", INST_FP_ARITH, simdFcadd, { A64Reg::FPCR }, {}, { rn, rm }, { rd });
        tc(i, A64Tc::FP_ALU);
        i->a = size;
        i->b = reg_bytes;
        i->d = opcode == 14 ? 1 : 0;
        return true;
    }
    if ( opcode == 15 ) {
        if ( size == 1 ) {
            auto* i = op(bundle, addr, "BFDOT", INST_FP_ARITH, simdBfDot, {}, {}, { rn, rm, rd }, { rd });
            tc(i, A64Tc::BF_DOT);
            i->b = reg_bytes;
            return true;
        }
        if ( size == 3 ) {
            auto* i = op(bundle, addr, "BFMLAL", INST_FP_ARITH, simdBfMlal, { A64Reg::FPCR }, {}, { rn, rm, rd }, { rd });
            tc(i, A64Tc::FP_COMPLEX_MLA);
            i->c = Q ? 1 : 0;
            markMultiplyAdd(i, 2);
            return true;
        }
        return false;
    }
    if ( opcode == 13 && size == 1 && Q ) {
        tc(op(bundle, addr, "BFMMLA", INST_FP_ARITH, simdBfMmla, {}, {}, { rn, rm, rd }, { rd }), A64Tc::BF_MMLA);
        return true;
    }
    return false;
}

inline bool
VanadisARM64Decoder::decodeSimdMisc(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t size = bits(ins, 23, 22), opcode = bits(ins, 16, 12);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    const uint32_t s = size >> 1, sz = size & 1;
    const uint32_t reg_bytes = Q ? 16 : 8;
    const uint32_t ud = U ? 1 : 0;
    const bool     is_fp = (opcode >= 0xC && opcode <= 0xF) || opcode == 0x16 || opcode == 0x17 || (opcode >= 0x18);
    const bool     int_estimate = opcode == 0x1C && s == 1;

    if ( is_fp && !int_estimate ) {
        if ( opcode == 0x16 || opcode == 0x17 ) {
            if ( opcode == 0x16 && !U && size == 2 && !scalar ) {
                auto* i = fpOp(bundle, addr, "BFCVTN", INST_FP_ARITH, simdBfConvertNarrow, { A64Reg::FPCR }, {},
                    Q ? std::vector<uint16_t> { rn, rd } : std::vector<uint16_t> { rn }, { rd });
                tc(i, A64Tc::BF_CVT);
                i->d = Q;
                return true;
            }
            if ( s ) return false;
            const uint32_t narrow = sz ? 2 : 1;
            if ( scalar ) {
                // FCVTXN to a single.
                if ( !(opcode == 0x16 && U && sz) ) return false;
                auto* i = fpOp(bundle, addr, "FCVTXN.S", INST_FP_ARITH, simdFpWidthConvert, { A64Reg::FPCR }, {}, { rn }, { rd });
                tc(i, A64Tc::FP_CVT);
                i->a = 2;
                i->c = 1;
                i->d = 2;
                return true;
            }
            if ( opcode == 0x16 ) {
                if ( U && !sz ) return false;
                auto* i = fpOp(bundle, addr, "FCVTN", INST_FP_ARITH, simdFpWidthConvert, { A64Reg::FPCR }, {},
                    Q ? std::vector<uint16_t> { rn, rd } : std::vector<uint16_t> { rn }, { rd });
                tc(i, narrow == 1 ? A64Tc::FP_CVT_S : A64Tc::FP_CVT);
                i->a = narrow;
                i->c = U ? 1 : 0;
                i->d = Q;
                return true;
            }
            if ( U ) return false;
            auto* i = fpOp(bundle, addr, "FCVTL", INST_FP_ARITH, simdFpWidthConvert, { A64Reg::FPCR }, {}, { rn }, { rd });
            tc(i, narrow == 1 ? A64Tc::FP_CVT_S : A64Tc::FP_CVT);
            i->a = narrow;
            i->c = 2;
            i->d = Q;
            return true;
        }
        if ( opcode <= 0xF && !s ) return false;
        const uint32_t code = (ud << 6) | (s << 5) | opcode;
        bool valid;
        if ( scalar ) {
            switch ( code ) {
            case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x2C: case 0x2D: case 0x2E: case 0x3A: case 0x3B: case 0x3D: case 0x3F:
            case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x6C: case 0x6D: case 0x7A: case 0x7B: case 0x7D:
                valid = true;
                break;
            default: valid = false; break;
            }
        }
        else {
            if ( sz && !Q ) return false;
            switch ( code ) {
            case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            case 0x2C: case 0x2D: case 0x2E: case 0x2F:
            case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3D:
            case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            case 0x6C: case 0x6D: case 0x6F:
            case 0x79: case 0x7A: case 0x7B: case 0x7D: case 0x7F:
                valid = true;
                break;
            default: valid = false; break;
            }
        }
        if ( !valid ) return false;
        const uint32_t a = sz ? 3 : 2;
        auto* i = fpOp(bundle, addr, code == 0x7F ? "FSQRT.V" : "FPMISC", code == 0x7F ? INST_FP_DIV : INST_FP_ARITH, simdFpMisc, { A64Reg::FPCR }, {}, { rn }, { rd });
        tc(i, fpMiscClass(code, a, scalar ? (1u << a) : reg_bytes));
        i->a = a;
        i->b = scalar ? (1u << a) : reg_bytes;
        i->c = code;
        return true;
    }
    bool valid = true;
    bool needs_d = false;
    uint32_t a = size;
    uint64_t imm = 0;
    uint32_t d = 0;
    if ( int_estimate ) {
        if ( scalar || sz ) return false;
        auto* i = fpOp(bundle, addr, "URECPE", INST_FP_ARITH, simdMisc, {}, {}, { rn }, { rd });
        tc(i, reg_bytes == 16 ? A64Tc::FP_CVT_S : A64Tc::FP_CVT);
        i->a = 2;
        i->b = reg_bytes;
        i->c = U ? 29 : 28;
        return true;
    }
    if ( scalar ) {
        switch ( opcode ) {
        case 3: needs_d = true; break;
        case 7: break;
        case 8: case 9: valid = size == 3; break;
        case 10: valid = size == 3 && !U; break;
        case 11: valid = size == 3; break;
        case 0x12: valid = U && size != 3; d = 2; break;
        case 0x14: valid = size != 3; d = 2; break;
        default: valid = false; break;
        }
    }
    else {
        switch ( opcode ) {
        case 0: valid = U ? size < 2 : size != 3; break;
        case 1: valid = !U && size == 0; break;
        case 2: valid = size != 3; break;
        case 6: valid = size != 3; needs_d = true; break;
        case 3: valid = size != 3 || Q; needs_d = true; break;
        case 4: valid = size != 3; break;
        case 5:
            if ( !U ) valid = size == 0;
            else {
                valid = size < 2;
                imm = size;
            }
            a = 0;
            break;
        case 7: case 8: case 9: case 11: valid = size != 3 || Q; break;
        case 10: valid = !U && (size != 3 || Q); break;
        case 0x12: case 0x14: valid = size != 3; d = Q; needs_d = Q; break;
        case 0x13: valid = U && size != 3; d = Q; break;
        default: valid = false; break;
        }
    }
    if ( !valid ) return false;
    auto* i = fpOp(bundle, addr, "INTMISC", INST_FP_ARITH, simdMisc, {}, {},
        needs_d ? std::vector<uint16_t> { rn, rd } : std::vector<uint16_t> { rn }, { rd });
    tc(i, intMiscClass(opcode, U));
    i->a = a;
    i->b = scalar ? (1u << a) : reg_bytes;
    i->c = (ud << 5) | opcode;
    i->d = d;
    i->imm = imm;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdMiscFp16(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t s = bit(ins, 23), opcode = bits(ins, 16, 12);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    const uint32_t code = (static_cast<uint32_t>(U) << 6) | (s << 5) | opcode;
    bool valid;
    if ( scalar ) {
        switch ( code ) {
        case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x2C: case 0x2D: case 0x2E: case 0x3A: case 0x3B: case 0x3D: case 0x3F:
        case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x6C: case 0x6D: case 0x7A: case 0x7B: case 0x7D:
            valid = true;
            break;
        default: valid = false; break;
        }
    }
    else {
        switch ( code ) {
        case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D:
        case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3D:
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D:
        case 0x6C: case 0x6D: case 0x6F:
        case 0x79: case 0x7A: case 0x7B: case 0x7D: case 0x7F:
            valid = true;
            break;
        default: valid = false; break;
        }
    }
    if ( !valid ) return false;
    auto* i = fpOp(bundle, addr, code == 0x7F ? "FSQRT.H" : "FPMISC.H", code == 0x7F ? INST_FP_DIV : INST_FP_ARITH, simdFpMisc, { A64Reg::FPCR }, {}, { rn }, { rd });
    tc(i, fpMiscClass(code, 1, scalar ? 2u : (Q ? 16u : 8u)));
    i->a = 1;
    i->b = scalar ? 2 : (Q ? 16 : 8);
    i->c = code;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdThreeDiff(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t size = bits(ins, 23, 22), opcode = bits(ins, 15, 12);
    const uint16_t rm = bits(ins, 20, 16), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    bool valid;
    if ( scalar ) {
        valid = !U && (opcode == 9 || opcode == 11 || opcode == 13) && (size == 1 || size == 2);
    }
    else {
        switch ( opcode ) {
        case 15: valid = false; break;
        case 14: valid = !U && (size == 0 || size == 3); break;
        case 9: case 11: case 13: valid = !U && (size == 1 || size == 2); break;
        default: valid = size != 3; break;
        }
    }
    if ( !valid ) return false;
    const bool accum = opcode == 5 || opcode == 8 || opcode == 9 || opcode == 10 || opcode == 11;
    const bool high_narrow = (opcode == 4 || opcode == 6) && Q;
    auto* i = fpOp(bundle, addr, "INT3D", INST_FP_ARITH, simdThreeDiff, {}, {},
        (accum || high_narrow) ? std::vector<uint16_t> { rn, rm, rd } : std::vector<uint16_t> { rn, rm }, { rd });
    tc(i, intThreeDiffClass(opcode));
    i->a = size;
    i->b = 16;
    i->c = (static_cast<uint32_t>(U) << 4) | opcode;
    i->d = (Q && !scalar ? 1u : 0u) | (scalar ? 2u : 0u);
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdShiftImm(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t immh = bits(ins, 22, 19), immb = bits(ins, 18, 16), opcode = bits(ins, 15, 11);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( immh == 0 ) return false;
    const uint32_t a = 31 - __builtin_clz(immh);
    const uint32_t esize = 8u << a;
    const uint32_t full = (immh << 3) | immb;
    const uint32_t right = 2 * esize - full;
    const uint32_t left = full - esize;
    const uint32_t reg_bytes = Q ? 16 : 8;
    if ( opcode == 28 || opcode == 31 ) {
        if ( a == 0 || (!scalar && a == 3 && !Q) ) return false;
        auto* i = fpOp(bundle, addr, "FIXCVT.V", INST_FP_ARITH, simdFpFixed, { A64Reg::FPCR }, {}, { rn }, { rd });
        tc(i, cvtClassOf(scalar ? 1u : reg_bytes >> a));
        i->a = a;
        i->b = scalar ? (1u << a) : reg_bytes;
        i->c = (opcode == 31 ? 0u : 2u) + (U ? 1u : 0u);
        i->imm = right;
        return true;
    }
    bool valid = false;
    uint64_t sh = 0;
    uint32_t d = 0;
    bool needs_d = false;
    const bool narrow = opcode >= 16 && opcode <= 19;
    if ( narrow ) {
        valid = a != 3;
        if ( scalar && opcode <= 17 && !U ) valid = false;
        sh = right;
        d = scalar ? 2 : (Q ? 1 : 0);
        needs_d = !scalar && Q;
    }
    else if ( opcode == 20 ) {
        valid = !scalar && a != 3;
        sh = left;
        d = Q ? 1 : 0;
    }
    else {
        switch ( opcode ) {
        case 0: case 4:
            valid = true;
            sh = right;
            break;
        case 2: case 6:
            valid = true;
            sh = right;
            needs_d = true;
            break;
        case 8:
            valid = U;
            sh = right;
            needs_d = true;
            break;
        case 10:
            valid = true;
            sh = left;
            needs_d = U;
            break;
        case 12:
            valid = U;
            sh = left;
            break;
        case 14:
            valid = true;
            sh = left;
            break;
        default: break;
        }
        if ( scalar && opcode != 12 && opcode != 14 && a != 3 ) valid = false;
        if ( !scalar && a == 3 && !Q ) valid = false;
    }
    if ( !valid ) return false;
    auto* i = fpOp(bundle, addr, "SHIFT.V", INST_FP_ARITH, simdShiftImm, {}, {},
        needs_d ? std::vector<uint16_t> { rn, rd } : std::vector<uint16_t> { rn }, { rd });
    tc(i, shiftImmClass(opcode, U));
    i->a = a;
    i->b = scalar ? (1u << a) : reg_bytes;
    i->c = (static_cast<uint32_t>(U) << 5) | opcode;
    i->d = d;
    i->imm = sh;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdModifiedImm(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), op_bit = bit(ins, 29), o2 = bit(ins, 11);
    const uint32_t cmode = bits(ins, 15, 12);
    const uint32_t imm8 = (bits(ins, 18, 16) << 5) | bits(ins, 9, 5);
    const uint16_t rd = bits(ins, 4, 0);
    const uint32_t reg_bytes = Q ? 16 : 8;
    uint64_t pattern = 0;
    uint32_t kind = 0;   // 0 move, 1 ORR, 2 BIC
    bool     invert = false;
    const auto rep32 = [](uint32_t v) { return (static_cast<uint64_t>(v) << 32) | v; };
    const auto rep16 = [](uint32_t v) {
        const uint64_t x = v & 0xFFFF;
        return x | (x << 16) | (x << 32) | (x << 48);
    };
    if ( o2 ) {
        if ( op_bit || cmode != 0xF ) return false;
        pattern = rep16(static_cast<uint32_t>(a64FpExpandImm(imm8, 5, 10)));
    }
    else if ( !(cmode & 8) ) {
        pattern = rep32(imm8 << (8 * ((cmode >> 1) & 3)));
        if ( cmode & 1 ) kind = op_bit ? 2 : 1;
        else invert = op_bit;
    }
    else if ( (cmode & 0xC) == 8 ) {
        pattern = rep16(imm8 << (8 * ((cmode >> 1) & 1)));
        if ( cmode & 1 ) kind = op_bit ? 2 : 1;
        else invert = op_bit;
    }
    else if ( (cmode & 0xE) == 0xC ) {
        const uint32_t v = (cmode & 1) ? ((imm8 << 16) | 0xFFFF) : ((imm8 << 8) | 0xFF);
        pattern = rep32(v);
        invert = op_bit;
    }
    else if ( cmode == 0xE ) {
        if ( !op_bit ) {
            pattern = 0;
            for ( unsigned k = 0; k < 8; ++k ) pattern |= static_cast<uint64_t>(imm8) << (8 * k);
        }
        else {
            for ( unsigned k = 0; k < 8; ++k ) {
                if ( (imm8 >> k) & 1 ) pattern |= 0xFFull << (8 * k);
            }
        }
    }
    else {   // cmode 1111
        if ( !op_bit ) {
            pattern = rep32(static_cast<uint32_t>(a64FpExpandImm(imm8, 8, 23)));
        }
        else {
            if ( !Q ) return false;
            pattern = a64FpExpandImm(imm8, 11, 52);
        }
    }
    if ( invert ) pattern = ~pattern;
    auto* i = op(bundle, addr, "MOVI", INST_FP_ARITH, simdImm, {}, {},
        kind ? std::vector<uint16_t> { rd } : std::vector<uint16_t> {}, { rd });
    tc(i, A64Tc::V_MISC);
    i->b = reg_bytes;
    i->c = kind;
    i->imm = pattern;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdCopy(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), op_bit = bit(ins, 29);
    const uint32_t imm5 = bits(ins, 20, 16), imm4 = bits(ins, 14, 11);
    const uint32_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    const uint16_t rn16 = rn, rd16 = rd;
    if ( (imm5 & 0xF) == 0 ) return false;
    const uint32_t size = __builtin_ctz(imm5);
    const uint32_t bytes = 1u << size;
    const uint32_t index_off = (imm5 >> (size + 1)) << size;
    const uint32_t reg_bytes = Q ? 16 : 8;
    if ( scalar ) {
        if ( op_bit || imm4 != 0 ) return false;
        auto* i = op(bundle, addr, "DUP.S", INST_FP_ARITH, simdDupElem, {}, {}, { rn16 }, { rd16 });
        tc(i, A64Tc::V_MISC);
        i->a = size;
        i->b = bytes;
        i->c = index_off;
        return true;
    }
    if ( op_bit ) {
        if ( !Q ) return false;
        auto* i = op(bundle, addr, "INS.E", INST_FP_ARITH, simdInsElem, {}, {}, { rd16, rn16 }, { rd16 });
        tc(i, A64Tc::V_MISC);
        i->a = size;
        i->c = index_off;
        i->d = (imm4 >> size) << size;
        return true;
    }
    switch ( imm4 ) {
    case 0: {
        if ( size == 3 && !Q ) return false;
        auto* i = op(bundle, addr, "DUP.E", INST_FP_ARITH, simdDupElem, {}, {}, { rn16 }, { rd16 });
        tc(i, A64Tc::V_MISC);
        i->a = size;
        i->b = reg_bytes;
        i->c = index_off;
        return true;
    }
    case 1: {
        if ( size == 3 && !Q ) return false;
        auto* i = op(bundle, addr, "DUP.G", INST_FP_ARITH, simdDupGp, { rz(rn) }, {}, {}, { rd16 });
        tc(i, A64Tc::FP_MOVE_FROM_GP);
        i->a = size;
        i->b = reg_bytes;
        return true;
    }
    case 3: {
        if ( !Q ) return false;
        auto* i = op(bundle, addr, "INS.G", INST_FP_ARITH, simdInsGp, { rz(rn) }, {}, { rd16 }, { rd16 });
        tc(i, A64Tc::FP_INSERT_FROM_GP);
        i->a = size;
        i->c = index_off;
        return true;
    }
    case 5:
    case 7: {
        const bool sign = imm4 == 5;
        if ( sign ) {
            if ( size == 3 || (size == 2 && !Q) ) return false;
        }
        else if ( Q ? size != 3 : size == 3 ) {
            return false;
        }
        auto* i = op(bundle, addr, "MOV.E2G", INST_FP_ARITH, simdMovToGp, {}, { rd == 31 ? A64Reg::T1 : rd16 }, { rn16 }, {});
        tc(i, A64Tc::FP_MOVE_TO_GP);
        i->a = size;
        i->c = index_off;
        i->d = (sign ? 1u : 0u) | (Q ? 0u : 2u);
        return true;
    }
    default: return false;
    }
}

inline bool
VanadisARM64Decoder::decodeSimdPermute(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30);
    const uint32_t size = bits(ins, 23, 22), opcode = bits(ins, 14, 12);
    const uint16_t rm = bits(ins, 20, 16), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( (opcode & 3) == 0 || (size == 3 && !Q) ) return false;
    auto* i = op(bundle, addr, "PERMUTE", INST_FP_ARITH, simdPermute, {}, {}, { rn, rm }, { rd });
    tc(i, A64Tc::V_MISC);
    i->a = size;
    i->b = Q ? 16 : 8;
    i->c = opcode;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdTable(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), tbx = bit(ins, 12);
    const uint32_t len = bits(ins, 14, 13) + 1;
    const uint16_t rm = bits(ins, 20, 16), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( bits(ins, 23, 22) != 0 ) return false;
    std::vector<uint16_t> sources;
    for ( uint32_t k = 0; k < len; ++k ) sources.push_back(static_cast<uint16_t>((rn + k) % 32));
    sources.push_back(rm);
    if ( tbx ) sources.push_back(rd);
    auto* i = op(bundle, addr, "TBL", INST_FP_ARITH, simdTable, {}, {}, sources, { rd });
    tc(i, (len + (tbx ? 1u : 0u)) <= 2 ? A64Tc::V_MISC : A64Tc::V_TBL_LONG);
    i->b = Q ? 16 : 8;
    i->c = len;
    i->d = tbx;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdExt(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30);
    const uint32_t imm4 = bits(ins, 14, 11);
    const uint16_t rm = bits(ins, 20, 16), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( bits(ins, 23, 22) != 0 || (!Q && (imm4 & 8)) ) return false;
    auto* i = op(bundle, addr, "EXT", INST_FP_ARITH, simdExt, {}, {}, { rn, rm }, { rd });
    tc(i, A64Tc::V_MISC);
    i->b = Q ? 16 : 8;
    i->c = imm4;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdAcross(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t size = bits(ins, 23, 22), opcode = bits(ins, 16, 12);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( opcode == 12 || opcode == 15 ) {
        const uint32_t s = size >> 1;
        if ( size & 1 ) return false;
        if ( U && !Q ) return false;
        auto* i = fpOp(bundle, addr, "FPACROSS", INST_FP_ARITH, simdFpReduce, { A64Reg::FPCR }, {}, { rn }, { rd });
        tc(i, A64Tc::FP_REDUCE);
        i->a = U ? 2 : 1;
        i->b = Q ? 16 : 8;
        i->c = (s << 1) | (opcode == 15 ? 1u : 0u);
        return true;
    }
    if ( opcode != 3 && opcode != 10 && opcode != 26 && !(opcode == 27 && !U) ) return false;
    if ( size == 3 || (size == 2 && !Q) ) return false;
    auto* i = op(bundle, addr, "ACROSS", INST_FP_ARITH, simdAcross, {}, {}, { rn }, { rd });
    tc(i, reduceClass((Q ? 16u : 8u) >> size));
    i->a = size;
    i->b = Q ? 16 : 8;
    i->c = opcode;
    i->d = U;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdScalarPair(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Simd;
    const bool     U = bit(ins, 29);
    const uint32_t size = bits(ins, 23, 22), opcode = bits(ins, 16, 12);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( opcode == 27 ) {
        if ( U || size != 3 ) return false;
        auto* i = op(bundle, addr, "ADDP.S", INST_FP_ARITH, simdAcross, {}, {}, { rn }, { rd });
        tc(i, A64Tc::V_ALU);
        i->a = 3;
        i->b = 16;
        i->c = 27;
        return true;
    }
    const uint32_t s = size >> 1, sz = size & 1;
    uint32_t code;
    if ( opcode == 12 ) code = s ? 2 : 0;
    else if ( opcode == 15 ) code = s ? 3 : 1;
    else if ( opcode == 13 && !s ) code = 4;
    else return false;
    uint32_t a;
    if ( U ) a = sz ? 3 : 2;
    else {
        if ( sz ) return false;
        a = 1;
    }
    auto* i = fpOp(bundle, addr, "FPPAIR", INST_FP_ARITH, simdFpReduce, { A64Reg::FPCR }, {}, { rn }, { rd });
    tc(i, A64Tc::FP_PAIRWISE);
    i->a = a;
    i->b = 2u << a;
    i->c = code;
    return true;
}

inline bool
VanadisARM64Decoder::decodeSimdIndexed(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar)
{
    using namespace A64Simd;
    const bool     Q = bit(ins, 30), U = bit(ins, 29);
    const uint32_t size = bits(ins, 23, 22), opcode = bits(ins, 15, 12);
    const uint32_t L = bit(ins, 21), M = bit(ins, 20), H = bit(ins, 11);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    const uint16_t rm4 = bits(ins, 19, 16), rm5 = bits(ins, 20, 16);
    const uint32_t reg_bytes = Q ? 16 : 8;

    // FCMLA by element.
    if ( U && (opcode & 9) == 1 ) {
        if ( scalar || (size != 1 && size != 2) ) return false;
        if ( size == 2 && (L || !Q) ) return false;
        auto* i = fpOp(bundle, addr, "FCMLA.E", INST_FP_ARITH, simdFcmla, { A64Reg::FPCR }, {}, { rn, rm5, rd }, { rd });
        tc(i, A64Tc::FP_COMPLEX_MLA);
        i->a = size;
        i->b = reg_bytes;
        i->c = 1;
        i->d = (opcode >> 1) & 3;
        i->imm = size == 1 ? ((H << 1) | L) : H;
        return true;
    }
    // FMLA, FMLS, FMUL, FMULX by element.
    if ( (!U && (opcode == 1 || opcode == 5 || opcode == 9)) || (U && opcode == 9) ) {
        uint32_t a;
        uint64_t index;
        uint16_t rm;
        if ( size == 0 ) {
            a = 1;
            index = (H << 2) | (L << 1) | M;
            rm = rm4;
        }
        else if ( size == 2 ) {
            a = 2;
            index = (H << 1) | L;
            rm = rm5;
        }
        else if ( size == 3 ) {
            if ( L || (!scalar && !Q) ) return false;
            a = 3;
            index = H;
            rm = rm5;
        }
        else {
            return false;
        }
        const uint32_t code = opcode == 1 ? 0 : opcode == 5 ? 1 : (U ? 3 : 2);
        auto* i = fpOp(bundle, addr, "FPELEM", INST_FP_ARITH, simdFpIndexed, { A64Reg::FPCR }, {},
            code < 2 ? std::vector<uint16_t> { rn, rm, rd } : std::vector<uint16_t> { rn, rm }, { rd });
        tc(i, code < 2 ? A64Tc::FP_MLA : A64Tc::FP_MUL);
        i->a = a;
        i->b = scalar ? (1u << a) : reg_bytes;
        i->c = code;
        i->imm = index;
        if ( code >= 2 ) i->setExtraExecuteCycles(fp_mul_extra_cycles);    // FMUL, FMULX
        if ( code < 2 ) markMultiplyAdd(i, 2);    // FMLA, FMLS
        return true;
    }
    // FMLAL, FMLSL, FMLAL2, FMLSL2 by element.
    if ( (!U && (opcode == 0 || opcode == 4)) || (U && (opcode == 8 || opcode == 12)) ) {
        if ( scalar || size != 2 ) return false;
        auto* i = fpOp(bundle, addr, "FMLAL.E", INST_FP_ARITH, simdFpMulAddLong, { A64Reg::FPCR }, {}, { rn, rm4, rd }, { rd });
        tc(i, A64Tc::FP_MLA);
        i->b = reg_bytes;
        i->c = (opcode & 4) ? 1 : 0;
        i->d = (U ? 1u : 0u) | 2u;
        i->imm = (H << 2) | (L << 1) | M;
        markMultiplyAdd(i, 2);
        return true;
    }
    // Dot products and the BFloat16 forms.
    if ( opcode == 14 || (!U && opcode == 15) ) {
        if ( scalar ) return false;
        const uint64_t group = (H << 1) | L;
        if ( opcode == 14 ) {
            if ( size != 2 ) return false;
            auto* i = op(bundle, addr, "DOT.E", INST_FP_ARITH, simdDot, {}, {}, { rn, rm5, rd }, { rd });
            tc(i, A64Tc::V_DOT);
            i->b = reg_bytes;
            i->c = U ? 1 : 0;
            i->d = 1;
            i->imm = group;
            return true;
        }
        if ( size == 0 || size == 2 ) {
            auto* i = op(bundle, addr, "USDOT.E", INST_FP_ARITH, simdDot, {}, {}, { rn, rm5, rd }, { rd });
            tc(i, A64Tc::V_DOT);
            i->b = reg_bytes;
            i->c = size == 0 ? 3 : 2;
            i->d = 1;
            i->imm = group;
            return true;
        }
        if ( size == 1 ) {
            auto* i = op(bundle, addr, "BFDOT.E", INST_FP_ARITH, simdBfDot, {}, {}, { rn, rm5, rd }, { rd });
            tc(i, A64Tc::BF_DOT);
            i->b = reg_bytes;
            i->d = 1;
            i->imm = group;
            return true;
        }
        auto* i = op(bundle, addr, "BFMLAL.E", INST_FP_ARITH, simdBfMlal, { A64Reg::FPCR }, {}, { rn, rm4, rd }, { rd });
        tc(i, A64Tc::FP_COMPLEX_MLA);
        i->c = Q ? 1 : 0;
        i->d = 1;
        i->imm = (H << 2) | (L << 1) | M;
        markMultiplyAdd(i, 2);
        return true;
    }
    // The integer forms: H or S elements.
    uint64_t index;
    uint16_t rm;
    if ( size == 1 ) {
        index = (H << 2) | (L << 1) | M;
        rm = rm4;
    }
    else if ( size == 2 ) {
        index = (H << 1) | L;
        rm = rm5;
    }
    else {
        return false;
    }
    int same = -1;
    if ( !U ) {
        if ( opcode == 8 ) same = 0;
        else if ( opcode == 12 ) same = 3;
        else if ( opcode == 13 ) same = 4;
    }
    else {
        if ( opcode == 0 ) same = 1;
        else if ( opcode == 4 ) same = 2;
        else if ( opcode == 13 ) same = 5;
        else if ( opcode == 15 ) same = 6;
    }
    if ( same >= 0 ) {
        if ( scalar && same < 3 ) return false;
        const bool needs_d = same == 1 || same == 2 || same >= 5;
        auto* i = fpOp(bundle, addr, "INT.E", INST_FP_ARITH, simdIndexedSame, {}, {},
            needs_d ? std::vector<uint16_t> { rn, rm, rd } : std::vector<uint16_t> { rn, rm }, { rd });
        tc(i, (same == 1 || same == 2 || same >= 5) ? A64Tc::V_MLA : A64Tc::V_MUL);
        i->a = size;
        i->b = scalar ? (1u << size) : reg_bytes;
        i->c = static_cast<uint32_t>(same);
        i->imm = index;
        return true;
    }
    uint32_t code;
    switch ( opcode ) {
    case 2: code = 8; break;
    case 6: code = 10; break;
    case 10: code = 12; break;
    case 3: code = 9; break;
    case 7: code = 11; break;
    case 11: code = 13; break;
    default: return false;
    }
    const bool saturating = code == 9 || code == 11 || code == 13;
    if ( saturating && U ) return false;
    if ( scalar && !saturating ) return false;
    auto* i = fpOp(bundle, addr, "INTL.E", INST_FP_ARITH, simdIndexedLong, {}, {},
        code == 12 || code == 13 ? std::vector<uint16_t> { rn, rm } : std::vector<uint16_t> { rn, rm, rd }, { rd });
    tc(i, (code == 12 || code == 13) ? A64Tc::V_MUL : A64Tc::V_MLA);
    i->a = size;
    i->c = (static_cast<uint32_t>(U) << 4) | code;
    i->d = (Q && !scalar ? 1u : 0u) | (scalar ? 2u : 0u);
    i->imm = index;
    return true;
}

// AESE, AESD, AESMC, AESIMC.
inline bool
VanadisARM64Decoder::decodeCryptoAes(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Crypto;
    const uint32_t opcode = bits(ins, 16, 12);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( !bit(ins, 30) || bit(ins, 29) || bits(ins, 23, 22) != 0 || opcode < 4 || opcode > 7 ) return false;
    const uint32_t code = opcode - 4;
    auto* i = op(bundle, addr, "AES", INST_FP_ARITH, aes, {}, {},
        code < 2 ? std::vector<uint16_t> { rd, rn } : std::vector<uint16_t> { rn }, { rd });
    // AESMC and AESIMC fuse with the AESE or AESD before them (section 4.10 of the X925 guide).
    tc(i, code >= 2 ? A64Tc::CRYPTO_AES_MIX : A64Tc::CRYPTO_AES);
    i->c = code;
    return true;
}

// SHA1C, SHA1P, SHA1M, SHA1SU0, SHA256H, SHA256H2, SHA256SU1.
inline bool
VanadisARM64Decoder::decodeCryptoSha3Reg(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Crypto;
    const uint32_t opcode = bits(ins, 14, 12);
    const uint16_t rm = bits(ins, 20, 16), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( bit(ins, 29) || bits(ins, 23, 22) != 0 ) return false;
    VanadisArm64Inst* i;
    switch ( opcode ) {
    case 0: case 1: case 2:
        i = op(bundle, addr, "SHA1HASH", INST_FP_ARITH, sha1Hash, {}, {}, { rd, rn, rm }, { rd });
        tc(i, A64Tc::CRYPTO_SHA_SLOW);
        i->c = opcode;
        return true;
    case 3:
        tc(op(bundle, addr, "SHA1SU0", INST_FP_ARITH, sha1Su0, {}, {}, { rd, rn, rm }, { rd }), A64Tc::CRYPTO_SHA_FAST);
        return true;
    case 4: case 5:
        i = op(bundle, addr, "SHA256H", INST_FP_ARITH, sha256Hash, {}, {}, { rd, rn, rm }, { rd });
        tc(i, A64Tc::CRYPTO_SHA_SLOW);
        i->c = opcode - 4;
        return true;
    case 6:
        tc(op(bundle, addr, "SHA256SU1", INST_FP_ARITH, sha256Su1, {}, {}, { rd, rn, rm }, { rd }), A64Tc::CRYPTO_SHA_FAST);
        return true;
    default: return false;
    }
}

// SHA1H, SHA1SU1, SHA256SU0.
inline bool
VanadisARM64Decoder::decodeCryptoSha2Reg(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Crypto;
    const uint32_t opcode = bits(ins, 16, 12);
    const uint16_t rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( bit(ins, 29) || bits(ins, 23, 22) != 0 ) return false;
    switch ( opcode ) {
    case 0: tc(op(bundle, addr, "SHA1H", INST_FP_ARITH, sha1Fixed, {}, {}, { rn }, { rd }), A64Tc::CRYPTO_SHA_FAST); return true;
    case 1: tc(op(bundle, addr, "SHA1SU1", INST_FP_ARITH, sha1Su1, {}, {}, { rd, rn }, { rd }), A64Tc::CRYPTO_SHA_FAST); return true;
    case 2: tc(op(bundle, addr, "SHA256SU0", INST_FP_ARITH, sha256Su0, {}, {}, { rd, rn }, { rd }), A64Tc::CRYPTO_SHA_FAST); return true;
    default: return false;
    }
}

// The groups with bit 31 set: SM3TT, three-register SHA512 and SM, four-register, XAR, two-register SHA512 and SM4E.
inline bool
VanadisARM64Decoder::decodeCryptoHigh(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Crypto;
    if ( bits(ins, 31, 24) != 0b11001110 ) return false;
    const uint16_t rm = bits(ins, 20, 16), ra = bits(ins, 14, 10), rn = bits(ins, 9, 5), rd = bits(ins, 4, 0);
    if ( !bit(ins, 23) && !bit(ins, 15) ) {
        const uint32_t kind = bits(ins, 22, 21);
        if ( kind == 3 ) return false;
        if ( kind == 2 ) {
            tc(op(bundle, addr, "SM3SS1", INST_FP_ARITH, sm3Ss1, {}, {}, { rn, rm, ra }, { rd }), A64Tc::CRYPTO_SHA_FAST);
            return true;
        }
        auto* i = op(bundle, addr, "SHA3.4", INST_FP_ARITH, sha3Op, {}, {}, { rn, rm, ra }, { rd });
        tc(i, A64Tc::CRYPTO_SHA3);
        i->c = kind;
        return true;
    }
    if ( bits(ins, 23, 21) == 0b010 && bits(ins, 15, 14) == 2 ) {
        auto* i = op(bundle, addr, "SM3TT", INST_FP_ARITH, sm3Tt, {}, {}, { rd, rn, rm }, { rd });
        tc(i, A64Tc::CRYPTO_SHA_FAST);
        i->c = bits(ins, 11, 10);
        i->imm = bits(ins, 13, 12);
        return true;
    }
    if ( bits(ins, 23, 21) == 0b011 && bit(ins, 15) && bits(ins, 13, 12) == 0 ) {
        const uint32_t o = bit(ins, 14), opcode = bits(ins, 11, 10);
        if ( !o ) {
            if ( opcode == 3 ) {
                auto* i = op(bundle, addr, "RAX1", INST_FP_ARITH, sha3Op, {}, {}, { rn, rm }, { rd });
                tc(i, A64Tc::CRYPTO_SHA3);
                i->c = 2;
                return true;
            }
            auto* i = op(bundle, addr, "SHA512", INST_FP_ARITH, sha512Three, {}, {}, { rd, rn, rm }, { rd });
            tc(i, A64Tc::CRYPTO_SHA_FAST);
            i->c = opcode;
            return true;
        }
        if ( opcode <= 1 ) {
            auto* i = op(bundle, addr, "SM3PART", INST_FP_ARITH, sm3Part, {}, {}, { rd, rn, rm }, { rd });
            tc(i, A64Tc::CRYPTO_SHA_FAST);
            i->c = opcode;
            return true;
        }
        if ( opcode == 2 ) {
            auto* i = op(bundle, addr, "SM4EKEY", INST_FP_ARITH, sm4, {}, {}, { rn, rm }, { rd });
            tc(i, A64Tc::CRYPTO_SHA_SLOW);
            i->c = 1;
            return true;
        }
        return false;
    }
    if ( bits(ins, 23, 21) == 0b100 ) {
        auto* i = op(bundle, addr, "XAR", INST_FP_ARITH, sha3Op, {}, {}, { rn, rm }, { rd });
        tc(i, A64Tc::CRYPTO_SHA3);
        i->c = 3;
        i->imm = bits(ins, 15, 10);
        return true;
    }
    if ( bits(ins, 23, 12) == 0xC08 ) {
        const uint32_t opcode = bits(ins, 11, 10);
        if ( opcode == 0 ) {
            tc(op(bundle, addr, "SHA512SU0", INST_FP_ARITH, sha512Su0, {}, {}, { rd, rn }, { rd }), A64Tc::CRYPTO_SHA_FAST);
            return true;
        }
        if ( opcode == 1 ) {
            auto* i = op(bundle, addr, "SM4E", INST_FP_ARITH, sm4, {}, {}, { rd, rn }, { rd });
            tc(i, A64Tc::CRYPTO_SHA_SLOW);
            i->c = 0;
            return true;
        }
    }
    return false;
}

} // namespace Vanadis
} // namespace SST

#endif
