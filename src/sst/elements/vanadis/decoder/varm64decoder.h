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

#ifndef _H_VANADIS_ARM64_DECODER
#define _H_VANADIS_ARM64_DECODER

#include "decoder/vdecoder.h"
#include "inst/arm64/sem_crypto.h"
#include "inst/arm64/sem_fpscalar.h"
#include "inst/arm64/sem_simd_fp.h"
#include "inst/arm64/sem_simd_int.h"
#include "inst/arm64/sem_sve.h"
#include "inst/arm64/sem_sve2.h"
#include "inst/arm64/sem_sve_fp.h"
#include "inst/arm64/varm64mem.h"
#include "inst/arm64/sem_int.h"
#include "inst/arm64/varm64inst.h"
#include "inst/vinstall.h"
#include "os/varm64cpuos.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace SST {
namespace Vanadis {

struct A64SveDtype;

// A64 decoder of the Vanadis core: EL0 only, little-endian. decode() walks the top-level encoding
// groups of the A64 instruction set; each group is a member function. One instruction becomes one
// bundle of micro-ops. The vector length of SVE is a parameter of the decoder, and it is the width of
// the core's floating-point register class.
class VanadisARM64Decoder : public VanadisDecoder
{
public:
    SST_ELI_REGISTER_SUBCOMPONENT(
        VanadisARM64Decoder,
    #ifdef VANADIS_BUILD_DEBUG
        "vanadisdbg",
    #else
        "vanadis",
    #endif
        "VanadisARM64Decoder",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Implements an AArch64 (EL0) decoder for Vanadis CPU processing.",
        SST::Vanadis::VanadisDecoder)

    SST_ELI_DOCUMENT_PARAMS(
        { "halt_on_decode_fault", "Fatal error if a decode fault occurs, used for debugging", "0" },
        { "entry_point", "Starting instruction pointer; 0 falls back to the core's ELF reader", "0" },
        { "vector_bytes", "SVE vector length in bytes: a multiple of 16 from 16 to 256; 16 with no SVE", "16" },
        { "dczid_block_bytes", "Block size DC ZVA zeroes and DCZID_EL0 reports, bytes (a power of two)", "64" },
        { "counter_hz", "Frequency CNTFRQ_EL0 reports and CNTVCT_EL0 counts at, Hz", "1000000000" },
        { "integer_mul_extra_cycles", "Cycles a scalar integer multiply takes above the latency of the integer unit", "0" },
        { "fp_mul_extra_cycles", "Cycles a floating-point multiply takes above the latency of the FP unit. A multiply-add has the unit latency, because the accumulator input is bypassed", "0" },
        { "core_hz", "Core clock, Hz; CNTVCT_EL0 is the cycle count scaled by counter_hz / core_hz", "1000000000" },
        { "unimplemented_as_nop", "Test aid: decode an instruction the decoder does not implement as a no-op and print "
                                  "its word, instead of a decode fault", "0" })

    VanadisARM64Decoder(ComponentId_t id, Params& params, SST::Output* output) : VanadisDecoder(id, params, output)
    {
        vector_bytes = params.find<uint32_t>("vector_bytes", 16);
        if ( vector_bytes < 16 || vector_bytes > 256 || (vector_bytes % 16) != 0 ) {
            output_->fatal(CALL_INFO, -1, "vector_bytes must be a multiple of 16 from 16 to 256, got %" PRIu32 "\n",
                vector_bytes);
        }
        dczid_block_bytes = params.find<uint32_t>("dczid_block_bytes", 64);
        counter_hz        = params.find<uint64_t>("counter_hz", 1000000000ull);
        integer_mul_extra_cycles = params.find<uint16_t>("integer_mul_extra_cycles", 0);
        fp_mul_extra_cycles      = params.find<uint16_t>("fp_mul_extra_cycles", 0);
        core_hz           = params.find<uint64_t>("core_hz", 1000000000ull);
        options           = new VanadisDecoderOptions(
            A64Reg::NO_ZERO_REG, A64Reg::INT_COUNT, A64Reg::FP_COUNT, /* syscall code */ 8, VANADIS_REGISTER_MODE_FP64,
            static_cast<uint16_t>(vector_bytes), /* FP load fills upper bytes with */ 0);
        setInstructionPointer(params.find<uint64_t>("entry_point", 0));
        fatal_decode_fault = params.find<bool>("halt_on_decode_fault", false);
        unimplemented_as_nop = params.find<bool>("unimplemented_as_nop", false);
    }

    ~VanadisARM64Decoder() {}

    const char*                  getISAName() const override { return "ARM64"; }
    uint16_t                     countISAIntReg() const override { return options->countISAIntRegisters(); }
    uint16_t                     countISAFPReg() const override { return options->countISAFPRegisters(); }
    const VanadisDecoderOptions* getDecoderOptions() const override { return options; }
    VanadisFPRegisterMode        getFPRegisterMode() const override { return VANADIS_REGISTER_MODE_FP64; }

    void setStackPointer(VanadisISATable* isa_tbl, VanadisRegisterFile* regFile, const uint64_t start_stack) override
    {
        regFile->setIntReg<uint64_t>(isa_tbl->getIntPhysReg(A64Reg::SP), start_stack);
    }

    void setArg1Register(VanadisISATable* isa_tbl, VanadisRegisterFile* regFile, const uint64_t value) override
    {
        regFile->setIntReg<uint64_t>(isa_tbl->getIntPhysReg(0), value);
    }

    void setReturnRegister(VanadisISATable* isa_tbl, VanadisRegisterFile* regFile, const uint64_t value) override
    {
        regFile->setIntReg<uint64_t>(isa_tbl->getIntPhysReg(0), value);
    }

    void setThreadPointer(VanadisISATable* isa_tbl, VanadisRegisterFile* regFile, const uint64_t value) override
    {
        regFile->setIntReg<uint64_t>(isa_tbl->getIntPhysReg(A64Reg::TPIDR), value);
    }

    // The fetch and bundle logic is the same as in the RISC-V decoder, without compressed instructions.
    bool tick(uint64_t cycle) override
    {
        cycle_count  = cycle;
        bool success = false;
        if ( !thread_rob->full() ) {
            if ( ins_loader->hasBundleAt(ip) ) {
                stat_uop_hit->addData(1);
                VanadisInstructionBundle* bundle = ins_loader->getBundleAt(ip);
                // A bundle that can never fit would stall the core for ever: an SVE store has one micro-op per element.
                if ( bundle->getInstructionCount() >= thread_rob->capacity() ) {
                    output_->fatal(CALL_INFO, -1, "The instruction at 0x%" PRI_ADDR " has %" PRIu32 " micro-ops; reorder_slots (%" PRIu64 ") must be larger.\n",
                        ip, static_cast<uint32_t>(bundle->getInstructionCount()), static_cast<uint64_t>(thread_rob->capacity()));
                }
                if ( bundle->getInstructionCount() < (thread_rob->capacity() - thread_rob->size()) ) {
                    bool bundle_has_branch = false;
                    for ( uint32_t i = 0; i < bundle->getInstructionCount(); ++i ) {
                        VanadisInstruction* next_ins = bundle->getInstructionByIndex(i);
                        if ( next_ins->getInstFuncType() == INST_BRANCH ) {
                            VanadisSpeculatedInstruction* next_spec_ins =
                                dynamic_cast<VanadisSpeculatedInstruction*>(next_ins);
                            const uint64_t predicted_address =
                                branch_predictor->predictNext(ip, ip + bundle->pcIncrement(), next_spec_ins->getBranchClass());
                            next_spec_ins->setSpeculatedAddress(predicted_address);
                            ip                = predicted_address;
                            bundle_has_branch = true;
                        }
                        thread_rob->push(next_ins->clone());
                    }
                    ip      = bundle_has_branch ? ip : ip + bundle->pcIncrement();
                    success = true;
                }
                else {
                    stat_uop_delayed_rob_full->addData(1);
                }
            }
            else if ( ins_loader->hasPredecodeAt(ip, 4) ) {
                VanadisInstructionBundle* decoded_bundle = new VanadisInstructionBundle(ip);
                stat_predecode_hit->addData(1);
                uint32_t   temp_ins = 0;
                const bool predecode_bytes =
                    ins_loader->getPredecodeBytes(ip, reinterpret_cast<uint8_t*>(&temp_ins), sizeof(temp_ins));
                if ( LIKELY(predecode_bytes) ) {
                    decode(ip, temp_ins, decoded_bundle);
                    ins_loader->cacheDecodedBundle(decoded_bundle);
                    if ( 0 == decoded_bundle->getInstructionCount() ) {
                        output_->fatal(CALL_INFO, -1, "Error - bundle at: 0x%" PRI_ADDR " generates no micro-ops.\n", ip);
                    }
                }
                else {
                    output_->fatal(CALL_INFO, -1, "Error - predecoded bytes for 0x%" PRIu64 " not retrieved.\n", ip);
                }
                success = true;
            }
            else if ( !ins_loader->pendingLoad(ip, 4) ) {
                ins_loader->requestLoadAt(ip, 4);
                stat_ins_bytes_loaded->addData(4);
                stat_predecode_miss->addData(1);
                success = true;
            }
        }
        return success;
    }

