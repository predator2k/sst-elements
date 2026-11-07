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

#ifndef _H_VANADIS_ARM64_INST
#define _H_VANADIS_ARM64_INST

#include "inst/regfile.h"
#include "inst/vinst.h"
#include "inst/vspeculate.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace SST {
namespace Vanadis {

// Architectural register numbering of the AArch64 decoder.
//
// Integer class: X0 to X30 keep their numbers; SP is 31. XZR has no number of its own in a source
// position: a source that reads XZR names ZR, a register no instruction writes, so it stays zero. A
// destination that writes XZR is left out of the instruction. NZCV is one renamed register whose
// bits 31 to 28 are N, Z, C and V, the layout of MRS NZCV. TPIDR_EL0 is renamed like a GPR. FPCR is a
// renamed register too, and every floating-point micro-op reads it: a write is rare and reads do not
// serialize, so an instruction after MSR FPCR sees the new mode wherever it executes. T0 to T4 are the
// decoder's temporaries.
//
// Floating-point class: its width is the SVE vector length in bytes (at least 16). Z0 to Z31 keep
// their numbers, and V<n> is the low 128 bits of Z<n>. P0 to P15 and FFR are in the same class: a
// third register class would change the constructor of every Vanadis instruction, and the core does
// not model register-file ports, so the class of a predicate does not change the timing.
namespace A64Reg {
constexpr uint16_t SP        = 31;
constexpr uint16_t NZCV      = 32;
constexpr uint16_t TPIDR     = 33;
constexpr uint16_t ZR        = 34;
constexpr uint16_t FPCR      = 35;
constexpr uint16_t T0        = 36;
constexpr uint16_t T1        = 37;
constexpr uint16_t T2        = 38;
constexpr uint16_t T3        = 39;
constexpr uint16_t T4        = 40;
constexpr uint16_t INT_COUNT = 41;

constexpr uint16_t P0        = 32;
constexpr uint16_t FFR       = 48;
constexpr uint16_t VT0       = 49;
constexpr uint16_t VT1       = 50;
constexpr uint16_t VT2       = 51;
constexpr uint16_t VT3       = 52;
constexpr uint16_t FP_COUNT  = 53;

// A number that no register has: the "ignore writes" register of the decoder options.
constexpr uint16_t NO_ZERO_REG = 0xFFFF;
} // namespace A64Reg

// The bits of NZCV in the register that holds it.
namespace A64Flag {
constexpr uint64_t N = 1ull << 31;
constexpr uint64_t Z = 1ull << 30;
constexpr uint64_t C = 1ull << 29;
constexpr uint64_t V = 1ull << 28;
} // namespace A64Flag

// The FPSR cumulative bits of one hardware thread. They change only at retirement (updateFPFlags), so MRS
// FPSR and MSR FPSR run at the front of the reorder buffer.
struct VanadisArm64FpState
{
    uint32_t fpsr = 0;
};

// One AArch64 micro-op. The instruction classes of the other ISAs have one class per operation; A64,
// AdvSIMD and SVE together have about 2,000 encoding groups, so this ISA has one class that holds a
// pointer to its semantic function and a plain payload. Register operands are given by position in
// the instruction's source and destination lists.
class VanadisArm64Inst : public VanadisInstruction
{
public:
    using Semantics = void (*)(VanadisArm64Inst& op, VanadisRegisterFile* rf);

    VanadisArm64Inst(
        const uint64_t addr, const uint32_t hw_thr, const VanadisDecoderOptions* isa_opts, const char* inst_name,
        const VanadisFunctionalUnitType inst_unit, const Semantics inst_fn, const std::vector<uint16_t>& int_in,
        const std::vector<uint16_t>& int_out, const std::vector<uint16_t>& fp_in = {},
        const std::vector<uint16_t>& fp_out = {}) :
        VanadisInstruction(
            addr, hw_thr, isa_opts, int_in.size(), int_out.size(), int_in.size(), int_out.size(), fp_in.size(),
            fp_out.size(), fp_in.size(), fp_out.size()),
        name(inst_name),
        unit(inst_unit),
        fn(inst_fn)
    {
        uint16_t k = 0;
        for ( auto r : int_in ) isa_int_regs_in[k++] = r;
        k = 0;
        for ( auto r : int_out ) isa_int_regs_out[k++] = r;
        k = 0;
        for ( auto r : fp_in ) isa_fp_regs_in[k++] = r;
        k = 0;
        for ( auto r : fp_out ) isa_fp_regs_out[k++] = r;
    }

    VanadisArm64Inst* clone() override { return new VanadisArm64Inst(*this); }

