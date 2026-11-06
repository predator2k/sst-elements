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

// Decoder groups of the scalar floating-point instructions and of the Advanced SIMD structure loads and stores.
// Member functions of VanadisARM64Decoder; included at the end of decoder/varm64decoder.h.

#ifndef _H_VANADIS_ARM64_DEC_FPSCALAR
#define _H_VANADIS_ARM64_DEC_FPSCALAR

namespace SST {
namespace Vanadis {

// VFPExpandImm of the Arm architecture.
inline uint64_t
a64FpExpandImm(uint32_t imm8, int exp_bits, int frac_bits)
{
    const uint64_t sign = (imm8 >> 7) & 1;
    const uint64_t b6 = (imm8 >> 6) & 1;
    uint64_t exp = (b6 ^ 1) << (exp_bits - 1);
    if ( b6 ) exp |= ((1ull << (exp_bits - 3)) - 1) << 2;
    exp |= (imm8 >> 4) & 3;
    const uint64_t frac = static_cast<uint64_t>(imm8 & 0xF) << (frac_bits - 4);
    return (sign << (exp_bits + frac_bits)) | (exp << frac_bits) | frac;
}

inline VanadisArm64Inst*
VanadisARM64Decoder::fpOp(VanadisInstructionBundle* bundle, uint64_t addr, const char* name, VanadisFunctionalUnitType unit,
    VanadisArm64Inst::Semantics fn, const std::vector<uint16_t>& in, const std::vector<uint16_t>& out,
    const std::vector<uint16_t>& fin, const std::vector<uint16_t>& fout)
{
    auto* i = op(bundle, addr, name, unit, fn, in, out, fin, fout);
    i->fp_state = &fp_state_;
    return i;
}

inline bool
VanadisARM64Decoder::decodeFpScalar(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64FpScalar;
    const bool     sf = bit(ins, 31), S = bit(ins, 29);
    const uint32_t ftype = bits(ins, 23, 22);
    const uint32_t rd = bits(ins, 4, 0), rn = bits(ins, 9, 5), rm = bits(ins, 20, 16);
    if ( S ) return false;
    // log2 of the bytes of the format; 0 marks ftype 10, which only the D[1] move and no arithmetic uses.
    const uint32_t fmt = ftype == 0 ? 2 : ftype == 1 ? 3 : ftype == 3 ? 1 : 0;
    const uint16_t rd16 = static_cast<uint16_t>(rd), rn16 = static_cast<uint16_t>(rn), rm16 = static_cast<uint16_t>(rm);

    if ( bit(ins, 24) ) {
        if ( sf || fmt == 0 ) return false;
        auto* i = fpOp(bundle, addr, "FMADD", INST_FP_ARITH, fpDp3, { A64Reg::FPCR }, {},
            { rn16, rm16, static_cast<uint16_t>(bits(ins, 14, 10)) }, { rd16 });
        i->a = fmt;
        i->c = (static_cast<uint32_t>(bit(ins, 21)) << 1) | static_cast<uint32_t>(bit(ins, 15));
        return true;
    }

    if ( !bit(ins, 21) ) {
        // conversion between floating-point and fixed-point
        const uint32_t rmode = bits(ins, 20, 19), opcode = bits(ins, 18, 16), scale = bits(ins, 15, 10);
        if ( fmt == 0 || (!sf && !(scale & 0x20)) ) return false;
        const uint32_t fbits = 64 - scale;
        if ( rmode == 3 && opcode <= 1 ) {
            auto* i = fpOp(bundle, addr, "FCVTZ.FIX", INST_FP_ARITH, fpToGp, { A64Reg::FPCR },
                { rd == 31 ? A64Reg::T1 : rd16 }, { rn16 }, {});
            i->a = fmt;
            i->b = static_cast<uint32_t>(A64Fp::Rounding::ZERO);
            i->c = fbits;
            i->d = (opcode == 0 ? 1u : 0u) | (sf ? 2u : 0u);
            return true;
        }
        if ( rmode == 0 && (opcode == 2 || opcode == 3) ) {
            auto* i = fpOp(bundle, addr, "CVTF.FIX", INST_FP_ARITH, gpToFp, { A64Reg::FPCR, rz(rn) }, {}, {}, { rd16 });
            i->a = fmt;
            i->c = fbits;
            i->d = (opcode == 2 ? 1u : 0u) | (sf ? 2u : 0u);
            return true;
        }
        return false;
    }

    const uint32_t lo2 = bits(ins, 11, 10);
    if ( lo2 == 1 ) {
        // conditional compare
        if ( sf || fmt == 0 ) return false;
        auto* i = fpOp(bundle, addr, "FCCMP", INST_FP_ARITH, fpCondCompare, { A64Reg::FPCR, A64Reg::NZCV }, { A64Reg::NZCV },
            { rn16, rm16 }, {});
        i->a = fmt;
        i->c = bits(ins, 15, 12);
        i->d = bit(ins, 4);
        i->imm = static_cast<uint64_t>(bits(ins, 3, 0)) << 28;
        return true;
    }
    if ( lo2 == 2 ) {
        const uint32_t opcode = bits(ins, 15, 12);
        if ( sf || fmt == 0 || opcode > 8 ) return false;
        auto* i = fpOp(bundle, addr, "FDP2", opcode == 1 ? INST_FP_DIV : INST_FP_ARITH, fpDp2, { A64Reg::FPCR }, {}, { rn16, rm16 }, { rd16 });
        i->a = fmt;
        i->c = opcode;
        return true;
    }
    if ( lo2 == 3 ) {
        if ( sf || fmt == 0 ) return false;
        auto* i = fpOp(bundle, addr, "FCSEL", INST_FP_ARITH, fpCondSelect, { A64Reg::NZCV }, {}, { rn16, rm16 }, { rd16 });
        i->a = fmt;
        i->c = bits(ins, 15, 12);
        return true;
    }

    if ( bits(ins, 15, 10) == 0 ) {
        // conversion between floating-point and integer
        const uint32_t rmode = bits(ins, 20, 19), opcode = bits(ins, 18, 16);
        if ( opcode == 6 || opcode == 7 ) {
            if ( !sf && ftype == 1 && rmode == 3 && opcode == 6 ) {
                auto* i = fpOp(bundle, addr, "FJCVTZS", INST_FP_ARITH, fpJsConvert, { A64Reg::FPCR },
                    { rd == 31 ? A64Reg::T1 : rd16, A64Reg::NZCV }, { rn16 }, {});
                i->a = 3;
                return true;
            }
            const bool high = sf && ftype == 2 && rmode == 1;
            uint32_t   bytes = 0;
            if ( high ) bytes = 8;
            else if ( rmode == 0 && !sf && ftype == 0 ) bytes = 4;
            else if ( rmode == 0 && sf && ftype == 1 ) bytes = 8;
            else if ( rmode == 0 && ftype == 3 ) bytes = 2;
            if ( bytes == 0 ) return false;
            if ( opcode == 6 ) {
                auto* i = op(bundle, addr, "FMOV.TOGP", INST_INT_ARITH, fpMoveToGp, {}, { rd == 31 ? A64Reg::T1 : rd16 }, { rn16 }, {});
                i->a = bytes;
                i->c = high ? 8 : 0;
            }
            else {
                auto* i = op(bundle, addr, "FMOV.FROMGP", INST_INT_ARITH, fpMoveFromGp, { rz(rn) }, {},
                    high ? std::vector<uint16_t> { rd16 } : std::vector<uint16_t> {}, { rd16 });
                i->a = bytes;
                i->d = high;
            }
            return true;
        }
        if ( fmt == 0 ) return false;
        if ( opcode == 2 || opcode == 3 ) {
            if ( rmode != 0 ) return false;
            auto* i = fpOp(bundle, addr, "CVTF", INST_FP_ARITH, gpToFp, { A64Reg::FPCR, rz(rn) }, {}, {}, { rd16 });
            i->a = fmt;
            i->d = (opcode == 2 ? 1u : 0u) | (sf ? 2u : 0u);
            return true;
        }
        A64Fp::Rounding r;
        if ( opcode <= 1 ) {
            static constexpr A64Fp::Rounding by_mode[4] = { A64Fp::Rounding::TIEEVEN, A64Fp::Rounding::POSINF,
                A64Fp::Rounding::NEGINF, A64Fp::Rounding::ZERO };
            r = by_mode[rmode];
        }
        else {   // opcode 4, 5: FCVTAS, FCVTAU
            if ( rmode != 0 ) return false;
            r = A64Fp::Rounding::TIEAWAY;
        }
        auto* i = fpOp(bundle, addr, "FCVT.TOGP", INST_FP_ARITH, fpToGp, { A64Reg::FPCR }, { rd == 31 ? A64Reg::T1 : rd16 }, { rn16 }, {});
        i->a = fmt;
        i->b = static_cast<uint32_t>(r);
        i->d = ((opcode & 1) == 0 ? 1u : 0u) | (sf ? 2u : 0u);
        return true;
    }

    if ( sf ) return false;

    if ( bits(ins, 14, 10) == 0b10000 ) {
        // one source
        const uint32_t opcode = bits(ins, 20, 15);
        if ( opcode == 6 ) {
            if ( ftype != 1 ) return false;
            auto* i = fpOp(bundle, addr, "BFCVT", INST_FP_ARITH, fpDp1, { A64Reg::FPCR }, {}, { rn16 }, { rd16 });
            i->a = 2;
            i->c = 6;
            return true;
        }
        if ( fmt == 0 ) return false;
        bool valid = opcode <= 3;
        if ( opcode == 4 ) valid = fmt != 2;
        if ( opcode == 5 ) valid = fmt != 3;
        if ( opcode == 7 ) valid = fmt != 1;
        if ( opcode >= 8 && opcode <= 15 ) valid = opcode != 13;
        if ( opcode >= 0x10 && opcode <= 0x13 ) valid = fmt != 1;
        if ( !valid ) return false;
        auto* i = fpOp(bundle, addr, opcode == 3 ? "FSQRT" : "FDP1", opcode == 3 ? INST_FP_DIV : INST_FP_ARITH, fpDp1,
            { A64Reg::FPCR }, {}, { rn16 }, { rd16 });
        i->a = fmt;
        i->c = opcode;
        return true;
    }
    if ( bits(ins, 13, 10) == 0b1000 ) {
        // compare
        const uint32_t opcode2 = bits(ins, 4, 0);
        if ( fmt == 0 || bits(ins, 15, 14) != 0 || (opcode2 & 7) != 0 ) return false;
        const bool with_zero = opcode2 & 8;
        if ( with_zero && rm != 0 ) return false;
        auto* i = fpOp(bundle, addr, "FCMP", INST_FP_ARITH, fpCompare, { A64Reg::FPCR }, { A64Reg::NZCV },
            with_zero ? std::vector<uint16_t> { rn16 } : std::vector<uint16_t> { rn16, rm16 }, {});
        i->a = fmt;
        i->d = (opcode2 & 0x10 ? 1u : 0u) | (with_zero ? 2u : 0u);
        return true;
    }
    if ( bits(ins, 12, 10) == 0b100 ) {
        // immediate
        if ( fmt == 0 || bits(ins, 9, 5) != 0 ) return false;
        const uint32_t imm8 = bits(ins, 20, 13);
        auto* i = op(bundle, addr, "FMOV.IMM", INST_FP_ARITH, fpImmediate, {}, {}, {}, { rd16 });
        i->a = fmt;
        i->imm = fmt == 1 ? a64FpExpandImm(imm8, 5, 10) : fmt == 2 ? a64FpExpandImm(imm8, 8, 23) : a64FpExpandImm(imm8, 11, 52);
        return true;
    }
    return false;
}

// LD1 to LD4 and ST1 to ST4, multiple and single structures, with and without post-index.
inline bool
VanadisARM64Decoder::decodeSimdStructure(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64Simd;
    if ( bit(ins, 31) ) return false;
    const bool     Q = bit(ins, 30), post = bit(ins, 23), L = bit(ins, 22), single = bit(ins, 24);
    const uint32_t rm = bits(ins, 20, 16), size = bits(ins, 11, 10), rn = bits(ins, 9, 5), rt = bits(ins, 4, 0);
    const uint16_t base = rs(rn);
    const uint32_t reg_bytes = Q ? 16 : 8;
    if ( !post && rm != 0 ) return false;
    static constexpr uint16_t temps[4] = { A64Reg::VT0, A64Reg::VT1, A64Reg::VT2, A64Reg::VT3 };
    auto vreg = [rt](uint32_t k) { return static_cast<uint16_t>((rt + k) % 32); };
    uint32_t total = 0;

    if ( !single ) {
        if ( bit(ins, 21) ) return false;
        uint32_t rpt, selem;
        switch ( bits(ins, 15, 12) ) {
        case 0b0000: rpt = 1; selem = 4; break;
        case 0b0010: rpt = 4; selem = 1; break;
        case 0b0100: rpt = 1; selem = 3; break;
        case 0b0110: rpt = 3; selem = 1; break;
        case 0b0111: rpt = 1; selem = 1; break;
        case 0b1000: rpt = 1; selem = 2; break;
        case 0b1010: rpt = 2; selem = 1; break;
        default: return false;
        }
        if ( size == 3 && !Q && selem > 1 ) return false;
        total = rpt * selem * reg_bytes;
        const uint32_t ebytes = 1u << size;
        if ( selem == 1 ) {
            for ( uint32_t k = 0; k < rpt; ++k ) {
                if ( L ) loadFp(bundle, addr, base, k * reg_bytes, vreg(k), reg_bytes);
                else storeFp(bundle, addr, base, k * reg_bytes, vreg(k), reg_bytes);
            }
        }
        else {
            std::vector<uint16_t> tl(temps, temps + selem), vl;
            for ( uint32_t k = 0; k < selem; ++k ) vl.push_back(vreg(k));
            if ( L ) {
                for ( uint32_t k = 0; k < selem; ++k ) loadFp(bundle, addr, base, k * reg_bytes, temps[k], reg_bytes);
                auto* i = op(bundle, addr, "UNZIPN", INST_FP_ARITH, structDeinterleave, {}, {}, tl, vl);
                i->a = ebytes;
                i->b = reg_bytes;
                i->c = selem;
            }
            else {
                auto* i = op(bundle, addr, "ZIPN", INST_FP_ARITH, structInterleave, {}, {}, vl, tl);
                i->a = ebytes;
                i->b = reg_bytes;
                i->c = selem;
                for ( uint32_t k = 0; k < selem; ++k ) storeFp(bundle, addr, base, k * reg_bytes, temps[k], reg_bytes);
            }
        }
    }
    else {
        const uint32_t opcode = bits(ins, 15, 13);
        const bool     S = bit(ins, 12), R = bit(ins, 21);
        const uint32_t selem = (((opcode & 1) << 1) | static_cast<uint32_t>(R)) + 1;
        uint32_t       scale = opcode >> 1;
        uint32_t       index = 0;
        bool           replicate = false;
        const uint32_t qbit = Q ? 1 : 0, sbit = S ? 1 : 0;
        switch ( scale ) {
        case 3:
            if ( !L || S ) return false;
            scale = size;
            replicate = true;
            break;
        case 0: index = (qbit << 3) | (sbit << 2) | size; break;
        case 1:
            if ( size & 1 ) return false;
            index = (qbit << 2) | (sbit << 1) | (size >> 1);
            break;
        default:
            if ( size & 2 ) return false;
            if ( !(size & 1) ) {
                index = (qbit << 1) | sbit;
            }
            else {
                if ( S ) return false;
                index = qbit;
                scale = 3;
            }
            break;
        }
        const uint32_t ebytes = 1u << scale;
        total = selem * ebytes;
        for ( uint32_t k = 0; k < selem; ++k ) {
            if ( L ) {
                loadFp(bundle, addr, base, k * ebytes, temps[k], ebytes);
                if ( replicate ) {
                    auto* i = op(bundle, addr, "LDR.REP", INST_FP_ARITH, laneReplicate, {}, {}, { temps[k] }, { vreg(k) });
                    i->a = ebytes;
                    i->b = reg_bytes;
                }
                else {
                    auto* i = op(bundle, addr, "LD.LANE", INST_FP_ARITH, laneInsert, {}, {}, { vreg(k), temps[k] }, { vreg(k) });
                    i->a = ebytes;
                    i->c = index * ebytes;
                }
            }
            else {
                auto* i = op(bundle, addr, "ST.LANE", INST_FP_ARITH, laneExtract, {}, {}, { vreg(k) }, { temps[k] });
                i->a = ebytes;
                i->c = index * ebytes;
                storeFp(bundle, addr, base, k * ebytes, temps[k], ebytes);
            }
        }
    }
    if ( post ) {
        if ( rm == 31 ) {
            addImmTo(bundle, addr, base, base, total);
        }
        else {
            op(bundle, addr, "POSTREG", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
                o.setX(rf, 0, o.X(rf, 0) + o.X(rf, 1));
            }, { base, rz(rm) }, { base });
        }
    }
    return true;
}

} // namespace Vanadis
} // namespace SST

#endif
