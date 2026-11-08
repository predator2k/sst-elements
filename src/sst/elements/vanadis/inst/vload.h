// Copyright 2009-2026 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2026, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _H_VANADIS_LOAD
#define _H_VANADIS_LOAD

#include "inst/vmemflagtype.h"

#ifndef PRI_ADDR
#define PRI_ADDR PRIx64
#endif

namespace SST {
namespace Vanadis {

enum VanadisLoadRegisterType { LOAD_INT_REGISTER, LOAD_FP_REGISTER };

class VanadisLoadInstruction : public virtual VanadisInstruction
{

public:
    VanadisLoadInstruction(
        const uint64_t addr, const uint32_t hw_thr, const VanadisDecoderOptions* isa_opts, const uint16_t memAddrReg,
        const int64_t offst, const uint16_t tgtReg, const uint16_t load_bytes, const bool extend_sign,
        const VanadisMemoryTransaction accessT, VanadisLoadRegisterType regT) :
        VanadisInstruction(
            addr, hw_thr, isa_opts,
            1, regT == LOAD_INT_REGISTER ? 1 : 0,
            1, regT == LOAD_INT_REGISTER ? 1 : 0,
            0, regT == LOAD_FP_REGISTER ? 1 : 0,
            0, regT == LOAD_FP_REGISTER ? 1 : 0),
        offset(offst),
        load_width(load_bytes),
        signed_extend(extend_sign),
        memAccessType(accessT),
        regType(regT)
    {

        isa_int_regs_in[0] = memAddrReg;

        switch ( regT ) {
        case LOAD_INT_REGISTER:
        {
            isa_int_regs_out[0] = tgtReg;
        } break;
        case LOAD_FP_REGISTER:
        {
            isa_fp_regs_out[0] = tgtReg;
        } break;
        }

    }

    VanadisLoadInstruction* clone() { return new VanadisLoadInstruction(*this); }

    bool performSignExtension() const { return signed_extend; }

    // A speculative vector load (first-fault, non-fault) takes a failed access as data rather than as an error.
    virtual bool suppressesFault() const { return false; }
    virtual void noteSuppressedFault(uint64_t vaddr) {}
    // Called once when the last request of the load has answered, or when computeLoadAddress asked for no access.
    virtual void finishLoad(VanadisRegisterFile* regFile) {}

    // The bytes this load read from memory or from a store: used to find a load that read before an older store knew its
    // address. A width of zero means it has not read yet.
    void     setAccessRange(const uint64_t address, const uint64_t width) { access_address = address; access_width = width; }
    uint64_t getAccessAddress() const { return access_address; }
    uint64_t getAccessWidth() const { return access_width; }

    virtual bool                      isPartialLoad() const { return false; }
    // A load of one register may take all its bytes from an older store.
    virtual bool                      takesForwardedStoreData() const { return MEM_TRANSACTION_NONE == memAccessType && ! isPartialLoad(); }
    virtual VanadisFunctionalUnitType getInstFuncType() const { return INST_LOAD; }
    virtual const char*               getInstCode() const
    {
        switch ( memAccessType ) {
        case MEM_TRANSACTION_LLSC_LOAD:
            return "LLSC_LOAD";
        case MEM_TRANSACTION_LOCK:
            return "LOCK_LOAD";
        case MEM_TRANSACTION_NONE:
        default:
            switch ( regType ) {
            case LOAD_INT_REGISTER:
                return "LOAD";
            case LOAD_FP_REGISTER:
                return "LOADFP";
            }
        }

        return "LOADUNK";
    }

    uint16_t getMemoryAddressRegister() const { return phys_int_regs_in[0]; }
    uint16_t getTargetRegister() const {
        switch(regType) {
            case LOAD_INT_REGISTER:
                return phys_int_regs_out[0];
            case LOAD_FP_REGISTER:
                return phys_fp_regs_out[0];
            default:
                assert(0);
        }
    }

    virtual void scalarExecute(SST::Output* output, VanadisRegisterFile* regFile) { markExecuted(); }

    virtual VanadisMemoryTransaction getTransactionType() const { return memAccessType; }