    VanadisFunctionalUnitType getInstFuncType() const override { return unit; }
    // The timing class of the micro-op chooses its functional unit.
    void setUnit(const VanadisFunctionalUnitType u) { unit = u; }

    // An integer divide (dp2 UDIV, SDIV): sources 0 and 1 are the dividend and the divisor, a is the 64-bit form and b
    // the kind. The divider retires divide_bits_per_cycle quotient bits per cycle above its base latency: the base alone
    // when the divisor is a power of two or longer than the dividend, at least one more cycle otherwise.
    uint16_t dynamicLatency(VanadisRegisterFile* rf) const override
    {
        const VanadisTimingRecord* t = getTiming();
        if ( t->dynamic != VanadisTimingDynamic::INTEGER_DIVIDE || countISAIntRegIn() < 2 ) return t->latency;
        uint64_t n = rf->getIntReg<uint64_t>(phys_int_regs_in[0]);
        uint64_t m = rf->getIntReg<uint64_t>(phys_int_regs_in[1]);
        const bool is64 = a != 0;
        const bool is_signed = b == divide_signed_kind;
        if ( ! is64 ) {
            n &= 0xFFFFFFFFull;
            m &= 0xFFFFFFFFull;
        }
        if ( is_signed ) {
            const int64_t sn = is64 ? static_cast<int64_t>(n) : static_cast<int64_t>(static_cast<int32_t>(n));
            const int64_t sm = is64 ? static_cast<int64_t>(m) : static_cast<int64_t>(static_cast<int32_t>(m));
            n = sn < 0 ? 0 - static_cast<uint64_t>(sn) : static_cast<uint64_t>(sn);
            m = sm < 0 ? 0 - static_cast<uint64_t>(sm) : static_cast<uint64_t>(sm);
        }
        if ( m == 0 || ( m & (m - 1) ) == 0 || n == 0 ) return t->latency;
        const int bits_n = 64 - __builtin_clzll(n), bits_m = 64 - __builtin_clzll(m);
        if ( bits_n < bits_m ) return t->latency;
        const int extra = (bits_n - bits_m) / t->divide_bits_per_cycle;
        return static_cast<uint16_t>(t->latency + (extra < 1 ? 1 : extra));
    }
    // The payload value b of a signed divide; set by the decoder.
    uint32_t divide_signed_kind = 0xFFFFFFFF;
    const char*               getInstCode() const override { return name; }

    void printToBuffer(char* buffer, size_t buffer_size) override
    {
        snprintf(buffer, buffer_size, "%s imm=0x%" PRIx64 " a=%u b=%u c=%u d=%u", name, imm, a, b, c, d);
    }

    void scalarExecute(SST::Output* output, VanadisRegisterFile* regFile) override
    {
        if ( rob_front_only && !checkFrontOfROB() ) return;
        fn(*this, regFile);
        markExecuted();
    }

    bool updatesFPFlags() const override { return fp_flags != 0; }
    void updateFPFlags() override { fp_state->fpsr |= fp_flags; }

    // Integer source and destination by position.
    uint64_t X(VanadisRegisterFile* rf, uint16_t pos) const { return rf->getIntReg<uint64_t>(phys_int_regs_in[pos]); }
    void     setX(VanadisRegisterFile* rf, uint16_t pos, uint64_t v) { rf->setIntReg<uint64_t>(phys_int_regs_out[pos], v); }

    // Vector-register bytes by position.
    const uint8_t* Vin(VanadisRegisterFile* rf, uint16_t pos) const
    {
        return rf->fpRegBytes(phys_fp_regs_in[pos]);
    }
    uint8_t* Vout(VanadisRegisterFile* rf, uint16_t pos)
    {
        return rf->fpRegBytes(phys_fp_regs_out[pos]);
    }

    // Payload of the semantic function; its meaning belongs to that function.
    uint64_t imm  = 0;
    uint64_t imm2 = 0;
    uint32_t a    = 0;
    uint32_t b    = 0;
    uint32_t c    = 0;
    uint32_t d    = 0;