    void checkpointState(FILE* fp) override { fprintf(fp, "FPSR %" PRIx32 "\n", fp_state_.fpsr); }
    void checkpointStateLoad(FILE* fp) override
    {
        uint32_t value = 0;
        if ( 1 != fscanf(fp, "FPSR %" SCNx32 "\n", &value) ) output_->fatal(CALL_INFO, -1, "The checkpoint file has no FPSR.\n");
        fp_state_.fpsr = value;
    }

    // Cycle count of the decoder and the counter frequency, read by MRS CNTVCT_EL0 and CNTFRQ_EL0.
    uint64_t counterValue() const
    {
        return static_cast<uint64_t>(static_cast<unsigned __int128>(cycle_count) * counter_hz / core_hz);
    }

protected:
    const VanadisDecoderOptions* options;
    bool                         fatal_decode_fault;
    bool                         unimplemented_as_nop = false;
    uint32_t                     vector_bytes;
    uint32_t                     dczid_block_bytes;
    uint64_t                     counter_hz;
    uint16_t                     integer_mul_extra_cycles;
    uint16_t                     fp_mul_extra_cycles;
    uint64_t                     core_hz;

    // ---------------------------------------------------------------- field helpers
    static constexpr uint32_t bits(uint32_t ins, int hi, int lo) { return (ins >> lo) & ((1ull << (hi - lo + 1)) - 1); }
    static constexpr bool     bit(uint32_t ins, int b) { return (ins >> b) & 1; }
    static int64_t            sext(uint64_t v, int width) { return static_cast<int64_t>(v << (64 - width)) >> (64 - width); }

    // A source register in a position where 31 means XZR, and one where 31 means SP.
    static uint16_t rz(uint32_t r) { return r == 31 ? A64Reg::ZR : static_cast<uint16_t>(r); }
    static uint16_t rs(uint32_t r) { return static_cast<uint16_t>(r); }
    // A destination list: empty when the destination is XZR.
    static std::vector<uint16_t> dz(uint32_t r) { return r == 31 ? std::vector<uint16_t>{} : std::vector<uint16_t>{ static_cast<uint16_t>(r) }; }

    // A bundle stores a clone of every instruction it is given, so a micro-op whose payload the caller still
    // sets is held here and added when the next micro-op comes or the decode ends.
    VanadisArm64Inst* pending_ = nullptr;

    void flushPending(VanadisInstructionBundle* bundle)
    {
        if ( pending_ != nullptr ) {
            bundle->addInstruction(pending_);
            delete pending_;
            pending_ = nullptr;
        }
    }

    // Adds one micro-op to the bundle after any pending one, and frees the original.
    void add(VanadisInstructionBundle* bundle, VanadisInstruction* inst)
    {
        flushPending(bundle);
        bundle->addInstruction(inst);
        delete inst;
    }

    VanadisArm64Inst* op(VanadisInstructionBundle* bundle, uint64_t addr, const char* name, VanadisFunctionalUnitType unit,
        VanadisArm64Inst::Semantics fn, const std::vector<uint16_t>& in, const std::vector<uint16_t>& out,
        const std::vector<uint16_t>& fin = {}, const std::vector<uint16_t>& fout = {})
    {
        flushPending(bundle);
        pending_ = new VanadisArm64Inst(addr, hw_thr, options, name, unit, fn, in, out, fin, fout);
        pending_->fp_state = &fp_state_;
        return pending_;
    }

    // DecodeBitMasks of the Arm architecture: false for a reserved encoding.
    static bool decodeBitMasks(bool N, uint32_t imms, uint32_t immr, bool sf, uint64_t* wmask, uint64_t* tmask)
    {
        const uint32_t v = (static_cast<uint32_t>(N) << 6) | ((~imms) & 0x3F);
        if ( v == 0 ) return false;
        int len = 31 - __builtin_clz(v);
        if ( len < 1 ) return false;
        if ( !sf && len > 5 ) return false;
        const uint32_t levels = (1u << len) - 1;
        const uint32_t S = imms & levels, R = immr & levels;
        const uint32_t diff = (S - R) & levels;
        const uint32_t esize = 1u << len;
        auto ones = [](uint32_t n) { return n >= 64 ? ~0ull : ((1ull << n) - 1); };
        const uint64_t welem = ones(S + 1);
        const uint64_t telem = ones(diff + 1);
        auto ror = [esize](uint64_t x, uint32_t r) {
            if ( r == 0 ) return x;
            const uint64_t m = esize >= 64 ? ~0ull : ((1ull << esize) - 1);
            x &= m;
            return ((x >> r) | (x << (esize - r))) & m;
        };
        auto repl = [esize](uint64_t x) {
            uint64_t out = 0;
            for ( uint32_t i = 0; i < 64; i += esize ) out |= x << i;
            return out;
        };
        uint64_t w = repl(ror(welem, R)), t = repl(telem);
        if ( !sf ) {
            w &= 0xFFFFFFFFull;
            t &= 0xFFFFFFFFull;
        }
        *wmask = w;
        *tmask = t;
        return true;
    }

    // ---------------------------------------------------------------- top level
    void decode(const uint64_t addr, const uint32_t ins, VanadisInstructionBundle* bundle)
    {
        bool ok = false;
        const uint32_t op0 = bits(ins, 28, 25);
        if ( (op0 & 0b1110) == 0b1000 ) {
            ok = decodeDataProcImm(addr, ins, bundle);
        }
        else if ( (op0 & 0b1110) == 0b1010 ) {
            ok = decodeBranchSys(addr, ins, bundle);
        }
        else if ( (op0 & 0b0101) == 0b0100 ) {
            ok = decodeLoadStore(addr, ins, bundle);
        }
        else if ( (op0 & 0b0111) == 0b0101 ) {
            ok = decodeDataProcReg(addr, ins, bundle);
        }
        else if ( (op0 & 0b0111) == 0b0111 ) {
            ok = decodeFpSimd(addr, ins, bundle);
        }
        else if ( op0 == 0b0010 ) {
            ok = decodeSve(addr, ins, bundle);
        }
        flushPending(bundle);
        if ( !ok && unimplemented_as_nop ) {
            output_->output("UNIMPLEMENTED 0x%08" PRIx32 " at 0x%" PRI_ADDR "\n", ins, addr);
            add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
            return;
        }
        if ( !ok ) {
            if ( fatal_decode_fault ) {
                output_->fatal(CALL_INFO, -1, "[decode] unknown instruction 0x%08" PRIx32 " at 0x%" PRI_ADDR "\n", ins, addr);
            }
            add(bundle, new VanadisInstructionDecodeFault(addr, hw_thr, options));
        }
    }