    virtual void printToBuffer(char* buffer, size_t buffer_size)
    {
        switch ( regType ) {
        case LOAD_INT_REGISTER:
        {
            snprintf(
                buffer, buffer_size,
                "LOAD (%s, %" PRIu16 " bytes)  %5" PRIu16 " <- memory[ %5" PRIu16 " + %" PRId64
                " (0x%" PRI_ADDR ") (phys: %5" PRIu16 " <- memory[%5" PRIu16 " + %" PRId64 " (0x%" PRI_ADDR ")])",
                getTransactionTypeString(memAccessType), load_width, isa_int_regs_out[0], isa_int_regs_in[0], offset,
                offset, phys_int_regs_out[0], phys_int_regs_in[0], offset, offset);
        } break;
        case LOAD_FP_REGISTER:
        {
            snprintf(
                buffer, buffer_size,
                "LOADFP (%s, %" PRIu16 " bytes)  %5" PRIu16 " <- memory[ %5" PRIu16 " + %" PRId64
                " (0x%" PRI_ADDR ") (phys: %5" PRIu16 " <- memory[%5" PRIu16 " + %" PRId64 " (0x%" PRI_ADDR ")])",
                getTransactionTypeString(memAccessType), load_width, isa_fp_regs_out[0], isa_int_regs_in[0], offset,
                offset, phys_fp_regs_out[0], phys_int_regs_in[0], offset, offset);
        } break;
        }
    }

    virtual void computeLoadAddress(VanadisRegisterFile* reg, uint64_t* out_addr, uint16_t* width)
    {
        int64_t tmp = reg->getIntReg<int64_t>(phys_int_regs_in[0]);

        (*out_addr) = (uint64_t)(tmp + offset);
        (*width)    = load_width;
    }

    virtual void
    computeLoadAddress(SST::Output* output, VanadisRegisterFile* regFile, uint64_t* out_addr, uint16_t* width)
    {
        int64_t tmp_val;
        uint64_t mem_addr_reg_val;
        uint16_t target_tid = 0;


        mem_addr_reg_val = regFile->getIntReg<uint64_t>(phys_int_regs_in[0]);

        #ifdef VANADIS_BUILD_DEBUG
        switch ( regType ) {
        case LOAD_INT_REGISTER:
        {
            // if(output->getVerboseLevel() >= 16)
            {
                output->verbose(
                    CALL_INFO, 16, 0,
                    "Execute: (0x%" PRI_ADDR ") LOAD addr-reg: %" PRIu16 " phys: %" PRIu16 " / offset: %" PRId64
                    " / target: %" PRIu16 " phys: %" PRIu16 "\n",
                    getInstructionAddress(), isa_int_regs_in[0], phys_int_regs_in[0], offset, isa_int_regs_out[0],
                    phys_int_regs_out[0]);
            }
        } break;
        case LOAD_FP_REGISTER:
        {
            if(output->getVerboseLevel() >= 16) {
                output->verbose(
                    CALL_INFO, 16, 0,
                    "Execute: (0x%" PRI_ADDR ") LOAD addr-reg: %" PRIu16 " phys: %" PRIu16 " / offset: %" PRId64
                    " / target: %" PRIu16 " phys: %" PRIu16 "\n",
                    getInstructionAddress(), isa_int_regs_in[0], phys_int_regs_in[0], offset, isa_fp_regs_out[0],
                    phys_fp_regs_out[0]);
            }
        } break;
        }

        // if(output->getVerboseLevel() >= 16)
        {
            output->verbose(
                CALL_INFO, 16, 0, "[execute-load]: transaction-type:  %s / ins: 0x%" PRI_ADDR "\n",
                getTransactionTypeString(memAccessType), getInstructionAddress());
            output->verbose(
                CALL_INFO, 16, 0, "[execute-load]: reg[%5" PRIu16 "]:       %" PRIu64 "\n", phys_int_regs_in[0],
                mem_addr_reg_val);
            output->verbose(CALL_INFO, 16, 0, "[execute-load]: offset           : %" PRId64 "\n", offset);
            output->verbose(
                CALL_INFO, 16, 0, "[execute-load]: (add)            : %" PRIu64 "\n", (mem_addr_reg_val + offset));
        }
        #endif

        tmp_val = regFile->getIntReg<int64_t>(phys_int_regs_in[0]);

        (*out_addr) = (uint64_t)(tmp_val + offset);
        (*width)    = load_width;

    }

    virtual uint16_t getLoadWidth() const { return load_width; }

    VanadisLoadRegisterType getValueRegisterType() const { return regType; }

    virtual uint16_t getRegisterOffset() const { return 0; }

    // The second register of a load pair whose registers fit one load port: it takes no issue slot of its own.
    bool sharesLoadPort() const { return shares_load_port_; }
    void markSharesLoadPort() { shares_load_port_ = true; }

