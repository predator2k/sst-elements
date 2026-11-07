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

// Vector memory micro-ops of SVE. A resolver computes the address and the width at execution from the registers, so
// inactive elements make no access. A load writes the bytes it read into the temporary at a register offset, and a
// later micro-op moves them to the architectural registers. Width 0 means no access.

#ifndef _H_VANADIS_ARM64_MEM
#define _H_VANADIS_ARM64_MEM

#include "inst/arm64/sem_sve.h"
#include "inst/vload.h"
#include "inst/vstore.h"

#include <vector>

namespace SST {
namespace Vanadis {

// The fields a resolver reads. Their meaning is in the resolver.
struct VanadisArm64MemPayload
{
    int64_t  imm = 0;           // constant added to the base
    uint32_t log2_ebytes = 0;   // element size, the granule of the predicate
    uint32_t mbytes = 1;        // bytes of one memory element
    uint32_t shift = 0;         // left shift of the index
    uint32_t mode = 0;          // the kind of index
    uint32_t stride = 1;        // memory bytes per structure
    uint32_t first = 0;         // first image byte of the chunk, or the element number
    uint32_t length = 0;        // image bytes of the chunk
    uint32_t elements = 0;      // elements in the operation
    bool     has_index = false;
    uint32_t page_part = 0;     // first-fault load: 0 all, 1 up to the end of the page, 2 the rest
    bool     speculative = false;   // non-fault or first-fault
    bool     non_fault = false;
};

class VanadisArm64VecLoad : public VanadisLoadInstruction
{
public:
    using Resolver = void (*)(VanadisArm64VecLoad& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width);

    VanadisArm64VecLoad(
        const uint64_t addr, const uint32_t hw_thr, const VanadisDecoderOptions* isa_opts, const Resolver fn,
        const std::vector<uint16_t>& int_in, const std::vector<uint16_t>& int_out, const std::vector<uint16_t>& fp_in,
        const uint16_t target) :
        VanadisInstruction(
            addr, hw_thr, isa_opts, int_in.size(), int_out.size(), int_in.size(), int_out.size(), fp_in.size(), 1,
            fp_in.size(), 1),
        VanadisLoadInstruction(addr, hw_thr, isa_opts, int_in[0], 0, target, 0, false, MEM_TRANSACTION_NONE, LOAD_FP_REGISTER),
        resolver(fn)
    {
        uint16_t k = 0;
        for ( auto r : int_in ) isa_int_regs_in[k++] = r;
        k = 0;
        for ( auto r : int_out ) isa_int_regs_out[k++] = r;
        k = 0;
        for ( auto r : fp_in ) isa_fp_regs_in[k++] = r;
    }

    VanadisArm64VecLoad* clone() override { return new VanadisArm64VecLoad(*this); }
    const char*          getInstCode() const override { return "SVE.LOAD"; }

    void computeLoadAddress(SST::Output* output, VanadisRegisterFile* regFile, uint64_t* out_addr, uint16_t* width) override
    {
        resolver(*this, regFile, out_addr, width);
    }
    void computeLoadAddress(VanadisRegisterFile* regFile, uint64_t* out_addr, uint16_t* width) override
    {
        resolver(*this, regFile, out_addr, width);
    }

    uint16_t getRegisterOffset() const override { return reg_offset; }

    // A vector load merges elements and has fault rules of its own.
    bool takesForwardedStoreData() const override { return false; }
    bool     suppressesFault() const override { return suppress; }
    void     noteSuppressedFault(uint64_t vaddr) override
    {
        if ( !fault_seen || vaddr < fault_vaddr ) fault_vaddr = vaddr;
        fault_seen = true;
    }

    // The first-fault chain: the fault position in elements, the lowest so far, moves through an integer register.
    void finishLoad(VanadisRegisterFile* rf) override
    {
        if ( countISAIntRegOut() == 0 ) return;
        uint64_t prior = rf->getIntReg<uint64_t>(phys_int_regs_in[countISAIntRegIn() - 1]);
        if ( fault_seen ) {
            const uint64_t pos = (fault_vaddr - base_addr) / m.stride;
            if ( pos < prior ) prior = pos;
        }
        rf->setIntReg<uint64_t>(phys_int_regs_out[0], prior);
    }