    // ---------------------------------------------------------------- data processing, immediate
    bool decodeDataProcImm(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t rd = bits(ins, 4, 0), rn = bits(ins, 9, 5);
        const bool     sf = bit(ins, 31);
        switch ( bits(ins, 25, 23) ) {
        case 0b000:
        case 0b001:
        {   // ADR, ADRP
            const int64_t imm = sext((bits(ins, 23, 5) << 2) | bits(ins, 30, 29), 21);
            uint64_t      val = bit(ins, 31) ? ((addr & ~0xFFFull) + (imm << 12)) : (addr + imm);
            if ( rd == 31 ) {
                add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
                return true;
            }
            op(bundle, addr, bit(ins, 31) ? "ADRP" : "ADR", INST_INT_ARITH, movImm, {}, { static_cast<uint16_t>(rd) })->imm = val;
            return true;
        }
        case 0b010:
        {   // add/subtract immediate
            const uint32_t sh = bit(ins, 22);
            const uint64_t imm = static_cast<uint64_t>(bits(ins, 21, 10)) << (sh ? 12 : 0);
            const bool sub = bit(ins, 30), set = bit(ins, 29);
            // Rd is SP-capable without S, XZR with S; Rn is always SP-capable.
            std::vector<uint16_t> out;
            uint32_t flags = 0;
            if ( set ) {
                if ( rd != 31 ) { out.push_back(rd); flags |= HAS_RD; }
                out.push_back(A64Reg::NZCV);
                flags |= SET_FLAGS;
            }
            else {
                out.push_back(rd);
                flags |= HAS_RD;
            }
            auto* i = op(bundle, addr, sub ? (set ? "SUBSI" : "SUBI") : (set ? "ADDSI" : "ADDI"), INST_INT_ARITH, addSub, { rs(rn) }, out);
            i->a = sf; i->b = sub; i->d = flags; i->imm = imm; i->imm2 = OP2_IMM;
            return true;
        }
        case 0b100:
        {   // logical immediate
            uint64_t wmask, tmask;
            const bool N = bit(ins, 22);
            if ( !sf && N ) return false;
            if ( !decodeBitMasks(N, bits(ins, 15, 10), bits(ins, 21, 16), sf, &wmask, &tmask) ) return false;
            const uint32_t opc = bits(ins, 30, 29);
            const bool set = opc == 3;
            std::vector<uint16_t> out;
            uint32_t flags = 0;
            if ( set ) {
                if ( rd != 31 ) { out.push_back(rd); flags |= HAS_RD; }
                out.push_back(A64Reg::NZCV);
                flags |= SET_FLAGS;
            }
            else {
                out.push_back(rd);   // SP-capable
                flags |= HAS_RD;
            }
            auto* i = op(bundle, addr, "LOGICI", INST_INT_ARITH, logicalOp, { rz(rn) }, out);
            i->a = sf; i->d = flags; i->imm = wmask; i->imm2 = OP2_IMM;
            i->b = (opc == 1) ? 1 : (opc == 2 ? 2 : 0);
            return true;
        }
        case 0b101:
        {   // move wide
            const uint32_t opc = bits(ins, 30, 29), hw = bits(ins, 22, 21);
            if ( opc == 1 || (!sf && hw >= 2) ) return false;
            const uint64_t imm = static_cast<uint64_t>(bits(ins, 20, 5)) << (hw * 16);
            if ( rd == 31 ) {
                add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
                return true;
            }
            if ( opc == 3 ) {
                auto* i = op(bundle, addr, "MOVK", INST_INT_ARITH, movK, { rz(rd) }, { static_cast<uint16_t>(rd) });
                i->a = sf; i->d = HAS_RD; i->imm = imm; i->imm2 = 0xFFFFull << (hw * 16);
            }
            else {
                // opc 0 is MOVN, 2 is MOVZ
                uint64_t v = opc == 2 ? imm : ~imm;
                if ( !sf ) v &= 0xFFFFFFFFull;
                op(bundle, addr, opc == 2 ? "MOVZ" : "MOVN", INST_INT_ARITH, movImm, {}, { static_cast<uint16_t>(rd) })->imm = v;
            }
            return true;
        }
        case 0b110:
        {   // bitfield
            uint64_t wmask, tmask;
            const uint32_t opc = bits(ins, 30, 29);
            const bool N = bit(ins, 22);
            if ( opc == 3 || N != sf ) return false;
            const uint32_t immr = bits(ins, 21, 16), imms = bits(ins, 15, 10);
            if ( !decodeBitMasks(N, imms, immr, sf, &wmask, &tmask) ) return false;
            if ( rd == 31 ) {
                add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
                return true;
            }
            std::vector<uint16_t> in{ rz(rn) };
            if ( opc == 1 ) in.push_back(rz(rd));
            auto* i = op(bundle, addr, opc == 0 ? "SBFM" : (opc == 1 ? "BFM" : "UBFM"), INST_INT_ARITH, bitfield, in,
                { static_cast<uint16_t>(rd) });
            i->a = sf; i->b = opc; i->c = immr; i->d = HAS_RD | (imms << 16); i->imm = wmask; i->imm2 = tmask;
            return true;
        }
        case 0b111:
        {   // extract
            if ( bits(ins, 30, 29) != 0 || bit(ins, 21) || bit(ins, 22) != sf || (!sf && bit(ins, 15)) ) return false;
            if ( rd == 31 ) {
                add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
                return true;
            }
            auto* i = op(bundle, addr, "EXTR", INST_INT_ARITH, extr, { rz(rn), rz(bits(ins, 20, 16)) }, { static_cast<uint16_t>(rd) });
            i->a = sf; i->c = bits(ins, 15, 10); i->d = HAS_RD;
            return true;
        }
        default:
            return false;   // 0b011: add/subtract with tags (MTE) is not implemented
        }
    }