    // A Load-Acquire with RCsc order (LDAR, LDLAR, LDAXR, LDAXP, the load of an acquire read-modify-write; not LDAPR): it
    // does not read before every older Store-Release of its thread is visible (acquire_waits_for_release of the LSQ).
    // The marks of this and the next pair of methods come from the work package vanadis-s7 (fork commit 0413e57b5).
    bool isAcquire() const { return acquire_; }
    void markAcquire() { acquire_ = true; }
    // A Load-AcquirePC (LDAPR, LDAPUR; RCpc order): it orders the younger accesses of its thread as a Load-Acquire does,
    // but need not wait for older Store-Releases.
    bool isAcquirePC() const { return acquire_pc_; }
    void markAcquirePC() { acquire_pc_ = true; }
    // Either kind: no younger access of the thread may read memory before this load (younger_loads_after_acquire of the core).
    bool ordersYoungerAccesses() const { return acquire_ || acquire_pc_; }
    // The Load-Acquire found an older Store-Release not yet visible (acquire_after_release_cycles of the LSQ).
    void markWaitedForRelease() { waited_for_release_ = true; }
    bool waitedForRelease() const { return waited_for_release_; }

    // The physical range [low, high) of the bytes the load read from memory, from the addresses of its answers when the
    // memory interface reports them (physical_address_in_responses of memHierarchy.standardInterface); high is 0 until an
    // answer has arrived. A load that took its bytes from a store records nothing.
    void notePhysicalRead(const uint64_t address, const uint64_t size)
    {
        if ( size == 0 || read_from_store_ ) return;
        if ( phys_read_high_ == 0 || address < phys_read_low_ ) phys_read_low_ = address;
        if ( address + size > phys_read_high_ ) phys_read_high_ = address + size;
    }
    // The load took its bytes from an older store of its thread: its answer carries no address of the memory system.
    void markReadFromStore() { read_from_store_ = true; }
    bool readPhysicalLine(const uint64_t line_address, const uint64_t line_bytes) const
    {
        return phys_read_high_ != 0 && phys_read_low_ < line_address + line_bytes && line_address < phys_read_high_;
    }

    // The bytes the load read (from memory or from a store), recorded as its answers arrive, so that an older load to the
    // same bytes that reads later can compare its values with them (check_load_order of the core: the coherence order of
    // reads of one location). Loads of more than READ_BYTES_MAX bytes are not recorded.
    static constexpr uint64_t READ_BYTES_MAX = 64;
    void noteReadBytes(const uint64_t address, const uint8_t* data, const uint64_t size)
    {
        if ( access_width == 0 || access_width > READ_BYTES_MAX ) return;
        for ( uint64_t i = 0; i < size; ++i ) {
            const uint64_t a = address + i;
            if ( a < access_address || a >= access_address + access_width ) continue;
            read_bytes_[a - access_address] = data[i];
            read_valid_ |= 1ull << (a - access_address);
        }
    }
    // Whether the load holds the byte at address, and its value.
    bool readByte(const uint64_t address, uint8_t* value) const
    {
        if ( address < access_address || address >= access_address + access_width || access_width > READ_BYTES_MAX ) return false;
        if ( ! (read_valid_ & (1ull << (address - access_address))) ) return false;
        *value = read_bytes_[address - access_address];
        return true;
    }
    // The load took its bytes from the store with this memory sequence (0: from memory).
    void     setForwardedFrom(const uint64_t store_sequence) { forwarded_from_ = store_sequence; }
    uint64_t getForwardedFrom() const { return forwarded_from_; }

protected:
    bool shares_load_port_ = false;
    // markAcquire, markAcquirePC.
    bool acquire_ = false;
    bool acquire_pc_ = false;
    // markWaitedForRelease.
    bool waited_for_release_ = false;
    // markReadFromStore.
    bool read_from_store_ = false;
    // notePhysicalRead.
    uint64_t phys_read_low_ = 0;
    uint64_t phys_read_high_ = 0;
    // noteReadBytes, setForwardedFrom.
    uint8_t  read_bytes_[READ_BYTES_MAX] = {};
    uint64_t read_valid_ = 0;
    uint64_t forwarded_from_ = 0;

    uint64_t access_address = 0;
    uint64_t access_width = 0;
    const bool               signed_extend;
    VanadisMemoryTransaction memAccessType;
    const int64_t            offset;
    const uint16_t           load_width;
    VanadisLoadRegisterType  regType;

};

} // namespace Vanadis
} // namespace SST

#endif