    uint64_t phys_x(VanadisRegisterFile* rf, uint16_t pos) const { return rf->getIntReg<uint64_t>(phys_int_regs_in[pos]); }
    const uint8_t* phys_v(VanadisRegisterFile* rf, uint16_t pos) const { return rf->fpRegBytes(phys_fp_regs_in[pos]); }

    VanadisArm64MemPayload m;
    uint16_t               reg_offset = 0;
    bool                   suppress = false;
    bool                   fault_seen = false;
    uint64_t               fault_vaddr = 0;
    uint64_t               base_addr = 0;

protected:
    Resolver resolver;
};

class VanadisArm64VecStore : public VanadisStoreInstruction
{
public:
    using Resolver = void (*)(VanadisArm64VecStore& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width);

    // The first FP source is the register that holds the data.
    VanadisArm64VecStore(
        const uint64_t addr, const uint32_t hw_thr, const VanadisDecoderOptions* isa_opts, const Resolver fn,
        const std::vector<uint16_t>& int_in, const std::vector<uint16_t>& fp_in) :
        VanadisInstruction(addr, hw_thr, isa_opts, int_in.size(), 0, int_in.size(), 0, fp_in.size(), 0, fp_in.size(), 0),
        VanadisStoreInstruction(addr, hw_thr, isa_opts, int_in[0], 0, fp_in[0], 0, MEM_TRANSACTION_NONE, STORE_FP_REGISTER),
        resolver(fn)
    {
        uint16_t k = 0;
        for ( auto r : int_in ) isa_int_regs_in[k++] = r;
        k = 0;
        for ( auto r : fp_in ) isa_fp_regs_in[k++] = r;
    }

    VanadisArm64VecStore* clone() override { return new VanadisArm64VecStore(*this); }
    const char*           getInstCode() const override { return "SVE.STORE"; }

    void computeStoreAddress(SST::Output* output, VanadisRegisterFile* regFile, uint64_t* out_addr, uint16_t* width) override
    {
        resolver(*this, regFile, out_addr, width);
    }

    uint16_t getRegisterOffset() const override { return reg_offset; }

    // The base, index and predicate registers decide the addresses, so they must be ready at issue. The first FP source
    // holds the data and is read when the store goes to memory. A vector store gives no bytes to a load.
    bool isStoreDataFPRegIn(const uint16_t index) const override { return 0 == index; }
    bool forwardsDataToLoads() const override { return false; }

    uint64_t phys_x(VanadisRegisterFile* rf, uint16_t pos) const { return rf->getIntReg<uint64_t>(phys_int_regs_in[pos]); }
    const uint8_t* phys_v(VanadisRegisterFile* rf, uint16_t pos) const { return rf->fpRegBytes(phys_fp_regs_in[pos]); }