    // ---------------------------------------------------------------- data processing, register
    bool decodeDataProcReg(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t rd = bits(ins, 4, 0), rn = bits(ins, 9, 5), rm = bits(ins, 20, 16);
        const bool     sf = bit(ins, 31);
        const uint32_t op1 = bit(ins, 28);
        const uint32_t op2 = bits(ins, 24, 21);
        // Destination list and flags for "Rd in XZR position, optional S".
        auto rd_out = [&](bool set, uint32_t& flags) {
            std::vector<uint16_t> out;
            flags = 0;
            if ( rd != 31 ) { out.push_back(rd); flags |= HAS_RD; }
            if ( set ) { out.push_back(A64Reg::NZCV); flags |= SET_FLAGS; }
            return out;
        };
        auto nop = [&]() {
            add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
            return true;
        };
        if ( op1 == 0 ) {
            if ( (op2 & 0b1000) == 0 ) {
                // logical, shifted register
                const uint32_t opc = bits(ins, 30, 29), shift = bits(ins, 23, 22), amount = bits(ins, 15, 10);
                const bool invert = bit(ins, 21);
                if ( !sf && (amount & 0x20) ) return false;
                const bool set = opc == 3;
                uint32_t flags;
                const auto out = rd_out(set, flags);
                if ( out.empty() ) return nop();
                auto* i = op(bundle, addr, "LOGIC", INST_INT_ARITH, logicalOp, { rz(rn), rz(rm) }, out);
                i->a = sf; i->d = flags; i->imm2 = OP2_SHIFT;
                i->b = (opc == 1) ? 1 : (opc == 2 ? 2 : 0);
                i->c = (shift << 8) | amount | (invert ? (1u << 16) : 0);
                return true;
            }
            const bool sub = bit(ins, 30), set = bit(ins, 29);
            if ( !bit(ins, 21) ) {
                // add/subtract, shifted register
                const uint32_t shift = bits(ins, 23, 22), amount = bits(ins, 15, 10);
                if ( shift == 3 || (!sf && (amount & 0x20)) ) return false;
                uint32_t flags;
                const auto out = rd_out(set, flags);
                if ( out.empty() ) return nop();
                auto* i = op(bundle, addr, sub ? "SUB" : "ADD", INST_INT_ARITH, addSub, { rz(rn), rz(rm) }, out);
                i->a = sf; i->b = sub; i->d = flags; i->imm2 = OP2_SHIFT; i->c = (shift << 8) | amount;
                return true;
            }
            // add/subtract, extended register
            if ( bits(ins, 23, 22) != 0 ) return false;
            const uint32_t option = bits(ins, 15, 13), amount = bits(ins, 12, 10);
            if ( amount > 4 ) return false;
            std::vector<uint16_t> out;
            uint32_t flags = 0;
            if ( set ) {
                if ( rd != 31 ) { out.push_back(rd); flags |= HAS_RD; }
                out.push_back(A64Reg::NZCV);
                flags |= SET_FLAGS;
            }
            else {
                out.push_back(rd);    // SP-capable
                flags |= HAS_RD;
            }
            auto* i = op(bundle, addr, sub ? "SUBX" : "ADDX", INST_INT_ARITH, addSub, { rs(rn), rz(rm) }, out);
            i->a = sf; i->b = sub; i->d = flags; i->imm2 = OP2_EXT; i->c = (option << 8) | amount;
            return true;
        }
        // op1 == 1
        if ( op2 == 0b0000 ) {
            const uint32_t op3 = bits(ins, 15, 10);
            if ( op3 == 0 ) {
                // ADC, ADCS, SBC, SBCS
                const bool sub = bit(ins, 30), set = bit(ins, 29);
                uint32_t flags;
                const auto out = rd_out(set, flags);
                if ( out.empty() ) return nop();
                auto* i = op(bundle, addr, sub ? "SBC" : "ADC", INST_INT_ARITH, addSubCarry, { rz(rn), rz(rm), A64Reg::NZCV }, out);
                i->a = sf; i->b = sub; i->d = flags;
                return true;
            }
            if ( (op3 & 0b011111) == 0b000001 ) {
                // RMIF
                if ( !sf || !bit(ins, 29) || bit(ins, 30) || bit(ins, 4) ) return false;
                auto* i = op(bundle, addr, "RMIF", INST_INT_ARITH, rmif, { rz(rn), A64Reg::NZCV }, { A64Reg::NZCV });
                i->c = bits(ins, 20, 15); i->imm = bits(ins, 3, 0);
                return true;
            }
            if ( (op3 & 0b001111) == 0b000010 ) {
                // SETF8, SETF16
                if ( sf || bit(ins, 30) || !bit(ins, 29) || bits(ins, 20, 15) != 0 || bits(ins, 4, 0) != 0b01101 ) return false;
                auto* i = op(bundle, addr, "SETF", INST_INT_ARITH, setf, { rz(rn), A64Reg::NZCV }, { A64Reg::NZCV });
                i->c = bit(ins, 14) ? 16 : 8;
                return true;
            }
            return false;
        }
        if ( op2 == 0b0010 ) {
            // conditional compare
            if ( !bit(ins, 29) || bit(ins, 10) || bit(ins, 4) ) return false;
            const bool is_imm = bit(ins, 11), sub = bit(ins, 30);
            std::vector<uint16_t> in{ rz(rn), A64Reg::NZCV };
            if ( !is_imm ) in.push_back(rz(rm));
            auto* i = op(bundle, addr, sub ? "CCMP" : "CCMN", INST_INT_ARITH, condCompare, in, { A64Reg::NZCV });
            i->a = sf; i->b = (sub ? 1 : 0) | (is_imm ? 0 : 2); i->c = bits(ins, 15, 12); i->d = SET_FLAGS;
            i->imm = rm; i->imm2 = static_cast<uint64_t>(bits(ins, 3, 0)) << 28;
            return true;
        }
        if ( op2 == 0b0100 ) {
            // conditional select
            if ( bit(ins, 29) || bit(ins, 11) ) return false;
            if ( rd == 31 ) return nop();
            auto* i = op(bundle, addr, "CSEL", INST_INT_ARITH, condSelect, { rz(rn), rz(rm), A64Reg::NZCV }, { static_cast<uint16_t>(rd) });
            i->a = sf; i->d = HAS_RD; i->c = bits(ins, 15, 12);
            i->b = (bit(ins, 30) ? 2 : 0) | (bit(ins, 10) ? 1 : 0);
            return true;
        }
        if ( op2 == 0b0110 ) {
            const uint32_t op3 = bits(ins, 15, 10);
            if ( bit(ins, 30) ) {
                // data processing, one source
                if ( bit(ins, 29) || bits(ins, 20, 16) != 0 ) return false;
                Dp1Op kind;
                switch ( op3 ) {
                case 0: kind = Dp1Op::RBIT; break;
                case 1: kind = Dp1Op::REV16; break;
                case 2: kind = Dp1Op::REV32; break;
                case 3: if ( !sf ) return false; kind = Dp1Op::REV64; break;
                case 4: kind = Dp1Op::CLZ; break;
                case 5: kind = Dp1Op::CLS; break;
                case 6: kind = Dp1Op::CTZ; break;
                case 7: kind = Dp1Op::CNT; break;
                case 8: kind = Dp1Op::ABS; break;
                default: return false;
                }
                if ( rd == 31 ) return nop();
                auto* i = op(bundle, addr, "DP1", INST_INT_ARITH, dataProc1, { rz(rn) }, { static_cast<uint16_t>(rd) });
                i->a = sf; i->b = static_cast<uint32_t>(kind); i->d = HAS_RD;
                // REV (32-bit operation) is opc 2; 64-bit REV32 keeps the word order.
                if ( kind == Dp1Op::REV32 && !sf ) i->b = static_cast<uint32_t>(Dp1Op::REV32);
                return true;
            }
            // data processing, two sources
            if ( bit(ins, 29) ) return false;
            if ( (op3 & 0b111000) == 0b010000 ) {
                // CRC32
                const uint32_t sz = bits(ins, 11, 10);
                if ( (sz == 3) != sf ) return false;
                if ( rd == 31 ) return nop();
                auto* i = op(bundle, addr, "CRC32", INST_INT_ARITH, crc32, { rz(rn), rz(rm) }, { static_cast<uint16_t>(rd) });
                i->a = 0; i->b = 1u << sz; i->d = HAS_RD;
                i->c = bit(ins, 12) ? 0x82F63B78u : 0xEDB88320u;
                return true;
            }
            Dp2Op kind;
            VanadisFunctionalUnitType unit = INST_INT_ARITH;
            switch ( op3 ) {
            case 0b000010: kind = Dp2Op::UDIV; unit = INST_INT_DIV; break;
            case 0b000011: kind = Dp2Op::SDIV; unit = INST_INT_DIV; break;
            case 0b001000: kind = Dp2Op::LSLV; break;
            case 0b001001: kind = Dp2Op::LSRV; break;
            case 0b001010: kind = Dp2Op::ASRV; break;
            case 0b001011: kind = Dp2Op::RORV; break;
            default: return false;
            }
            if ( rd == 31 ) return nop();
            auto* i = op(bundle, addr, "DP2", unit, dataProc2, { rz(rn), rz(rm) }, { static_cast<uint16_t>(rd) });
            i->a = sf; i->b = static_cast<uint32_t>(kind); i->d = HAS_RD;
            return true;
        }
        if ( op2 & 0b1000 ) {
            // data processing, three sources
            if ( bits(ins, 30, 29) != 0 ) return false;
            const uint32_t op31 = bits(ins, 23, 21);
            const bool o0 = bit(ins, 15);
            const uint32_t ra = bits(ins, 14, 10);
            Dp3Op kind;
            bool  is64 = sf;
            switch ( op31 ) {
            case 0: kind = o0 ? Dp3Op::MSUB : Dp3Op::MADD; break;
            case 1: if ( !sf ) return false; kind = o0 ? Dp3Op::SMSUBL : Dp3Op::SMADDL; break;
            case 2: if ( !sf || o0 || ra != 31 ) return false; kind = Dp3Op::SMULH; break;
            case 5: if ( !sf ) return false; kind = o0 ? Dp3Op::UMSUBL : Dp3Op::UMADDL; break;
            case 6: if ( !sf || o0 || ra != 31 ) return false; kind = Dp3Op::UMULH; break;
            default: return false;
            }
            if ( rd == 31 ) return nop();
            auto* i = op(bundle, addr, "DP3", INST_INT_ARITH, dataProc3, { rz(rn), rz(rm), rz(ra) }, { static_cast<uint16_t>(rd) });
            i->a = is64; i->b = static_cast<uint32_t>(kind); i->d = HAS_RD;
            i->setExtraExecuteCycles(integer_mul_extra_cycles);
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------- branches, exceptions, system
    bool decodeBranchSys(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        const uint32_t op0 = bits(ins, 31, 29);
        if ( (op0 & 0b011) == 0b000 && !bit(ins, 29) && bits(ins, 30, 29) == 0 ) {
            // B, BL
            const int64_t off = sext(bits(ins, 25, 0), 26) * 4;
            const bool link = bit(ins, 31);
            add(bundle, new VanadisArm64Branch(addr, hw_thr, options, link ? "BL" : "B",
                VanadisArm64BranchKind::DIRECT, off, 0, true, {}, link ? std::vector<uint16_t>{ 30 } : std::vector<uint16_t>{}));
            return true;
        }
        if ( bits(ins, 30, 25) == 0b011010 ) {
            // CBZ, CBNZ
            const int64_t off = sext(bits(ins, 23, 5), 19) * 4;
            add(bundle, new VanadisArm64Branch(addr, hw_thr, options, bit(ins, 24) ? "CBNZ" : "CBZ",
                bit(ins, 24) ? VanadisArm64BranchKind::CBNZ : VanadisArm64BranchKind::CBZ, off, 0, bit(ins, 31),
                { rz(bits(ins, 4, 0)) }, {}));
            return true;
        }
        if ( bits(ins, 30, 25) == 0b011011 ) {
            // TBZ, TBNZ
            const int64_t off = sext(bits(ins, 18, 5), 14) * 4;
            const uint32_t b = (bits(ins, 31, 31) << 5) | bits(ins, 23, 19);
            add(bundle, new VanadisArm64Branch(addr, hw_thr, options, bit(ins, 24) ? "TBNZ" : "TBZ",
                bit(ins, 24) ? VanadisArm64BranchKind::TBNZ : VanadisArm64BranchKind::TBZ, off, b, true,
                { rz(bits(ins, 4, 0)) }, {}));
            return true;
        }
        if ( bits(ins, 31, 25) == 0b0101010 ) {
            // B.cond, BC.cond
            if ( bit(ins, 24) ) return false;
            const int64_t off = sext(bits(ins, 23, 5), 19) * 4;
            add(bundle, new VanadisArm64Branch(addr, hw_thr, options, "B.cond",
                VanadisArm64BranchKind::COND, off, bits(ins, 3, 0), true, { A64Reg::NZCV }, {}));
            return true;
        }
        if ( bits(ins, 31, 24) == 0b11010100 ) {
            // exception generation: only SVC #imm is an EL0 call here; the others fault.
            if ( bits(ins, 23, 21) == 0 && bits(ins, 4, 0) == 1 ) {
                add(bundle, new VanadisSysCallInstruction(addr, hw_thr, options));
                return true;
            }
            return false;
        }
        if ( bits(ins, 31, 22) == 0b1101010100 ) return decodeSystem(addr, ins, bundle);
        if ( bits(ins, 31, 25) == 0b1101011 ) {
            // BR, BLR, RET and the PAC forms; PAC is not reported in HWCAP, so the keyed forms fault.
            const uint32_t opc = bits(ins, 24, 21), op2 = bits(ins, 20, 16), op3 = bits(ins, 15, 10), op4 = bits(ins, 4, 0);
            if ( op2 != 0b11111 || op3 != 0 || op4 != 0 ) return false;
            const uint32_t rn = bits(ins, 9, 5);
            if ( opc == 0 || opc == 2 ) {
                add(bundle, new VanadisArm64Branch(addr, hw_thr, options, opc == 0 ? "BR" : "RET",
                    VanadisArm64BranchKind::REG, 0, 0, true, { rz(rn) }, {}));
                return true;
            }
            if ( opc == 1 ) {
                add(bundle, new VanadisArm64Branch(addr, hw_thr, options, "BLR",
                    VanadisArm64BranchKind::REG, 0, 0, true, { rz(rn) }, { 30 }));
                return true;
            }
            return false;
        }
        return false;
    }

    bool decodeSystem(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const bool     L = bit(ins, 21);
        const uint32_t op0 = bits(ins, 20, 19), op1 = bits(ins, 18, 16), crn = bits(ins, 15, 12), crm = bits(ins, 11, 8),
                       op2 = bits(ins, 7, 5), rt = bits(ins, 4, 0);
        auto nop = [&]() {
            add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
            return true;
        };
        if ( !L && op0 == 0 && crn == 0b0010 && rt == 31 ) {
            // HINT space: every hint is a no-op here (NOP, YIELD, WFE, WFI, SEV, BTI, PAC hints).
            return nop();
        }
        if ( !L && op0 == 0 && crn == 0b0011 && rt == 31 && op1 == 3 ) {
            switch ( op2 ) {
            case 2: return nop();    // CLREX: the monitor is cleared by the next store-conditional
            case 6: {                // ISB: nothing younger issues until it retires, which is what a following MRS needs
                auto* isb = op(bundle, addr, "ISB", INST_INT_ARITH, [](VanadisArm64Inst&, VanadisRegisterFile*) {}, {}, {});
                isb->markSerializing();
                return true;
            }
            case 4:
            case 5:
            case 1:
            {   // DSB, DMB
                VanadisFenceType f = VANADIS_LOAD_STORE_FENCE;
                const uint32_t domain_type = crm & 3;
                if ( domain_type == 1 ) f = VANADIS_LOAD_FENCE;
                else if ( domain_type == 2 ) f = VANADIS_STORE_FENCE;
                add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, f));
                return true;
            }
            default: return false;
            }
        }
        if ( !L && op0 == 0 && crn == 0b0100 && rt == 31 ) {
            // MSR (immediate) to a PSTATE field: CFINV, XAFLAG, AXFLAG are in this space.
            if ( op1 == 0 && crm == 0 ) {
                if ( op2 == 0 ) { op(bundle, addr, "CFINV", INST_INT_ARITH, cfinv, { A64Reg::NZCV }, { A64Reg::NZCV }); return true; }
                if ( op2 == 1 ) { op(bundle, addr, "XAFLAG", INST_INT_ARITH, xaflag, { A64Reg::NZCV }, { A64Reg::NZCV }); return true; }
                if ( op2 == 2 ) { op(bundle, addr, "AXFLAG", INST_INT_ARITH, axflag, { A64Reg::NZCV }, { A64Reg::NZCV }); return true; }
            }
            return false;
        }
        if ( op0 == 1 ) {
            // SYS: DC ZVA zeroes a block; the other cache and instruction-cache maintenance is a no-op,
            // because the model has no incoherent instruction cache for user code to maintain.
            if ( !L && op1 == 3 && crn == 7 && crm == 4 && op2 == 1 ) return decodeDcZva(addr, rt, bundle);
            if ( !L ) return nop();
            return false;
        }
        if ( op0 >= 2 ) return decodeSysReg(addr, ins, L, op0, op1, crn, crm, op2, rt, bundle);
        return false;
    }

