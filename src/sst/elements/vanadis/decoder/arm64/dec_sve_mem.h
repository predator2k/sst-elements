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

// Decoder groups of the SVE loads and stores. Member functions of VanadisARM64Decoder; included at the end of
// decoder/varm64decoder.h.

#ifndef _H_VANADIS_ARM64_DEC_SVE_MEM
#define _H_VANADIS_ARM64_DEC_SVE_MEM

namespace SST {
namespace Vanadis {

// The element size, memory bytes and signedness of the 4-bit dtype of SVE loads.
struct A64SveDtype
{
    uint8_t log2_ebytes;
    uint8_t mbytes;
    bool    is_signed;
};
inline A64SveDtype
a64SveDtype(uint32_t dtype)
{
    static constexpr A64SveDtype table[16] = {
        { 0, 1, false }, { 1, 1, false }, { 2, 1, false }, { 3, 1, false }, { 3, 4, true }, { 1, 2, false }, { 2, 2, false }, { 3, 2, false },
        { 3, 2, true }, { 2, 2, true }, { 2, 4, false }, { 3, 4, false }, { 3, 1, true }, { 2, 1, true }, { 1, 1, true }, { 3, 8, false }
    };
    return table[dtype];
}

inline uint32_t
VanadisARM64Decoder::sveChunkBytes() const
{
    return (vector_bytes % 64 == 0) ? 64 : (vector_bytes % 32 == 0) ? 32 : 16;
}

// A contiguous load of nreg registers from base + (index << shift) + imm. index = 0xFFFF for none. ff: 0 normal,
// 1 first-fault, 2 non-fault.
inline void
VanadisARM64Decoder::sveContigLoad(uint64_t addr, VanadisInstructionBundle* bundle, uint16_t pg, uint16_t zt, uint32_t nreg,
    A64SveDtype dt, uint16_t base, uint16_t index_reg, uint32_t shift, int64_t imm, uint32_t ff)
{
    using namespace A64SveMem;
    const uint32_t elements = vector_bytes >> dt.log2_ebytes;
    const uint32_t stride = dt.mbytes * nreg;
    const uint32_t image = elements * stride;
    const uint32_t chunk = sveChunkBytes();
    std::vector<uint16_t> zs;
    for ( uint32_t r = 0; r < nreg; ++r ) zs.push_back(static_cast<uint16_t>((zt + r) % 32));

    const auto fill_payload = [&](VanadisArm64MemPayload& m, uint32_t c) {
        m.imm = imm;
        m.log2_ebytes = dt.log2_ebytes;
        m.mbytes = dt.mbytes;
        m.shift = shift;
        m.stride = stride;
        m.first = c;
        m.length = image - c < chunk ? image - c : chunk;
        m.elements = elements;
        m.has_index = index_reg != 0xFFFF;
    };

    if ( ff == 0 ) {
        for ( uint32_t c = 0; c < image; c += chunk ) {
            auto* l = new VanadisArm64VecLoad(addr, hw_thr, options, contigLoadResolve,
                index_reg != 0xFFFF ? std::vector<uint16_t> { base, index_reg } : std::vector<uint16_t> { base }, {}, { pg }, A64Reg::VT0);
            fill_payload(l->m, c);
            const uint32_t length = l->m.length;
            add(bundle, l);
            std::vector<uint16_t> in { pg, A64Reg::VT0 };
            if ( c != 0 ) in.insert(in.end(), zs.begin(), zs.end());
            auto* i = op(bundle, addr, "SVE.MERGE", INST_FP_ARITH, contigMerge, {}, {}, in, zs);
            i->a = dt.log2_ebytes;
            i->b = dt.mbytes;
            i->c = nreg;
            i->d = (dt.is_signed ? 1u : 0u) | (c == 0 ? 2u : 0u);
            i->imm = c;
            i->imm2 = length;
        }
        return;
    }
    // T0 = the base address; T3 is the first-fault chain.
    auto* b = op(bundle, addr, "SVE.BASE", INST_INT_ARITH, [](VanadisArm64Inst& o, VanadisRegisterFile* rf) {
        uint64_t a = o.X(rf, 0);
        if ( o.d ) a += o.X(rf, 1) << o.c;
        o.setX(rf, 0, a + o.imm);
    }, index_reg != 0xFFFF ? std::vector<uint16_t> { base, index_reg } : std::vector<uint16_t> { base }, { A64Reg::T0 });
    b->c = shift;
    b->d = index_reg != 0xFFFF;
    b->imm = static_cast<uint64_t>(imm);
    op(bundle, addr, "SVE.FFINIT", INST_INT_ARITH, A64Sem::movImm, {}, { A64Reg::T3 })->imm = ~0ull;
    for ( uint32_t c = 0; c < image; c += chunk ) {
        uint32_t length = 0;
        for ( uint32_t part = 1; part <= 2; ++part ) {
            auto* l = new VanadisArm64VecLoad(addr, hw_thr, options, contigLoadResolve, { A64Reg::T0, A64Reg::T3 }, { A64Reg::T3 }, { pg },
                part == 1 ? A64Reg::VT0 : A64Reg::VT1);
            fill_payload(l->m, c);
            l->m.imm = 0;
            l->m.has_index = false;
            l->m.speculative = true;
            l->m.non_fault = ff == 2;
            l->m.page_part = part;
            length = l->m.length;
            add(bundle, l);
        }
        std::vector<uint16_t> in { pg, A64Reg::VT0, A64Reg::VT1 };
        if ( c != 0 ) in.push_back(zt);
        auto* i = op(bundle, addr, "SVE.MERGEFF", INST_FP_ARITH, contigMergeFf, { A64Reg::T0 }, {}, in, { zt });
        i->a = dt.log2_ebytes;
        i->b = dt.mbytes;
        i->d = (dt.is_signed ? 1u : 0u) | (c == 0 ? 2u : 0u);
        i->imm = c;
        i->imm2 = length;
    }
    auto* u = op(bundle, addr, "SVE.FFR", INST_FP_ARITH, ffrUpdate, { A64Reg::T3 }, {}, { A64Reg::FFR }, { A64Reg::FFR });
    u->a = dt.log2_ebytes;
}

inline void
VanadisARM64Decoder::sveContigStore(uint64_t addr, VanadisInstructionBundle* bundle, uint16_t pg, uint16_t zt, uint32_t nreg,
    uint32_t log2_ebytes, uint32_t mbytes, uint16_t base, uint16_t index_reg, uint32_t shift, int64_t imm)
{
    using namespace A64SveMem;
    const uint32_t elements = vector_bytes >> log2_ebytes;
    const uint32_t stride = mbytes * nreg;
    const uint32_t image = elements * stride;
    const uint32_t chunk = sveChunkBytes();
    const bool     direct = nreg == 1 && mbytes == (1u << log2_ebytes);
    static constexpr uint16_t temps[4] = { A64Reg::VT0, A64Reg::VT1, A64Reg::VT2, A64Reg::VT3 };
    if ( !direct ) {
        std::vector<uint16_t> zs, out;
        for ( uint32_t r = 0; r < nreg; ++r ) zs.push_back(static_cast<uint16_t>((zt + r) % 32));
        for ( uint32_t k = 0; k * vector_bytes < image; ++k ) out.push_back(temps[k]);
        auto* i = op(bundle, addr, "SVE.PACK", INST_FP_ARITH, contigPack, {}, {}, zs, out);
        i->a = log2_ebytes;
        i->b = mbytes;
        i->c = nreg;
    }
    for ( uint32_t c = 0; c < image; c += chunk ) {
        const uint32_t chunk_end = image - c < chunk ? image : c + chunk;
        const uint32_t temp = c / vector_bytes;
        for ( uint32_t e = c / stride; e < elements && e * stride < chunk_end; ++e ) {
            const uint32_t from = e * stride < c ? c : e * stride;
            auto* s = new VanadisArm64VecStore(addr, hw_thr, options, contigStoreResolve,
                index_reg != 0xFFFF ? std::vector<uint16_t> { base, index_reg } : std::vector<uint16_t> { base },
                { direct ? zt : temps[temp], pg });
            s->m.imm = imm;
            s->m.log2_ebytes = log2_ebytes;
            s->m.mbytes = mbytes;
            s->m.stride = stride;
            s->m.first = from;
            s->m.length = chunk_end - from;
            s->m.elements = elements;
            s->m.has_index = index_reg != 0xFFFF;
            s->m.shift = chunk;
            s->m.mode = temp * vector_bytes;
            // contigBase shifts the index by index_shift, kept apart from the chunk size.
            s->index_shift = shift;
            add(bundle, s);
        }
    }
}

inline void
VanadisARM64Decoder::sveGather(uint64_t addr, VanadisInstructionBundle* bundle, uint16_t pg, uint16_t zt, uint16_t zm, uint16_t base,
    uint32_t mode, uint32_t shift, int64_t imm, A64SveDtype dt, bool ff)
{
    using namespace A64SveMem;
    const uint32_t elements = vector_bytes >> dt.log2_ebytes;
    if ( ff ) op(bundle, addr, "SVE.FFINIT", INST_INT_ARITH, A64Sem::movImm, {}, { A64Reg::T3 })->imm = ~0ull;
    for ( uint32_t e = 0; e < elements; ++e ) {
        auto* l = new VanadisArm64VecLoad(addr, hw_thr, options, gatherResolve,
            ff ? std::vector<uint16_t> { base, A64Reg::T3 } : std::vector<uint16_t> { base },
            ff ? std::vector<uint16_t> { A64Reg::T3 } : std::vector<uint16_t> {}, { pg, zm }, A64Reg::VT0);
        l->m.imm = imm;
        l->m.log2_ebytes = dt.log2_ebytes;
        l->m.mbytes = dt.mbytes;
        l->m.shift = shift;
        l->m.mode = mode;
        l->m.first = e;
        l->m.elements = elements;
        l->m.speculative = ff;
        add(bundle, l);
        std::vector<uint16_t> in { pg, A64Reg::VT0 };
        if ( e != 0 ) in.push_back(A64Reg::VT1);
        auto* i = op(bundle, addr, "SVE.GMERGE", INST_FP_ARITH, gatherMerge, {}, {}, in, { A64Reg::VT1 });
        i->a = dt.log2_ebytes;
        i->b = dt.mbytes;
        i->c = e;
        i->d = (dt.is_signed ? 1u : 0u) | (e == 0 ? 2u : 0u);
    }
    op(bundle, addr, "SVE.GCOPY", INST_FP_ARITH, A64Sve::copyZ, {}, {}, { A64Reg::VT1 }, { zt });
    if ( ff ) {
        auto* u = op(bundle, addr, "SVE.FFR", INST_FP_ARITH, ffrUpdate, { A64Reg::T3 }, {}, { A64Reg::FFR }, { A64Reg::FFR });
        u->a = dt.log2_ebytes;
    }
}

inline void
VanadisARM64Decoder::sveScatter(uint64_t addr, VanadisInstructionBundle* bundle, uint16_t pg, uint16_t zt, uint16_t zm, uint16_t base,
    uint32_t mode, uint32_t shift, int64_t imm, uint32_t log2_ebytes, uint32_t mbytes)
{
    using namespace A64SveMem;
    const uint32_t elements = vector_bytes >> log2_ebytes;
    for ( uint32_t e = 0; e < elements; ++e ) {
        auto* s = new VanadisArm64VecStore(addr, hw_thr, options, scatterResolve, { base }, { zt, pg, zm });
        s->m.imm = imm;
        s->m.log2_ebytes = log2_ebytes;
        s->m.mbytes = mbytes;
        s->m.shift = shift;
        s->m.mode = mode;
        s->m.first = e;
        s->m.elements = elements;
        add(bundle, s);
    }
}

inline bool
VanadisARM64Decoder::decodeSveMem(uint64_t addr, uint32_t ins, VanadisInstructionBundle* bundle)
{
    using namespace A64SveMem;
    const uint32_t top = bits(ins, 31, 29);
    const uint16_t pg = a64Pred(bits(ins, 12, 10));
    const uint16_t zt = bits(ins, 4, 0);
    const uint32_t rn = bits(ins, 9, 5), rm = bits(ins, 20, 16);
    const uint32_t b1513 = bits(ins, 15, 13);
    const uint32_t msz = bits(ins, 24, 23);
    const auto nop = [&]() {
        add(bundle, new VanadisNoOpInstruction(addr, hw_thr, options));
        return true;
    };

    if ( top == 5 ) {
        const A64SveDtype dt = a64SveDtype(bits(ins, 24, 21));
        const uint32_t log2_m = dt.mbytes == 1 ? 0 : dt.mbytes == 2 ? 1 : dt.mbytes == 4 ? 2 : 3;
        const int64_t  imm4_vl = sext(bits(ins, 19, 16), 4) * static_cast<int64_t>(vector_bytes);
        switch ( b1513 ) {
        case 2:   // LD1 scalar + scalar
            sveContigLoad(addr, bundle, pg, zt, 1, dt, rs(rn), rz(rm), log2_m, 0, 0);
            return true;
        case 3:   // LDFF1
            sveContigLoad(addr, bundle, pg, zt, 1, dt, rs(rn), rz(rm), log2_m, 0, 1);
            return true;
        case 5:   // LD1 scalar + immediate; LDNF1
            if ( !bit(ins, 20) ) {
                sveContigLoad(addr, bundle, pg, zt, 1, dt, rs(rn), 0xFFFF, 0, imm4_vl / static_cast<int64_t>(1u << dt.log2_ebytes) * dt.mbytes, 0);
            }
            else {
                sveContigLoad(addr, bundle, pg, zt, 1, dt, rs(rn), 0xFFFF, 0, imm4_vl / static_cast<int64_t>(1u << dt.log2_ebytes) * dt.mbytes, 2);
            }
            return true;
        case 6:
        case 7: {
            // LD2, LD3, LD4 and LDNT1; 7 is the immediate form
            if ( b1513 == 7 && bit(ins, 20) ) return false;
            const uint32_t opc = bits(ins, 22, 21);
            const A64SveDtype st = { static_cast<uint8_t>(msz), static_cast<uint8_t>(1u << msz), false };
            if ( b1513 == 6 ) sveContigLoad(addr, bundle, pg, zt, opc + 1, st, rs(rn), rz(rm), msz, 0, 0);
            else sveContigLoad(addr, bundle, pg, zt, opc + 1, st, rs(rn), 0xFFFF, 0, imm4_vl * (opc + 1), 0);
            return true;
        }
        case 0:
        case 1: {
            // LD1RQ, LD1RO
            if ( bit(ins, 22) || (b1513 == 1 && bit(ins, 20)) ) return false;
            const bool ro = bit(ins, 21);
            const uint32_t seg = ro ? 32 : 16;
            if ( vector_bytes < seg ) return false;
            auto* l = new VanadisArm64VecLoad(addr, hw_thr, options, contigLoadResolve,
                b1513 == 0 ? std::vector<uint16_t> { rs(rn), rz(rm) } : std::vector<uint16_t> { rs(rn) }, {}, { pg }, A64Reg::VT0);
            l->m.imm = b1513 == 0 ? 0 : sext(bits(ins, 19, 16), 4) * static_cast<int64_t>(seg);
            l->m.log2_ebytes = msz;
            l->m.mbytes = 1u << msz;
            l->m.shift = msz;
            l->m.stride = 1u << msz;
            l->m.first = 0;
            l->m.length = seg;
            l->m.elements = seg >> msz;
            l->m.has_index = b1513 == 0;
            add(bundle, l);
            auto* i = op(bundle, addr, "LD1RQ.M", INST_FP_ARITH, quadMerge, {}, {}, { pg, A64Reg::VT0 }, { zt });
            i->a = msz;
            i->b = seg;
            return true;
        }
        default: return false;
        }
    }

    if ( top == 4 ) {
        if ( bit(ins, 22) && bit(ins, 15) ) {
            // LD1R*
            const A64SveDtype dt = a64SveDtype((msz << 2) | bits(ins, 14, 13));
            auto* l = new VanadisArm64VecLoad(addr, hw_thr, options, replicateResolve, { rs(rn) }, {}, { pg }, A64Reg::VT0);
            l->m.imm = static_cast<int64_t>(bits(ins, 21, 16)) * dt.mbytes;
            l->m.log2_ebytes = dt.log2_ebytes;
            l->m.mbytes = dt.mbytes;
            l->m.elements = vector_bytes >> dt.log2_ebytes;
            add(bundle, l);
            auto* i = op(bundle, addr, "LD1R.M", INST_FP_ARITH, replicateMerge, {}, {}, { pg, A64Reg::VT0 }, { zt });
            i->a = dt.log2_ebytes;
            i->b = dt.mbytes;
            i->d = dt.is_signed;
            return true;
        }
        if ( (ins & 0xFFC0E010) == 0x85800000 || (ins & 0xFFC0E000) == 0x85804000 ) {
            // LDR P, LDR Z
            const bool is_z = bit(ins, 14);
            const int64_t imm9 = sext((bits(ins, 21, 16) << 3) | bits(ins, 12, 10), 9);
            const uint32_t total = is_z ? vector_bytes : vector_bytes / 8;
            const uint16_t target = is_z ? zt : a64Pred(bits(ins, 3, 0));
            const int64_t off = imm9 * static_cast<int64_t>(total);
            const uint32_t chunk = sveChunkBytes();
            if ( total <= chunk ) {
                loadFp(bundle, addr, rs(rn), off, target, total);
                return true;
            }
            for ( uint32_t c = 0; c < total; c += chunk ) {
                auto* l = new VanadisArm64VecLoad(addr, hw_thr, options, fillResolve, { rs(rn) }, {}, {}, A64Reg::VT0);
                l->m.imm = off;
                l->m.first = c;
                l->m.length = chunk;
                add(bundle, l);
                auto* i = op(bundle, addr, "FILL.M", INST_FP_ARITH, fillMerge, {}, {},
                    c == 0 ? std::vector<uint16_t> { A64Reg::VT0 } : std::vector<uint16_t> { A64Reg::VT0, target }, { target });
                i->d = c == 0;
                i->imm = c;
                i->imm2 = chunk;
            }
            return true;
        }
        // Prefetches.
        if ( (ins & 0xFE60E010) == 0x8400C000 || (ins & 0xFFC08010) == 0x85C00000 || (ins & 0xFE60E010) == 0x8400E000 ||
             (ins & 0xFFA08010) == 0x84200000 ) return nop();
        if ( !bit(ins, 15) ) {
            // 32-bit elements, scalar base, 32-bit offsets
            const bool scaled = bit(ins, 21), xs = bit(ins, 22);
            const bool ff = bit(ins, 13), u = bit(ins, 14);
            if ( msz == 3 || (msz == 2 && !u) ) return false;
            const A64SveDtype dt = { 2, static_cast<uint8_t>(1u << msz), !u };
            if ( scaled && msz == 0 ) return false;
            sveGather(addr, bundle, pg, zt, rm, rs(rn), xs ? 1 : 2, scaled ? msz : 0, 0, dt, ff);
            return true;
        }
        if ( bit(ins, 21) && !bit(ins, 22) ) {
            // vector base + immediate
            const bool ff = bit(ins, 13), u = bit(ins, 14);
            if ( msz == 3 || (msz == 2 && !u) ) return false;
            const A64SveDtype dt = { 2, static_cast<uint8_t>(1u << msz), !u };
            sveGather(addr, bundle, pg, zt, rn, A64Reg::ZR, 3, 0, static_cast<int64_t>(rm) * dt.mbytes, dt, ff);
            return true;
        }
        if ( !bit(ins, 21) && !bit(ins, 22) && !bit(ins, 14) ) {
            // LDNT1 (vector base + scalar): bit 13 = unsigned
            const bool u = bit(ins, 13);
            if ( msz == 3 || (msz == 2 && !u) ) return false;
            const A64SveDtype dt = { 2, static_cast<uint8_t>(1u << msz), !u };
            sveGather(addr, bundle, pg, zt, rn, rz(rm), 4, 0, 0, dt, false);
            return true;
        }
        return false;
    }

    if ( top == 6 ) {
        // Gather into 64-bit elements.
        const bool u = bit(ins, 14), ff = bit(ins, 13);
        if ( (ins & 0xFE60E010) == 0xC400E000 || (ins & 0xFFA08010) == 0xC4200000 || (ins & 0xFFE08010) == 0xC4608000 ) return nop();
        const auto dtype_ok = [&]() { return !(msz == 3 && !u); };
        const A64SveDtype dt = { 3, static_cast<uint8_t>(1u << msz), !u };
        if ( !bit(ins, 15) ) {
            // 32-bit offsets, unpacked; bit 22 = sign-extended, bit 21 = scaled
            if ( !dtype_ok() || (bit(ins, 21) && msz == 0) ) return false;
            sveGather(addr, bundle, pg, zt, rm, rs(rn), bit(ins, 22) ? 1 : 2, bit(ins, 21) ? msz : 0, 0, dt, ff);
            return true;
        }
        if ( bit(ins, 22) ) {
            // 64-bit offsets
            if ( !dtype_ok() || (bit(ins, 21) && msz == 0) ) return false;
            sveGather(addr, bundle, pg, zt, rm, rs(rn), 0, bit(ins, 21) ? msz : 0, 0, dt, ff);
            return true;
        }
        if ( bit(ins, 21) ) {
            // vector base + immediate
            if ( !dtype_ok() ) return false;
            sveGather(addr, bundle, pg, zt, rn, A64Reg::ZR, 3, 0, static_cast<int64_t>(rm) * dt.mbytes, dt, ff);
            return true;
        }
        // LDNT1 64: bit 14 = unsigned, bit 13 = 0
        if ( ff || !dtype_ok() ) return false;
        sveGather(addr, bundle, pg, zt, rn, rz(rm), 4, 0, 0, dt, false);
        return true;
    }

    // top == 7: stores
    const uint32_t size = bits(ins, 22, 21);
    const int64_t  imm4_vl = sext(bits(ins, 19, 16), 4) * static_cast<int64_t>(vector_bytes);
    if ( (ins & 0xFFC0E010) == 0xE5800000 || (ins & 0xFFC0E000) == 0xE5804000 ) {
        const bool is_z = bit(ins, 14);
        const int64_t imm9 = sext((bits(ins, 21, 16) << 3) | bits(ins, 12, 10), 9);
        const uint32_t total = is_z ? vector_bytes : vector_bytes / 8;
        const uint16_t source = is_z ? zt : a64Pred(bits(ins, 3, 0));
        const int64_t off = imm9 * static_cast<int64_t>(total);
        const uint32_t chunk = sveChunkBytes();
        if ( total <= chunk ) {
            storeFp(bundle, addr, rs(rn), off, source, total);
            return true;
        }
        for ( uint32_t c = 0; c < total; c += chunk ) {
            auto* s = new VanadisArm64VecStore(addr, hw_thr, options, spillResolve, { rs(rn) }, { source });
            s->m.imm = off;
            s->m.first = c;
            s->m.length = chunk;
            add(bundle, s);
        }
        return true;
    }
    switch ( b1513 ) {
    case 7: {
        if ( bit(ins, 20) ) {
            // ST2, ST3, ST4 and STNT1, immediate form
            const uint32_t nreg = size + 1;
            sveContigStore(addr, bundle, pg, zt, nreg, msz, 1u << msz, rs(rn), 0xFFFF, 0, imm4_vl * nreg);
            return true;
        }
        // ST1, immediate form: bits 22..21 = element size
        if ( size < msz ) return false;
        sveContigStore(addr, bundle, pg, zt, 1, size, 1u << msz, rs(rn), 0xFFFF, 0, imm4_vl / static_cast<int64_t>(1u << size) * (1 << msz));
        return true;
    }
    case 2: {
        if ( size < msz ) return false;
        sveContigStore(addr, bundle, pg, zt, 1, size, 1u << msz, rs(rn), rz(rm), msz, 0);
        return true;
    }
    case 3: {
        // ST2, ST3, ST4 and STNT1, scalar + scalar
        sveContigStore(addr, bundle, pg, zt, size + 1, msz, 1u << msz, rs(rn), rz(rm), msz, 0);
        return true;
    }
    case 5: {
        switch ( size ) {
        case 0:   // [Xn, Zm.D]
            sveScatter(addr, bundle, pg, zt, rm, rs(rn), 0, 0, 0, 3, 1u << msz);
            return true;
        case 1:   // [Xn, Zm.D, LSL #n]
            if ( msz == 0 ) return false;
            sveScatter(addr, bundle, pg, zt, rm, rs(rn), 0, msz, 0, 3, 1u << msz);
            return true;
        case 2:   // [Zn.D, #imm]
            sveScatter(addr, bundle, pg, zt, rn, A64Reg::ZR, 3, 0, static_cast<int64_t>(rm) * (1 << msz), 3, 1u << msz);
            return true;
        default:  // [Zn.S, #imm]
            if ( msz == 3 ) return false;
            sveScatter(addr, bundle, pg, zt, rn, A64Reg::ZR, 3, 0, static_cast<int64_t>(rm) * (1 << msz), 2, 1u << msz);
            return true;
        }
    }
    case 4:
    case 6: {
        // [Xn, Zm, xtw]: bit 14 = sign-extended; bits 22..21: 00 D unscaled, 01 D scaled, 10 S unscaled, 11 S scaled
        const uint32_t mode = bit(ins, 14) ? 1 : 2;
        const bool scaled = size & 1;
        const uint32_t log2_e = (size & 2) ? 2 : 3;
        if ( (scaled && msz == 0) || (log2_e == 2 && msz == 3) ) return false;
        sveScatter(addr, bundle, pg, zt, rm, rs(rn), mode, scaled ? msz : 0, 0, log2_e, 1u << msz);
        return true;
    }
    case 1: {
        // STNT1 (vector base + scalar): bit 22 = S elements, bit 21 must be 0
        if ( bit(ins, 21) ) return false;
        const uint32_t log2_e = bit(ins, 22) ? 2 : 3;
        if ( log2_e == 2 && msz == 3 ) return false;
        sveScatter(addr, bundle, pg, zt, rn, rz(rm), 4, 0, 0, log2_e, 1u << msz);
        return true;
    }
    default: return false;
    }
}

} // namespace Vanadis
} // namespace SST

#endif