    VanadisArm64MemPayload m;
    uint16_t               reg_offset = 0;
    uint32_t               index_shift = 0;

protected:
    Resolver resolver;
};

namespace A64SveMem {

using namespace A64Sve;

constexpr uint64_t PAGE_BYTES = 4096;

// Base address of a contiguous operation: X0 + (X1 << shift) + imm.
template <typename Op>
inline uint64_t
contigBase(Op& op, VanadisRegisterFile* rf)
{
    uint64_t a = op.phys_x(rf, 0);
    if ( op.m.has_index ) a += op.phys_x(rf, 1) << op.m.shift;
    return a + static_cast<uint64_t>(op.m.imm);
}

// The first and last active image byte of the chunk [first, first + length) of a structure image. The predicate is FP
// source pred_pos. Returns false when no structure with bytes in the chunk is active.
template <typename Op>
inline bool
activeSpan(Op& op, VanadisRegisterFile* rf, uint16_t pred_pos, uint32_t* lo, uint32_t* hi)
{
    const uint8_t* pg = op.phys_v(rf, pred_pos);
    const uint32_t ebytes = 1u << op.m.log2_ebytes;
    const uint32_t s = op.m.first, end = op.m.first + op.m.length;
    bool any = false;
    for ( uint32_t e = s / op.m.stride; e < op.m.elements && e * op.m.stride < end; ++e ) {
        if ( !pbit(pg, e * ebytes) ) continue;
        const uint32_t a = e * op.m.stride, b = a + op.m.stride;
        const uint32_t from = a < s ? s : a;
        const uint32_t to = (b > end ? end : b) - 1;
        if ( !any ) *lo = from;
        *hi = to;
        any = true;
    }
    return any;
}

// The contiguous load of one chunk: the active span, optionally cut at a page boundary for a first-fault load.
inline void
contigLoadResolve(VanadisArm64VecLoad& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width)
{
    uint32_t lo = 0, hi = 0;
    const uint64_t base = contigBase(op, rf);
    op.base_addr = base;
    *addr = base;
    *width = 0;
    if ( !activeSpan(op, rf, 0, &lo, &hi) ) return;
    uint64_t start = base + lo;
    uint32_t count = hi - lo + 1;
    if ( op.m.page_part ) {
        const uint64_t to_page_end = PAGE_BYTES - (start % PAGE_BYTES);
        if ( op.m.page_part == 1 ) {
            if ( count > to_page_end ) count = static_cast<uint32_t>(to_page_end);
        }
        else {
            if ( count <= to_page_end ) return;
            lo += static_cast<uint32_t>(to_page_end);
            start += to_page_end;
            count -= static_cast<uint32_t>(to_page_end);
        }
    }
    if ( op.m.speculative ) {
        // The first active element of the whole operation may fault, unless the load is non-fault.
        bool holds_first = false;
        if ( !op.m.non_fault && op.m.page_part == 1 ) {
            const uint8_t* pg = op.phys_v(rf, 0);
            const uint32_t ebytes = 1u << op.m.log2_ebytes;
            uint32_t e = 0;
            while ( e < op.m.elements && !pbit(pg, e * ebytes) ) ++e;
            holds_first = e < op.m.elements && e * op.m.stride >= op.m.first && e * op.m.stride < op.m.first + op.m.length;
        }
        op.suppress = !holds_first;
    }
    *addr = start;
    *width = static_cast<uint16_t>(count);
    op.reg_offset = static_cast<uint16_t>(lo - op.m.first);
}

// Writes the bytes of one load chunk to the destination registers. FP sources: Pg, the temporary, then the previous
// values of the destinations unless this is the first chunk. a = log2 element bytes, b = memory element bytes, c =
// registers. d & 1 = signed, d & 2 = first chunk. imm = first image byte, imm2 = image bytes of the chunk.
inline void
contigMerge(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        regs[4][MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint32_t ebytes = 1u << op.a, mbytes = op.b, nreg = op.c;
    const bool     sign = op.d & 1, first = op.d & 2;
    const uint8_t* pg = op.Vin(rf, 0);
    const uint8_t* tmp = op.Vin(rf, 1);
    const uint32_t s = static_cast<uint32_t>(op.imm), end = s + static_cast<uint32_t>(op.imm2);
    const uint32_t stride = mbytes * nreg;
    const uint32_t elements = vl / ebytes;
    for ( uint32_t r = 0; r < nreg; ++r ) {
        if ( first ) std::memset(regs[r], 0, vl);
        else std::memcpy(regs[r], op.Vin(rf, 2 + r), vl);
    }
    for ( uint32_t e = s / stride; e < elements && e * stride < end; ++e ) {
        if ( !pbit(pg, e * ebytes) ) continue;
        for ( uint32_t r = 0; r < nreg; ++r ) {
            for ( uint32_t k = 0; k < mbytes; ++k ) {
                const uint32_t pos = e * stride + r * mbytes + k;
                if ( pos < s || pos >= end ) continue;
                regs[r][e * ebytes + k] = tmp[pos - s];
                if ( sign && k == mbytes - 1 && (tmp[pos - s] & 0x80) ) {
                    std::memset(regs[r] + e * ebytes + mbytes, 0xFF, ebytes - mbytes);
                }
            }
        }
    }
    for ( uint32_t r = 0; r < nreg; ++r ) writeZ(op, rf, regs[r], static_cast<uint16_t>(r));
}

// The merge of a first-fault or non-fault chunk: bytes at addresses below the end of the page of the first active byte
// come from temporary 1, the rest from temporary 2. Integer source: the base address. FP sources: Pg, temporary 1,
// temporary 2, and the previous value unless first. a, b, imm, imm2, d as contigMerge; one register.
inline void
contigMergeFf(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        reg[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint32_t ebytes = 1u << op.a, mbytes = op.b;
    const bool     sign = op.d & 1, first = op.d & 2;
    const uint8_t* pg = op.Vin(rf, 0);
    const uint32_t s = static_cast<uint32_t>(op.imm), end = s + static_cast<uint32_t>(op.imm2);
    const uint32_t elements = vl / ebytes;
    const uint64_t base = op.X(rf, 0);
    if ( first ) std::memset(reg, 0, vl);
    else std::memcpy(reg, op.Vin(rf, 3), vl);
    bool     have_boundary = false;
    uint64_t boundary = 0;
    for ( uint32_t e = s / mbytes; e < elements && e * mbytes < end; ++e ) {
        if ( !pbit(pg, e * ebytes) ) continue;
        for ( uint32_t k = 0; k < mbytes; ++k ) {
            const uint32_t pos = e * mbytes + k;
            if ( pos < s || pos >= end ) continue;
            const uint64_t a = base + pos;
            if ( !have_boundary ) {
                boundary = (a / PAGE_BYTES + 1) * PAGE_BYTES;
                have_boundary = true;
            }
            const uint8_t* tmp = a < boundary ? op.Vin(rf, 1) : op.Vin(rf, 2);
            reg[e * ebytes + k] = tmp[pos - s];
            if ( sign && k == mbytes - 1 && (tmp[pos - s] & 0x80) ) std::memset(reg + e * ebytes + mbytes, 0xFF, ebytes - mbytes);
        }
    }
    writeZ(op, rf, reg);
}

// Packs registers into the memory image, in temporaries. FP sources: the registers. a = log2 element bytes, b = memory
// element bytes, c = registers. FP destinations: the temporaries (one per VL of image).
inline void
contigPack(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        image[4 * MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint32_t ebytes = 1u << op.a, mbytes = op.b, nreg = op.c;
    const uint32_t stride = mbytes * nreg;
    for ( uint32_t e = 0; e < vl / ebytes; ++e ) {
        for ( uint32_t r = 0; r < nreg; ++r ) {
            std::memcpy(image + e * stride + r * mbytes, op.Vin(rf, r) + e * ebytes, mbytes);
        }
    }
    for ( uint32_t k = 0; k * vl < (vl / ebytes) * stride; ++k ) writeZ(op, rf, image + k * vl, static_cast<uint16_t>(k));
}

// One store micro-op: the run of active structures that begins at image byte first inside its chunk, unless the
// previous structure is active and in the same chunk, whose micro-op then stores this one. FP sources: the data
// register, Pg. length = bytes to the end of the chunk, shift = chunk bytes, mode = image byte of the register's byte 0.
inline void
contigStoreResolve(VanadisArm64VecStore& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width)
{
    const uint8_t* pg = op.phys_v(rf, 1);
    const uint32_t ebytes = 1u << op.m.log2_ebytes;
    const uint32_t stride = op.m.stride;
    const uint32_t start = op.m.first, end = start + op.m.length;
    const uint32_t e0 = start / stride;
    *addr = 0;
    *width = 0;
    if ( !pbit(pg, e0 * ebytes) ) return;
    if ( start % op.m.shift != 0 && e0 > 0 && pbit(pg, (e0 - 1) * ebytes) ) return;
    uint32_t stop = start;
    for ( uint32_t e = e0; e < op.m.elements && pbit(pg, e * ebytes); ++e ) {
        const uint32_t next = (e + 1) * stride;
        stop = next < end ? next : end;
        if ( next >= end ) break;
    }
    uint64_t a = op.phys_x(rf, 0);
    if ( op.m.has_index ) a += op.phys_x(rf, 1) << op.index_shift;
    *addr = a + static_cast<uint64_t>(op.m.imm) + start;
    *width = static_cast<uint16_t>(stop - start);
    op.reg_offset = static_cast<uint16_t>(start - op.m.mode);
}

// LDR, STR of a part of a register: fixed offset and width.
inline void
fillResolve(VanadisArm64VecLoad& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width)
{
    *addr = contigBase(op, rf) + op.m.first;
    *width = static_cast<uint16_t>(op.m.length);
    op.reg_offset = 0;
    op.base_addr = *addr;
}

inline void
spillResolve(VanadisArm64VecStore& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width)
{
    *addr = contigBase(op, rf) + op.m.first;
    *width = static_cast<uint16_t>(op.m.length);
    op.reg_offset = static_cast<uint16_t>(op.m.first);
}

// LDR: a = the register bytes total; moves chunk bytes to the destination. FP sources: temporary, previous value unless
// first. imm = first byte, imm2 = bytes, d = first.
inline void
fillMerge(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    if ( op.d ) std::memset(r, 0, vl);
    else std::memcpy(r, op.Vin(rf, 1), vl);
    std::memcpy(r + op.imm, op.Vin(rf, 0), op.imm2);
    writeZ(op, rf, r);
}

// The address of one gather or scatter element. mode: 0 64-bit index, 1 sign-extended 32-bit, 2 zero-extended 32-bit,
// 3 vector base with constant offset, 4 vector base with scalar offset. first = element.
template <typename Op>
inline uint64_t
gatherAddress(Op& op, VanadisRegisterFile* rf, uint16_t zm_pos)
{
    const uint8_t* zm = op.phys_v(rf, zm_pos);
    const uint32_t ebytes = 1u << op.m.log2_ebytes;
    uint64_t raw = 0;
    std::memcpy(&raw, zm + op.m.first * ebytes, ebytes);
    switch ( op.m.mode ) {
    case 0: return op.phys_x(rf, 0) + (raw << op.m.shift);
    case 1: return op.phys_x(rf, 0) + (static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(raw))) << op.m.shift);
    case 2: return op.phys_x(rf, 0) + ((raw & 0xFFFFFFFFull) << op.m.shift);
    case 3: return raw + static_cast<uint64_t>(op.m.imm);
    default: return raw + op.phys_x(rf, 0);
    }
}

// One gather element. FP sources: Pg, Zm.
inline void
gatherResolve(VanadisArm64VecLoad& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width)
{
    const uint8_t* pg = op.phys_v(rf, 0);
    const uint32_t ebytes = 1u << op.m.log2_ebytes;
    *addr = 0;
    *width = 0;
    op.reg_offset = 0;
    if ( !pbit(pg, op.m.first * ebytes) ) return;
    *addr = gatherAddress(op, rf, 1);
    *width = static_cast<uint16_t>(op.m.mbytes);
    if ( op.m.speculative ) {
        uint32_t e = 0;
        while ( e < op.m.elements && !pbit(pg, e * ebytes) ) ++e;
        op.suppress = e != op.m.first;
        // The chain reports a position in elements: stride 1 from a base that is the element's distance.
        op.base_addr = *addr - op.m.first;
        op.m.stride = 1;
    }
}

// Writes one gathered element into the accumulator. FP sources: Pg, temporary, accumulator (unless first). a = log2
// element bytes, b = memory bytes, c = element, d & 1 = signed, d & 2 = first.
inline void
gatherMerge(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint32_t ebytes = 1u << op.a;
    if ( op.d & 2 ) std::memset(r, 0, vl);
    else std::memcpy(r, op.Vin(rf, 2), vl);
    if ( pbit(op.Vin(rf, 0), op.c * ebytes) ) {
        uint8_t*       d = r + op.c * ebytes;
        const uint8_t* t = op.Vin(rf, 1);
        std::memset(d, ((op.d & 1) && (t[op.b - 1] & 0x80)) ? 0xFF : 0, ebytes);
        std::memcpy(d, t, op.b);
    }
    writeZ(op, rf, r);
}

// One scatter element. FP sources: data register, Pg, Zm. shift, mode as gatherAddress; first = element.
inline void
scatterResolve(VanadisArm64VecStore& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width)
{
    const uint8_t* pg = op.phys_v(rf, 1);
    const uint32_t ebytes = 1u << op.m.log2_ebytes;
    *addr = 0;
    *width = 0;
    if ( !pbit(pg, op.m.first * ebytes) ) return;
    *addr = gatherAddress(op, rf, 2);
    *width = static_cast<uint16_t>(op.m.mbytes);
    op.reg_offset = static_cast<uint16_t>(op.m.first * ebytes);
}

// LD1R*: loads one element when any element is active. FP source: Pg.
inline void
replicateResolve(VanadisArm64VecLoad& op, VanadisRegisterFile* rf, uint64_t* addr, uint16_t* width)
{
    const uint8_t* pg = op.phys_v(rf, 0);
    const uint32_t ebytes = 1u << op.m.log2_ebytes;
    *addr = contigBase(op, rf);
    *width = 0;
    op.reg_offset = 0;
    for ( uint32_t e = 0; e < op.m.elements; ++e ) {
        if ( pbit(pg, e * ebytes) ) {
            *width = static_cast<uint16_t>(op.m.mbytes);
            return;
        }
    }
}

// LD1R*: the merge. FP sources Pg, temporary. a = log2 element bytes, b = memory bytes, d = signed.
inline void
replicateMerge(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint32_t ebytes = 1u << op.a;
    const uint8_t* t = op.Vin(rf, 1);
    for ( uint32_t e = 0; e < vl / ebytes; ++e ) {
        uint8_t* d = r + e * ebytes;
        if ( pbit(op.Vin(rf, 0), e * ebytes) ) {
            std::memset(d, (op.d && (t[op.b - 1] & 0x80)) ? 0xFF : 0, ebytes);
            std::memcpy(d, t, op.b);
        }
        else {
            std::memset(d, 0, ebytes);
        }
    }
    writeZ(op, rf, r);
}

// LD1RQ, LD1RO: the first 16 or 32 bytes under the predicate, repeated. FP sources Pg, temporary. a = log2 element
// bytes, b = segment bytes. The temporary holds the segment from its byte 0.
inline void
quadMerge(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        seg[32];
    uint8_t        r[MAX_VL_BYTES];
    const unsigned vl = vlOf(rf);
    const uint32_t ebytes = 1u << op.a;
    const uint8_t* pg = op.Vin(rf, 0);
    for ( uint32_t e = 0; e < op.b / ebytes; ++e ) {
        if ( pbit(pg, e * ebytes) ) std::memcpy(seg + e * ebytes, op.Vin(rf, 1) + e * ebytes, ebytes);
        else std::memset(seg + e * ebytes, 0, ebytes);
    }
    std::memset(r, 0, vl);
    for ( uint32_t k = 0; (k + 1) * op.b <= vl; ++k ) std::memcpy(r + k * op.b, seg, op.b);
    writeZ(op, rf, r);
}

// Updates FFR from the first-fault chain: integer source 0 = the lowest faulted element, ~0 for none. FP source FFR.
// a = log2 element bytes. The bits at and above the element are cleared.
inline void
ffrUpdate(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t        r[MAX_VL_BYTES / 8];
    const unsigned vl = vlOf(rf);
    const uint32_t ebytes = 1u << op.a;
    const uint64_t pos = op.X(rf, 0);
    std::memcpy(r, op.Vin(rf, 0), vl / 8);
    for ( uint64_t b = pos >= vl ? vl : pos * ebytes; b < vl; ++b ) {
        r[b / 8] = static_cast<uint8_t>(r[b / 8] & ~(1u << (b % 8)));
    }
    writeP(op, rf, r);
}

} // namespace A64SveMem
} // namespace Vanadis
} // namespace SST

#endif