    bool decodeDcZva(uint64_t addr, uint32_t rt, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        // The block address is Rt aligned down; the zeroing is a run of 8-byte stores of ZR.
        auto* i = op(bundle, addr, "DCZVA.addr", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
            o.setX(rf, 0, o.X(rf, 0) & ~o.imm);
        }, { rz(rt) }, { A64Reg::T0 });
        i->imm = dczid_block_bytes - 1;
        for ( uint32_t off = 0; off < dczid_block_bytes; off += 8 ) {
            add(bundle, new VanadisStoreInstruction(addr, hw_thr, options, A64Reg::T0, off, A64Reg::ZR, 8,
                MEM_TRANSACTION_NONE, STORE_INT_REGISTER));
        }
        return true;
    }

    bool decodeSysReg(uint64_t addr, uint32_t ins, bool L, uint32_t op0, uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2,
        uint32_t rt, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t key = (op0 << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2;
        constexpr auto enc = [](uint32_t o0, uint32_t o1, uint32_t n, uint32_t m, uint32_t o2) {
            return (o0 << 14) | (o1 << 11) | (n << 7) | (m << 3) | o2;
        };
        auto read_const = [&](const char* name, uint64_t v) {
            if ( rt == 31 ) {
                add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
            }
            else {
                op(bundle, addr, name, INST_INT_ARITH, movImm, {}, { static_cast<uint16_t>(rt) })->imm = v;
            }
            return true;
        };
        if ( key == enc(3, 3, 4, 2, 0) ) {   // NZCV
            if ( L ) {
                if ( rt == 31 ) { add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options)); return true; }
                op(bundle, addr, "MRS.NZCV", INST_INT_ARITH, mrsNzcv, { A64Reg::NZCV }, { static_cast<uint16_t>(rt) });
            }
            else {
                op(bundle, addr, "MSR.NZCV", INST_INT_ARITH, msrNzcv, { rz(rt) }, { A64Reg::NZCV });
            }
            return true;
        }
        if ( key == enc(3, 3, 13, 0, 2) ) {   // TPIDR_EL0
            auto* i = op(bundle, addr, "TPIDR", INST_INT_ARITH, movReg, { L ? A64Reg::TPIDR : rz(rt) },
                L ? dz(rt) : std::vector<uint16_t>{ A64Reg::TPIDR });
            i->a = 1;
            i->d = (L && rt == 31) ? 0 : HAS_RD;
            return true;
        }
        if ( L ) {
            switch ( key ) {
            case enc(3, 3, 0, 0, 7): {       // DCZID_EL0: BS is log2(block in 4-byte words)
                return read_const("DCZID", static_cast<uint64_t>(__builtin_ctz(dczid_block_bytes) - 2));
            }
            case enc(3, 3, 0, 0, 1):         // CTR_EL0: IDC and DIC set, 64-byte minimum lines
                return read_const("CTR", 0x3000ull << 16 | (4ull << 16) | 4ull | (1ull << 31) | (1ull << 28) | (1ull << 29));
            case enc(3, 3, 14, 0, 0):        // CNTFRQ_EL0
                return read_const("CNTFRQ", counter_hz);
            case enc(3, 0, 0, 0, 0):         // MIDR_EL1: Arm implementer, an implementation number of zero
                return read_const("MIDR", 0x41000000ull);
            case enc(3, 0, 0, 0, 5):         // MPIDR_EL1
            case enc(3, 3, 13, 0, 3):        // TPIDRRO_EL0
                return read_const("SYSREG0", 0);
            case enc(3, 3, 14, 0, 1):        // CNTPCT_EL0
            case enc(3, 3, 14, 0, 2):        // CNTVCT_EL0
            case enc(3, 3, 14, 0, 5):        // CNTPCTSS_EL0
            case enc(3, 3, 14, 0, 6):        // CNTVCTSS_EL0
            {
                if ( rt == 31 ) { add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options)); return true; }
                auto* i = op(bundle, addr, "CNTVCT", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
                    o.setX(rf, 0, reinterpret_cast<const VanadisARM64Decoder*>(o.imm)->counterValue());
                }, {}, { static_cast<uint16_t>(rt) });
                i->imm = reinterpret_cast<uint64_t>(this);
                return true;
            }
            default: break;
            }
        }
        return decodeFpSysReg(addr, L, key, rt, bundle);
    }

    // ---------------------------------------------------------------- loads and stores
    // One integer or FP load through the LSQ: base register + offset.
    void loadInt(VanadisInstructionBundle* bundle, uint64_t addr, uint16_t base, int64_t off, uint16_t target, uint32_t bytes,
        bool sign, VanadisMemoryTransaction type = MEM_TRANSACTION_NONE)
    {
        add(bundle, new VanadisLoadInstruction(addr, hw_thr, options, base, off, target, bytes, sign, type, LOAD_INT_REGISTER));
    }
    void loadFp(VanadisInstructionBundle* bundle, uint64_t addr, uint16_t base, int64_t off, uint16_t target, uint32_t bytes)
    {
        add(bundle, new VanadisLoadInstruction(addr, hw_thr, options, base, off, target, bytes, false, MEM_TRANSACTION_NONE, LOAD_FP_REGISTER));
    }
    void storeInt(VanadisInstructionBundle* bundle, uint64_t addr, uint16_t base, int64_t off, uint16_t value, uint32_t bytes)
    {
        add(bundle, new VanadisStoreInstruction(addr, hw_thr, options, base, off, value, bytes, MEM_TRANSACTION_NONE, STORE_INT_REGISTER));
    }
    void storeFp(VanadisInstructionBundle* bundle, uint64_t addr, uint16_t base, int64_t off, uint16_t value, uint32_t bytes)
    {
        add(bundle, new VanadisStoreInstruction(addr, hw_thr, options, base, off, value, bytes, MEM_TRANSACTION_NONE, STORE_FP_REGISTER));
    }
    void addImmTo(VanadisInstructionBundle* bundle, uint64_t addr, uint16_t dst, uint16_t src, int64_t imm)
    {
        op(bundle, addr, "ADDIMM", INST_INT_ARITH, A64Sem::addImm, { src }, { dst })->imm = static_cast<uint64_t>(imm);
    }

    // One load or store of Rt at base + off. size is log2(bytes); opc is the load/store opc field.
    // vec selects the SIMD&FP register file. Returns false for an unallocated combination.
    bool singleAccess(VanadisInstructionBundle* bundle, uint64_t addr, bool vec, uint32_t size, uint32_t opc, uint16_t base,
        int64_t off, uint32_t rt)
    {
        using namespace A64Sem;
        if ( vec ) {
            const uint32_t bytes_log2 = (opc & 2) ? (size == 0 ? 4 : 99) : size;
            if ( bytes_log2 > 4 ) return false;
            if ( opc & 1 ) loadFp(bundle, addr, base, off, rt, 1u << bytes_log2);
            else storeFp(bundle, addr, base, off, rt, 1u << bytes_log2);
            return true;
        }
        const uint32_t bytes = 1u << size;
        if ( opc == 0 ) {
            storeInt(bundle, addr, base, off, rz(rt), bytes);
            return true;
        }
        const uint16_t target = rt == 31 ? A64Reg::T1 : static_cast<uint16_t>(rt);
        if ( opc == 1 ) {
            loadInt(bundle, addr, base, off, target, bytes, false);
            return true;
        }
        if ( opc == 2 ) {
            // PRFM at size 3 is a no-op; LDRSB, LDRSH, LDRSW to an X register otherwise.
            if ( size == 3 ) {
                add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
                return true;
            }
            loadInt(bundle, addr, base, off, target, bytes, true);
            return true;
        }
        // opc 3: sign-extending load to a W register, sizes 0 and 1
        if ( size >= 2 ) return false;
        loadInt(bundle, addr, base, off, target, bytes, true);
        if ( rt != 31 ) {
            auto* i = op(bundle, addr, "ZEXT32", INST_INT_ARITH, movReg, { target }, { target });
            i->a = 0;
            i->d = HAS_RD;
        }
        return true;
    }

    bool decodeLoadStore(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t op0 = bits(ins, 31, 28), op1 = bit(ins, 26), op2 = bits(ins, 24, 23), op3 = bits(ins, 21, 16),
                       op4 = bits(ins, 11, 10);
        const uint32_t rt = bits(ins, 4, 0), rn = bits(ins, 9, 5);
        const uint32_t size = bits(ins, 31, 30);
        const bool     vec = op1;

        // The top-level classes of the loads and stores (op0 = bits 31..28, op1 = bit 26, op2 = bits 24..23,
        // op3 = bits 21..16, op4 = bits 11..10); lo is op0's bits 29..28.
        const uint32_t lo = bits(ins, 29, 28);
        if ( (op0 & 0b1011) == 0 && vec ) return decodeSimdStructure(addr, ins, bundle);
        if ( lo == 0b00 ) {
            if ( vec ) return false;
            if ( (op2 & 2) == 0 ) return decodeExclusiveOrdered(addr, ins, bundle);
            return false;   // RCpc3 (LDIAPP, STILP, post-indexed LDAPR) is not implemented
        }
        if ( lo == 0b01 && (op2 & 2) ) {
            if ( op4 == 0b01 ) return false;   // memory copy and set (MOPS) are not reported in HWCAP2
            if ( !vec && !(op3 & 0x20) && op4 == 0b00 ) return decodeRcpcUnscaled(addr, ins, bundle);
            return false;
        }
        if ( lo == 0b01 ) {
            // load register, literal
            const int64_t off = sext(bits(ins, 23, 5), 19) * 4;
            const uint32_t opc = bits(ins, 31, 30);
            const uint64_t target = addr + off;
            if ( vec ) {
                if ( opc == 3 ) return false;
                op(bundle, addr, "LITADDR", INST_INT_ARITH, movImm, {}, { A64Reg::T0 })->imm = target;
                loadFp(bundle, addr, A64Reg::T0, 0, rt, 4u << opc);
                return true;
            }
            if ( opc == 3 ) {   // PRFM literal
                add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
                return true;
            }
            op(bundle, addr, "LITADDR", INST_INT_ARITH, movImm, {}, { A64Reg::T0 })->imm = target;
            const uint16_t t = rt == 31 ? A64Reg::T1 : static_cast<uint16_t>(rt);
            if ( opc == 2 ) loadInt(bundle, addr, A64Reg::T0, 0, t, 4, true);
            else loadInt(bundle, addr, A64Reg::T0, 0, t, opc == 1 ? 8 : 4, false);
            return true;
        }
        if ( lo == 0b10 ) return decodeLoadStorePair(addr, ins, bundle);
        if ( lo == 0b11 ) {
            const bool is_unsigned_imm = bit(ins, 24);
            const uint32_t opc = bits(ins, 23, 22);
            if ( is_unsigned_imm ) {
                const uint32_t scale = vec ? (((opc & 2) && size == 0) ? 4 : size) : size;
                const int64_t off = static_cast<int64_t>(bits(ins, 21, 10)) << scale;
                return singleAccess(bundle, addr, vec, size, opc, rs(rn), off, rt);
            }
            if ( bit(ins, 21) ) {
                if ( op4 == 0b00 ) return decodeAtomicLse(addr, ins, bundle);
                if ( op4 == 0b10 ) {
                    // register offset
                    const uint32_t option = bits(ins, 15, 13);
                    if ( !(option & 2) ) return false;
                    const uint32_t scale = vec ? (((opc & 2) && size == 0) ? 4 : size) : size;
                    const uint32_t shift = bit(ins, 12) ? scale : 0;
                    auto* i = op(bundle, addr, "ADDRREG", INST_INT_ARITH, addrRegOffset, { rs(rn), rz(bits(ins, 20, 16)) }, { A64Reg::T0 });
                    i->c = (option << 8) | shift;
                    return singleAccess(bundle, addr, vec, size, opc, A64Reg::T0, 0, rt);
                }
                return false;
            }
            // immediate: unscaled, post-indexed, unprivileged, pre-indexed
            const int64_t imm9 = sext(bits(ins, 20, 12), 9);
            switch ( op4 ) {
            case 0b00:   // LDUR, STUR
            case 0b10:   // LDTR, STTR behave as LDR, STR at EL0
                return singleAccess(bundle, addr, vec, size, opc, rs(rn), imm9, rt);
            case 0b01:   // post-indexed
                if ( !singleAccess(bundle, addr, vec, size, opc, rs(rn), 0, rt) ) return false;
                addImmTo(bundle, addr, rs(rn), rs(rn), imm9);
                return true;
            default:     // pre-indexed
                addImmTo(bundle, addr, rs(rn), rs(rn), imm9);
                return singleAccess(bundle, addr, vec, size, opc, rs(rn), 0, rt);
            }
        }
        (void)op3;
        return false;
    }

    // LDAPUR, LDAPURS*, STLUR (FEAT_LRCPC2): unscaled immediate, release-consistent processor-consistent order.
    bool decodeRcpcUnscaled(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t size = bits(ins, 31, 30), opc = bits(ins, 23, 22), rn = bits(ins, 9, 5), rt = bits(ins, 4, 0);
        const int64_t  off = sext(bits(ins, 20, 12), 9);
        const uint32_t bytes = 1u << size;
        if ( opc == 0 ) {
            add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
            storeInt(bundle, addr, rs(rn), off, rz(rt), bytes);
            return true;
        }
        if ( (opc == 2 && size == 3) || (opc == 3 && size >= 2) ) return false;
        const uint16_t target = rt == 31 ? A64Reg::T1 : static_cast<uint16_t>(rt);
        loadInt(bundle, addr, rs(rn), off, target, bytes, opc >= 2);
        if ( opc == 3 && rt != 31 ) {
            auto* i = op(bundle, addr, "ZEXT32", INST_INT_ARITH, movReg, { target }, { target });
            i->a = 0;
            i->d = HAS_RD;
        }
        add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
        return true;
    }

    bool decodeLoadStorePair(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t opc = bits(ins, 31, 30), mode = bits(ins, 24, 23);
        const bool     vec = bit(ins, 26), load = bit(ins, 22);
        const uint32_t rt = bits(ins, 4, 0), rt2 = bits(ins, 14, 10), rn = bits(ins, 9, 5);
        uint32_t scale;
        bool     sign = false;
        if ( vec ) {
            if ( opc == 3 ) return false;
            scale = 2 + opc;
        }
        else {
            if ( opc == 3 ) return false;
            if ( opc == 1 && !load ) return false;
            sign  = opc == 1;
            scale = (opc == 2) ? 3 : 2;
        }
        const int64_t off = sext(bits(ins, 21, 15), 7) * (1ll << scale);
        const uint32_t bytes = 1u << scale;
        const uint16_t base = rs(rn);
        const bool writeback = mode == 1 || mode == 3;
        const int64_t access_off = (mode == 1) ? 0 : off;
        // A load that overwrites the base register first would change the second address, so the base
        // is copied when Rt is the base.
        uint16_t use_base = base;
        if ( load && !vec && rt == rn && !writeback ) {
            auto* m = op(bundle, addr, "COPYBASE", INST_INT_ARITH, movReg, { base }, { A64Reg::T0 });
            m->a = 1;
            m->d = HAS_RD;
            use_base = A64Reg::T0;
        }
        if ( vec ) {
            if ( load ) {
                loadFp(bundle, addr, base, access_off, rt, bytes);
                loadFp(bundle, addr, base, access_off + bytes, rt2, bytes);
            }
            else {
                storeFp(bundle, addr, base, access_off, rt, bytes);
                storeFp(bundle, addr, base, access_off + bytes, rt2, bytes);
            }
        }
        else if ( load ) {
            loadInt(bundle, addr, use_base, access_off, rt == 31 ? A64Reg::T1 : static_cast<uint16_t>(rt), bytes, sign);
            loadInt(bundle, addr, use_base, access_off + bytes, rt2 == 31 ? A64Reg::T1 : static_cast<uint16_t>(rt2), bytes, sign);
        }
        else {
            storeInt(bundle, addr, base, access_off, rz(rt), bytes);
            storeInt(bundle, addr, base, access_off + bytes, rz(rt2), bytes);
        }
        if ( writeback ) addImmTo(bundle, addr, base, base, off);
        return true;
    }

    bool decodeExclusiveOrdered(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t size = bits(ins, 31, 30), rs_ = bits(ins, 20, 16), rt2 = bits(ins, 14, 10), rn = bits(ins, 9, 5), rt = bits(ins, 4, 0);
        const bool o2 = bit(ins, 23), L = bit(ins, 22), o1 = bit(ins, 21), o0 = bit(ins, 15);
        const uint32_t bytes = 1u << size;
        const uint16_t base = rs(rn);
        const uint16_t target = rt == 31 ? A64Reg::T1 : static_cast<uint16_t>(rt);
        if ( bits(ins, 29, 24) != 0b001000 ) return false;
        if ( !o2 ) {
            if ( o1 ) {
                // LDXP, LDAXP, STXP, STLXP (size 2 or 3), and CASP is o1 with size bit 31 clear
                if ( size < 2 ) return decodeCaspOrFault(addr, ins, bundle);
                return exclusivePair(addr, ins, bundle);
            }
            if ( L ) {   // LDXR, LDAXR
                loadInt(bundle, addr, base, 0, target, bytes, false, MEM_TRANSACTION_LLSC_LOAD);
                if ( o0 ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
                return true;
            }
            // STXR, STLXR
            if ( o0 ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
            add(bundle, new VanadisStoreConditionalInstruction(addr, hw_thr, options, base, 0, rz(rt),
                rs_ == 31 ? A64Reg::T2 : static_cast<uint16_t>(rs_), bytes, STORE_INT_REGISTER, 0, 1));
            return true;
        }
        if ( o1 ) {
            // CAS, CASA, CASL, CASAL (byte, halfword, word, doubleword)
            return decodeCas(addr, ins, bundle);
        }
        if ( L ) {   // LDAR, LDLAR
            loadInt(bundle, addr, base, 0, target, bytes, false);
            add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
            return true;
        }
        // STLR, STLLR
        add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
        storeInt(bundle, addr, base, 0, rz(rt), bytes);
        return true;
    }

    // LDXP and STXP go through a vector temporary, because one LL or SC micro-op has one data register.
    bool exclusivePair(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t size = bits(ins, 31, 30), rs_ = bits(ins, 20, 16), rt2 = bits(ins, 14, 10), rn = bits(ins, 9, 5), rt = bits(ins, 4, 0);
        const bool     L = bit(ins, 22), o0 = bit(ins, 15);
        const uint32_t elem = 1u << size;
        if ( L ) {
            add(bundle, new VanadisLoadInstruction(addr, hw_thr, options, rs(rn), 0, A64Reg::VT0, elem * 2, false,
                MEM_TRANSACTION_LLSC_LOAD, LOAD_FP_REGISTER));
            std::vector<uint16_t> out = dz(rt);
            for ( auto r : dz(rt2) ) out.push_back(r);
            auto* i = op(bundle, addr, "SPLITPAIR", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
                const uint8_t* v = o.Vin(rf, 0);
                uint64_t lo = 0, hi = 0;
                std::memcpy(&lo, v, o.c);
                std::memcpy(&hi, v + o.c, o.c);
                uint32_t pos = 0;
                if ( o.a & 1 ) o.setX(rf, pos++, lo);
                if ( o.a & 2 ) o.setX(rf, pos++, hi);
            }, {}, out, { A64Reg::VT0 });
            i->c = elem;
            i->a = (rt != 31 ? 1 : 0) | (rt2 != 31 ? 2 : 0);
            if ( o0 ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
            return true;
        }
        if ( o0 ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
        auto* i = op(bundle, addr, "JOINPAIR", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
            const uint64_t lo = o.X(rf, 0), hi = o.X(rf, 1);
            uint8_t* v = o.Vout(rf, 0);
            std::memcpy(v, &lo, o.c);
            std::memcpy(v + o.c, &hi, o.c);
        }, { rz(rt), rz(rt2) }, {}, {}, { A64Reg::VT0 });
        i->c = elem;
        add(bundle, new VanadisStoreConditionalInstruction(addr, hw_thr, options, rs(rn), 0, A64Reg::VT0,
            rs_ == 31 ? A64Reg::T2 : static_cast<uint16_t>(rs_), elem * 2, STORE_FP_REGISTER, 0, 1));
        return true;
    }

    // The tail of a read-modify-write: store T2 conditionally, write the loaded value to Rd only when the store
    // succeeded, then replay the instruction if it failed. The branch is the last micro-op of the bundle: a mispredicted
    // branch squashes every younger micro-op, including the ones of its own bundle, so nothing may follow it. Rd keeps
    // its value on a failure because the replay reads it again.
    void rmwStoreAndRetry(VanadisInstructionBundle* bundle, uint64_t addr, uint16_t base, uint32_t bytes, uint32_t rd, bool acquire)
    {
        using namespace A64Sem;
        add(bundle, new VanadisStoreConditionalInstruction(addr, hw_thr, options, base, 0, A64Reg::T2, A64Reg::T3, bytes,
            STORE_INT_REGISTER, 0, 1));
        if ( rd != 31 ) {
            op(bundle, addr, "RMWRES", INST_INT_ARITH, rmwCommit, { A64Reg::T3, A64Reg::T1, static_cast<uint16_t>(rd) },
                { static_cast<uint16_t>(rd) })->c = bytes;
        }
        if ( acquire ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
        add(bundle, new VanadisArm64Branch(addr, hw_thr, options, "RMWRETRY", VanadisArm64BranchKind::CBNZ, 0, 0, true,
            { A64Reg::T3 }, {}));
    }

    bool decodeCas(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        const uint32_t size = bits(ins, 31, 30), rs_ = bits(ins, 20, 16), rn = bits(ins, 9, 5), rt = bits(ins, 4, 0);
        const bool     A = bit(ins, 22), R = bit(ins, 15);
        const uint32_t bytes = 1u << size;
        if ( R ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
        loadInt(bundle, addr, rs(rn), 0, A64Reg::T1, bytes, false, MEM_TRANSACTION_LLSC_LOAD);
        op(bundle, addr, "CASVAL", INST_INT_ARITH, casValue, { A64Reg::T1, rz(rs_), rz(rt) }, { A64Reg::T2 })->c = bytes;
        rmwStoreAndRetry(bundle, addr, rs(rn), bytes, rs_, A);
        return true;
    }

    // CASP is not in the integer core of this group: 16-byte compare through a vector temporary.
    bool decodeCaspOrFault(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        const uint32_t rs_ = bits(ins, 20, 16), rn = bits(ins, 9, 5), rt = bits(ins, 4, 0);
        const bool     sz = bit(ins, 30), A = bit(ins, 22), R = bit(ins, 15);
        if ( bit(ins, 31) || (rs_ & 1) || (rt & 1) || bits(ins, 14, 10) != 31 ) return false;
        const uint32_t elem = sz ? 8 : 4;
        if ( R ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
        add(bundle, new VanadisLoadInstruction(addr, hw_thr, options, rs(rn), 0, A64Reg::VT0, elem * 2, false,
            MEM_TRANSACTION_LLSC_LOAD, LOAD_FP_REGISTER));
        // New value of the pair in VT1: Rt,Rt+1 when the memory equals Rs,Rs+1, else the memory unchanged.
        auto* i = op(bundle, addr, "CASPVAL", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
            const uint8_t* mem = o.Vin(rf, 0);
            uint64_t lo = 0, hi = 0;
            std::memcpy(&lo, mem, o.c);
            std::memcpy(&hi, mem + o.c, o.c);
            const uint64_t mask = o.c == 8 ? ~0ull : 0xFFFFFFFFull;
            const bool same = lo == (o.X(rf, 0) & mask) && hi == (o.X(rf, 1) & mask);
            uint8_t* v = o.Vout(rf, 0);
            if ( same ) {
                const uint64_t nlo = o.X(rf, 2), nhi = o.X(rf, 3);
                std::memcpy(v, &nlo, o.c);
                std::memcpy(v + o.c, &nhi, o.c);
            }
            else {
                std::memcpy(v, mem, o.c * 2);
            }
        }, { rz(rs_), rz(rs_ + 1), rz(rt), rz(rt + 1) }, {}, { A64Reg::VT0 }, { A64Reg::VT1 });
        i->c = elem;
        add(bundle, new VanadisStoreConditionalInstruction(addr, hw_thr, options, rs(rn), 0, A64Reg::VT1, A64Reg::T3,
            elem * 2, STORE_FP_REGISTER, 0, 1));
        // The pair goes to Rs, Rs+1 only when the store succeeded; the branch is the last micro-op (rmwStoreAndRetry).
        std::vector<uint16_t> out = dz(rs_);
        for ( auto r : dz(rs_ + 1) ) out.push_back(r);
        auto* j = op(bundle, addr, "RMWRES2", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
            const uint8_t* v = o.Vin(rf, 0);
            uint64_t lo = 0, hi = 0;
            std::memcpy(&lo, v, o.c);
            std::memcpy(&hi, v + o.c, o.c);
            if ( o.X(rf, 0) != 0 ) {
                lo = o.X(rf, 1);
                hi = o.X(rf, 2);
            }
            uint32_t pos = 0;
            if ( o.a & 1 ) o.setX(rf, pos++, lo);
            if ( o.a & 2 ) o.setX(rf, pos++, hi);
        }, { A64Reg::T3, rz(rs_), rz(rs_ + 1) }, out, { A64Reg::VT0 });
        j->c = elem;
        j->a = (rs_ != 31 ? 1 : 0) | (rs_ + 1 != 31 ? 2 : 0);
        if ( A ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
        add(bundle, new VanadisArm64Branch(addr, hw_thr, options, "RMWRETRY", VanadisArm64BranchKind::CBNZ, 0, 0, true,
            { A64Reg::T3 }, {}));
        return true;
    }

    // LDADD, LDCLR, LDEOR, LDSET, LDSMAX, LDSMIN, LDUMAX, LDUMIN, SWP, and LDAPR, through LL/SC.
    bool decodeAtomicLse(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        if ( bit(ins, 26) ) return false;
        const uint32_t size = bits(ins, 31, 30), rs_ = bits(ins, 20, 16), rn = bits(ins, 9, 5), rt = bits(ins, 4, 0),
                       opc = bits(ins, 14, 12);
        const bool A = bit(ins, 23), R = bit(ins, 22), o3 = bit(ins, 15);
        const uint32_t bytes = 1u << size;
        uint32_t kind;
        if ( o3 ) {
            if ( opc == 0 ) {
                kind = 8;    // SWP
            }
            else if ( opc == 4 && A && !R && rs_ == 31 ) {
                // LDAPR, LDAPRB, LDAPRH
                loadInt(bundle, addr, rs(rn), 0, rt == 31 ? A64Reg::T1 : static_cast<uint16_t>(rt), bytes, false);
                add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
                return true;
            }
            else {
                return false;
            }
        }
        else {
            kind = opc;
        }
        if ( R ) add(bundle, new VanadisFenceInstruction(addr, hw_thr, options, VANADIS_LOAD_STORE_FENCE));
        loadInt(bundle, addr, rs(rn), 0, A64Reg::T1, bytes, false, MEM_TRANSACTION_LLSC_LOAD);
        auto* i = op(bundle, addr, "ATOMICOP", INST_INT_ARITH, atomicOp, { A64Reg::T1, rz(rs_) }, { A64Reg::T2 });
        i->b = kind;
        i->c = bytes;
        rmwStoreAndRetry(bundle, addr, rs(rn), bytes, rt, A);
        return true;
    }

    // ---------------------------------------------------------------- later stages
    // The floating-point and vector groups; their definitions are in decoder/arm64/.
    bool decodeFpSimd(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
    {
        if ( (bits(ins, 31, 28) & 0b0101) == 0b0001 ) return decodeFpScalar(addr, ins, bundle);
        return decodeAdvSimd(addr, ins, bundle);
    }
    bool decodeAdvSimd(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSimdThreeSame(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdThreeSameFp16(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdThreeExt(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdThreeDiff(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdMisc(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdMiscFp16(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdShiftImm(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdIndexed(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdCopy(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle, bool scalar);
    bool decodeSimdModifiedImm(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSimdPermute(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSimdTable(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSimdExt(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSimdAcross(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSimdScalarPair(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeCryptoAes(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeCryptoSha3Reg(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeCryptoSha2Reg(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeCryptoHigh(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSve(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSveInt(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSvePermute(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSveCompareAndPred(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSveFp(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSveFpMiscInt(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSveMem(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSve2(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSve2Logic(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSve2MulUnpred(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    uint32_t sveChunkBytes() const;
    void sveContigLoad(uint64_t addr, VanadisInstructionBundle* bundle, uint16_t pg, uint16_t zt, uint32_t nreg, struct A64SveDtype dt,
        uint16_t base, uint16_t index_reg, uint32_t shift, int64_t imm, uint32_t ff);
    void sveContigStore(uint64_t addr, VanadisInstructionBundle* bundle, uint16_t pg, uint16_t zt, uint32_t nreg, uint32_t log2_ebytes,
        uint32_t mbytes, uint16_t base, uint16_t index_reg, uint32_t shift, int64_t imm);
    void sveGather(uint64_t addr, VanadisInstructionBundle* bundle, uint16_t pg, uint16_t zt, uint16_t zm, uint16_t base, uint32_t mode,
        uint32_t shift, int64_t imm, struct A64SveDtype dt, bool ff);
    void sveScatter(uint64_t addr, VanadisInstructionBundle* bundle, uint16_t pg, uint16_t zt, uint16_t zm, uint16_t base, uint32_t mode,
        uint32_t shift, int64_t imm, uint32_t log2_ebytes, uint32_t mbytes);
    bool decodeFpScalar(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    bool decodeSimdStructure(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle);
    // A micro-op that accumulates FPSR bits at retirement.
    VanadisArm64Inst* fpOp(VanadisInstructionBundle* bundle, uint64_t addr, const char* name, VanadisFunctionalUnitType unit,
        VanadisArm64Inst::Semantics fn, const std::vector<uint16_t>& in, const std::vector<uint16_t>& out,
        const std::vector<uint16_t>& fin, const std::vector<uint16_t>& fout);
    // FPCR is a renamed integer-class register; FPSR is the thread's accumulated state, read and written at the
    // front of the reorder buffer so every earlier instruction has added its flags.
    VanadisArm64FpState fp_state_;

    bool decodeFpSysReg(uint64_t addr, bool L, uint32_t key, uint32_t rt, VanadisInstructionBundle* bundle)
    {
        using namespace A64Sem;
        constexpr uint32_t FPCR_KEY = (3u << 14) | (3u << 11) | (4u << 7) | (4u << 3) | 0u;
        constexpr uint32_t FPSR_KEY = (3u << 14) | (3u << 11) | (4u << 7) | (4u << 3) | 1u;
        // Writable FPCR bits: AHP, DN, FZ, RMode, FZ16 (and the trap enables, which this model never takes).
        constexpr uint64_t FPCR_MASK = 0x07C89F00ull;
        constexpr uint64_t FPSR_MASK = 0xF800009Full;
        if ( key == FPCR_KEY ) {
            if ( L ) {
                if ( rt == 31 ) { add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options)); return true; }
                auto* i = op(bundle, addr, "MRS.FPCR", INST_INT_ARITH, movReg, { A64Reg::FPCR }, { static_cast<uint16_t>(rt) });
                i->a = 1; i->d = HAS_RD;
            }
            else {
                auto* i = op(bundle, addr, "MSR.FPCR", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
                    o.setX(rf, 0, o.X(rf, 0) & o.imm);
                }, { rz(rt) }, { A64Reg::FPCR });
                i->imm = FPCR_MASK;
            }
            return true;
        }
        if ( key == FPSR_KEY ) {
            VanadisArm64Inst* i;
            if ( L ) {
                i = op(bundle, addr, "MRS.FPSR", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
                    if ( o.countISAIntRegOut() ) o.setX(rf, 0, o.fp_state->fpsr);
                }, {}, dz(rt));
            }
            else {
                i = op(bundle, addr, "MSR.FPSR", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
                    o.fp_state->fpsr = static_cast<uint32_t>(o.X(rf, 0) & o.imm);
                }, { rz(rt) }, {});
                i->imm = FPSR_MASK;
            }
            i->fp_state       = &fp_state_;
            i->rob_front_only = true;
            return true;
        }
        return false;
    }
};

} // namespace Vanadis
} // namespace SST

#include "decoder/arm64/dec_fpscalar.h"
#include "decoder/arm64/dec_simd.h"
#include "decoder/arm64/dec_sve.h"
#include "decoder/arm64/dec_sve2.h"
#include "decoder/arm64/dec_sve_fp.h"
#include "decoder/arm64/dec_sve_mem.h"

#endif