    // FPSR bits this instruction accumulates at retirement, the thread's FPSR, and whether the micro-op
    // must wait for the front of the reorder buffer.
    uint32_t             fp_flags       = 0;
    VanadisArm64FpState* fp_state       = nullptr;
    bool                 rob_front_only = false;

protected:
    const char*               name;
    VanadisFunctionalUnitType unit;
    Semantics                 fn;
};

// Branch kinds of one AArch64 branch micro-op.
enum class VanadisArm64BranchKind {
    DIRECT,  // B, BL: target is the instruction address plus imm
    REG,     // BR, BLR, RET: target is source 0
    COND,    // B.cond: source 0 is NZCV, cond in the payload
    CBZ,     // source 0, width by is64
    CBNZ,
    TBZ,     // source 0, bit number in the payload
    TBNZ
};

// A condition of the A64 condition-code table on an NZCV register value.
inline bool
a64ConditionHolds(uint64_t nzcv, uint32_t cond)
{
    const bool n = nzcv & A64Flag::N, z = nzcv & A64Flag::Z, c = nzcv & A64Flag::C, v = nzcv & A64Flag::V;
    bool       r = false;
    switch ( (cond >> 1) & 7 ) {
    case 0: r = z; break;
    case 1: r = c; break;
    case 2: r = n; break;
    case 3: r = v; break;
    case 4: r = c && !z; break;
    case 5: r = n == v; break;
    case 6: r = (n == v) && !z; break;
    case 7: r = true; break;
    }
    return ((cond & 1) && cond != 15) ? !r : r;
}

class VanadisArm64Branch : public virtual VanadisSpeculatedInstruction
{
public:
    VanadisArm64Branch(
        const uint64_t addr, const uint32_t hw_thr, const VanadisDecoderOptions* isa_opts, const char* inst_name,
        const VanadisArm64BranchKind inst_kind, const int64_t offset, const uint32_t cond_or_bit, const bool is_64,
        const std::vector<uint16_t>& int_in, const std::vector<uint16_t>& int_out) :
        VanadisInstruction(addr, hw_thr, isa_opts, int_in.size(), int_out.size(), int_in.size(), int_out.size(), 0, 0, 0, 0),
        VanadisSpeculatedInstruction(
            addr, hw_thr, isa_opts, 4, int_in.size(), int_out.size(), int_in.size(), int_out.size(), 0, 0, 0, 0,
            VANADIS_NO_DELAY_SLOT),
        name(inst_name),
        kind(inst_kind),
        imm(offset),
        selector(cond_or_bit),
        is64(is_64)
    {
        uint16_t k = 0;
        for ( auto r : int_in ) isa_int_regs_in[k++] = r;
        k = 0;
        for ( auto r : int_out ) isa_int_regs_out[k++] = r;
    }

    VanadisArm64Branch* clone() override { return new VanadisArm64Branch(*this); }

    VanadisBranchClass getBranchClass() const override
    {
        if ( VanadisArm64BranchKind::DIRECT == kind || VanadisArm64BranchKind::REG == kind ) {
            if ( count_isa_int_reg_out > 0 ) return VanadisBranchClass::CALL;
            return 0 == strcmp(name, "RET") ? VanadisBranchClass::RETURN : VanadisBranchClass::OTHER;
        }
        return VanadisBranchClass::CONDITIONAL;
    }

    const char*         getInstCode() const override { return name; }

    void printToBuffer(char* buffer, size_t buffer_size) override
    {
        snprintf(buffer, buffer_size, "%s offset=%" PRId64 " sel=%u", name, imm, selector);
    }

    void scalarExecute(SST::Output* output, VanadisRegisterFile* regFile) override
    {
        const uint64_t pc     = getInstructionAddress();
        bool           taken  = true;
        uint64_t       target = pc + imm;
        switch ( kind ) {
        case VanadisArm64BranchKind::DIRECT: break;
        case VanadisArm64BranchKind::REG: target = regFile->getIntReg<uint64_t>(phys_int_regs_in[0]); break;
        case VanadisArm64BranchKind::COND:
            taken = a64ConditionHolds(regFile->getIntReg<uint64_t>(phys_int_regs_in[0]), selector);
            break;
        case VanadisArm64BranchKind::CBZ:
        case VanadisArm64BranchKind::CBNZ:
        {
            uint64_t v = regFile->getIntReg<uint64_t>(phys_int_regs_in[0]);
            if ( !is64 ) v &= 0xFFFFFFFFull;
            taken = (v == 0) == (kind == VanadisArm64BranchKind::CBZ);
        } break;
        case VanadisArm64BranchKind::TBZ:
        case VanadisArm64BranchKind::TBNZ:
        {
            const bool set = (regFile->getIntReg<uint64_t>(phys_int_regs_in[0]) >> selector) & 1;
            taken          = set == (kind == VanadisArm64BranchKind::TBNZ);
        } break;
        }
        if ( countISAIntRegOut() > 0 ) regFile->setIntReg<uint64_t>(phys_int_regs_out[0], pc + 4);
        takenAddress = taken ? target : calculateStandardNotTakenAddress();
        markExecuted();
    }

protected:
    const char*            name;
    VanadisArm64BranchKind kind;
    int64_t                imm;
    uint32_t               selector;
    bool                   is64;
};

} // namespace Vanadis
} // namespace SST

#endif
