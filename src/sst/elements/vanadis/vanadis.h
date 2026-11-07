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

#ifndef _VANADIS_COMPONENT_H
#define _VANADIS_COMPONENT_H

#include "datastruct/cqueue.h"
#include "decoder/vdecoder.h"
#include "inst/isatable.h"
#include "inst/regfile.h"
#include "inst/regstack.h"
#include "inst/vinst.h"
#include "lsq/vlsq.h"
#include "lsq/vbasiclsq.h"
#include "velf/velfinfo.h"
#include "vfpflags.h"
#include "vfuncunit.h"
#include "rocc/vroccinterface.h"
#include "rocc/vbasicrocc.h"

#include "os/vgetthreadstate.h"
#include "os/vdumpregsreq.h"
#include "os/vcheckpointreq.h"

#include <array>
#include <limits>
#include <set>
#include <sst/core/component.h>
#include <sst/core/interfaces/stdMem.h>
#include <sst/core/link.h>
#include <sst/core/output.h>
#include <sst/core/params.h>


namespace SST {
namespace Vanadis {

class VanadisInsCacheLoadRecord
{
public:
    VanadisInsCacheLoadRecord(const uint32_t thr, const uint64_t addrStart, const uint16_t len) :
        hw_thr(thr),
        addr(addrStart),
        width(len),
        hasPayload(false)
    {

        data = new uint8_t[len];

        for ( uint16_t i = 0; i < width; ++i ) {
            data[i] = 0;
        }
    }

    uint64_t getAddress() const { return addr; }
    uint32_t getHWThread() const { return hw_thr; }
    uint16_t getWidth() const { return width; }
    bool     hasData() const { return hasPayload; }
    uint8_t* getPayload() { return data; }

    void setPayload(uint8_t* ptr)
    {
        hasPayload = true;

        for ( uint16_t i = 0; i < width; ++i ) {
            data[i] = ptr[i];
        }
    }

private:
    bool           hasPayload;
    uint8_t*       data;
    const uint64_t addr;
    const uint16_t width;
    const uint32_t hw_thr;
};

class VanadisCore : public SST::Component
{

public:
    SST_ELI_REGISTER_COMPONENT(
        VanadisCore,
#ifdef VANADIS_BUILD_DEBUG
        "vanadisdbg",
#else
        "vanadis",
#endif
        "core",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Vanadis Processor Component",
        COMPONENT_CATEGORY_PROCESSOR
    )

    // This is the old name, allow use so that existing config files don't break
    SST_ELI_REGISTER_ALIAS("dbg_VanadisCPU")

    SST_ELI_DOCUMENT_PARAMS(
        { "verbose", "Set the level of output verbosity, 0 is no output, higher "
                     "is more output", "0" },
        { "dbg_mask", "Mask for output. Default is to not mask anything out (0) and defer to 'verbose'.", "0"},
        { "start_verbose_when_issue_address", "Set verbose to 0 until the specified instruction "
                                        "address is issued, then set to 'verbose' parameter", ""},
        { "stop_verbose_when_retire_address", "When the specified instruction "
                                        "address is retired, set verbose to 0", ""},
        { "pause_when_retire_address", "If specified, the simulation will stop when this address is retired.", "0"},
        { "pipeline_trace_file", "If specified, a trace of the pipeline activity will be generated to this file.", ""},
        { "max_cycle", "Maximum number of cycles to execute. The core will halt after this many cycles." , "std::numeric_limits<uint64_t>::max()"},
        { "node_id", "Identifier for the node this core belongs to. Each node in the system needs a unique ID between 0 and (number of nodes) - 1. Used to tag output.", "0"},
        { "core_id", "Identifier for this core. Each core in the system needs a unique ID between 0 and (number of cores) - 1.", 0 },
        { "hardware_threads", "Number of hardware threads in this core", "1" },
        { "clock", "Core clock frequency", "1GHz" },
        { "reorder_slots", "Number of slots in the reorder buffer", "64"},
        { "misprediction_extra_cycles", "Cycles decode waits after a branch misprediction, above the time the flush takes", "0"},
        { "renames_per_cycle", "With rename_at_dispatch, micro-ops renamed per cycle", "decodes_per_cycle"},
        { "dividers_block", "A divider takes no new instruction until the current one has executed", "false"},
        { "fp_dividers_block", "An FP divider takes no new instruction until the current one has executed; a timing class's issue interval can pipeline it instead", "dividers_block"},
        { "reorder_instructions", "Instructions the reorder buffer holds, counted once whatever their micro-ops; 0 counts only the micro-op slots (reorder_slots)", "0"},
        { "recover_at_execute", "With rename_at_dispatch, a mispredicted branch squashes the younger instructions when it executes, and the older ones go on; false squashes at retire", "false"},
        { "rename_at_dispatch", "Rename registers in program order when an instruction enters the reorder buffer, and issue a consumer when its producer has executed. False renames at issue and waits for the producer to retire. For a decoder that never writes its zero register", "false"},
        { "physical_integer_registers", "Number of physical integer registers per hardware thread", "128" },
        { "physical_fp_registers", "Number of physical floating point registers per hardware thread", "128" },
        { "integer_arith_units", "Number of integer arithemetic units", "2" },
        { "integer_arith_cycles", "Cycles per instruction for integer arithmetic", "2" },
        { "integer_div_units", "Number of integer division units", "1" },
        { "integer_div_cycles", "Cycles per instruction for integer division", "4" },
        { "fp_arith_units", "Number of floating point arithmetic units", "2" },
        { "fp_arith_cycles", "Cycles per floating point arithmetic", "8" },
        { "fp_div_units", "Number of floating point division units", "1" },
        { "fp_div_cycles", "Cycles per floating point division", "80" },
        { "branch_units", "Number of branch units", "1" },
        { "branch_unit_cycles", "Cycles per branch", "int_arith_cycles"},
        { "issues_per_cycle", "Number of instruction issues per cycle", "2" },
        { "integer_arith_pipe_order", "Order in which issue tries the pipes of the integer unit, by pipe index (the bits of a timing class's pipe mask); empty is index order", "[]" },
        { "fp_arith_pipe_order", "Order in which issue tries the pipes of the floating-point unit, by pipe index; empty is index order", "[]" },
        { "branch_pipe_order", "Order in which issue tries the pipes of the branch unit, by pipe index; empty is index order", "[]" },
        { "fetches_per_cycle", "Number of instruction fetches per cycle", "2" },
        { "retires_per_cycle", "Number of instruction retires per cycle", "2" },
        { "decodes_per_cycle", "Number of instruction decodes per cycle", "2" },
        { "dcache_line_width", "Width of a line for the data cache, in bytes. (Currently not used but may be in the future).", "64"},
        { "icache_line_width", "Width of a line for the instruction cache, in bytes", "64"},
        { "print_retire_tables", "Print registers during retirement step (default is yes)", "true" },
        { "print_issue_tables", "Print registers during issue step (default is yes)", "true" },
        { "print_int_reg", "Print integer registers true/false, auto set to true if verbose > 16", "false" },
        { "print_fp_reg", "Print floating-point registers true/false, auto set to "
                          "true if verbose > 16", "false" },
        { "print_rob", "Print reorder buffer state during issue and retire", "true"},
        { "enable_simt", "Implement SIMT pipeline for multithread kernels", "false"}  )

    SST_ELI_DOCUMENT_STATISTICS(
        { "cycles", "Number of cycles the core executed", "cycles", 1 },
        { "syscall-cycles",
          "Number of cycles spent waiting on execution of SYSCALL in OS "
          "components",
          "cycles", 1 },
        { "rob_slots_in_use", "Number of micro-ops in the ROB each cycle", "instructions", 1 },
        { "rob_cleared_entries", "Number of micro-ops that are cleared during a pipeline clear", "instructions", 1 },
        { "instructions_issued", "Number of instructions issued", "instructions", 1 },
        { "instructions_retired", "Number of instructions retired", "instructions", 1 },
        { "instructions_decoded", "Number of instructions decoded", "instructions", 1 },
        { "branch_mispredicts", "Number of retired branches which were mis-predicted", "instructions", 1 },
        { "memory_order_violations", "Loads that read before an older store knew its address", "events", 1 },
        { "branches", "Number of retired branches", "instructions", 1 },
        { "loads_issued", "Number of load instructions issued to the LSQ", "instructions", 1 },
        { "stores_issued", "Number of store instructions issued to the LSQ", "instructions", 1 },
        { "phys_int_reg_in_use", "Number of physical integer registers that are in use each cycle", "registers", 1 },
        { "phys_fp_reg_in_use", "Number of physical floating point registers than are in use each cycle", "registers",
          1 })

    SST_ELI_DOCUMENT_PORTS({ "icache_link", "Connects the CPU to the instruction cache", {} },
                           { "dcache_link", "Connects the CPU to the data cache", {} },
                           { "os_link", "Connects this handler to the main operating system of the node", {}} )

    // Optional since there is nothing to document
    SST_ELI_DOCUMENT_SUBCOMPONENT_SLOTS(
        { "lsq", "Load-Store Queue for Memory Access", "SST::Vanadis::VanadisLoadStoreQueue" },
        { "decoder", "Decoder to use", "SST::Vanadis::VanadisDecoder" },
        { "rocc", "RoCC accelerator interface(s), optional", "SST::Vanadis::VanadisRoCCInterface" },
        { "mem_interface_inst", "Interface to memory system for instructions", "SST::Interfaces::StandardMem" },
    )

    VanadisCore(SST::ComponentId_t id, SST::Params& params);
    ~VanadisCore();

    void init(unsigned int phase) override;

    void setup() override;
    void finish() override;

    void printStatus(SST::Output& output) override;

    //    void handleIncomingDataCacheEvent( StandardMem::Request* ev );
    void handleIncomingInstCacheEvent(StandardMem::Request* ev);
    void recvOSEvent(SST::Event* ev);

    void handleMisspeculate(const uint32_t hw_thr, const uint64_t new_ip);
    void clearROBMisspeculate(const uint32_t hw_thr);

    void clearFuncUnit(const uint32_t hw_thr, std::vector<VanadisFunctionalUnit*>& unit);

    void syscallReturn(uint32_t thr);
    void setHalt(uint32_t thr, int64_t halt_code);
    void startThread(int thr, uint64_t stackStart, uint64_t instructionPointer );
    void startThreadFork( VanadisStartThreadForkReq* req );
    void startThreadClone( VanadisStartThreadCloneReq* req );
    void startThreadClone3( VanadisStartThreadClone3Req* req );
    void getThreadState( VanadisGetThreadStateReq* req );
    void dumpRegs( VanadisDumpRegsReq* req );

private:
    VanadisCore();                        // for serialization only
    VanadisCore(const VanadisCore&); // do not implement
    void operator=(const VanadisCore&);   // do not implement

    virtual bool tick(SST::Cycle_t);

    void resetRegisterUseTemps(const int hw_thr, const uint16_t i_reg, const uint16_t f_reg);

    int assignRegistersToInstruction(
        const uint16_t int_reg_count, const uint16_t fp_reg_count, VanadisInstruction* ins,
        VanadisRegisterStack* int_regs, VanadisRegisterStack* fp_regs, VanadisISATable* isa_table);

    int checkInstructionResources(
        VanadisInstruction* ins, VanadisRegisterStack* int_regs, VanadisRegisterStack* fp_regs,
        VanadisISATable* isa_table);

    int recoverRetiredRegisters(
        VanadisInstruction* ins, VanadisRegisterStack* int_regs, VanadisRegisterStack* fp_regs,
        VanadisISATable* issue_isa_table, VanadisISATable* retire_isa_table);

    int recoverRetiredRegisters(
        VanadisInstruction* ins, VanadisRegisterStack* int_regs, VanadisRegisterStack* fp_regs,
        VanadisISATable* issue_isa_table, VanadisISATable* retire_isa_table, uint16_t sw_thr);

    void performFetch(const uint64_t cycle);
    void performDecode(const uint64_t cycle);

    // rename_at_dispatch: rename the instructions that entered the reorder buffer, oldest first, up to renames_per_cycle.
    void performRename(const uint32_t hw_thr);
    // rename_at_dispatch: issue the oldest renamed instruction that can issue. Returns 0 if one issued, otherwise 1.
    int performIssueRenamed(const uint64_t cycle, const int hwThr, uint32_t& rob_start, int& unallocated_memory_op_seen);
    // rename_at_dispatch: every input the instruction reads at issue has its value; include_store_data adds store data.
    bool inputValuesReady(const VanadisInstruction* ins, const bool include_store_data) const;
    // A consumer may take this producer's result as its accumulator before the producer has executed.
    bool accumulatorBypassed(const VanadisInstruction* ins, const VanadisInstruction* producer) const;
    // A vector producer and consumer in different forwarding regions take one more cycle.
    bool forwardingPenalty(const VanadisInstruction* producer, const VanadisInstruction* consumer) const;
    // recover_at_execute: squashes the instructions younger than the oldest mispredicted branch that has executed.
    void recoverMispredictions(const uint64_t cycle, const uint32_t thr);
    void squashYounger(const uint64_t cycle, const uint32_t thr, VanadisSpeculatedInstruction* branch);
    void clearFuncUnitYounger(const uint32_t hw_thr, const uint64_t sequence, std::vector<VanadisFunctionalUnit*>& unit);
    // Reorders a unit's pipes by the order a parameter gives.
    void orderPipes(SST::Params& params, const char* key, std::vector<VanadisFunctionalUnit*>& units);
    // rename_at_dispatch: the instruction no longer produces its output registers (it retired or was squashed).
    void clearRegisterProducers(const VanadisInstruction* ins);
    int  performIssue(const uint64_t cycle, int hwThr, uint32_t& rob_start, int& unallocated_memory_op_seen);
    void performExecute(const uint64_t cycle);
    int  performRetire(int rob_num, VanadisCircularQueue<VanadisInstruction*>* rob, const uint64_t cycle);
    int  allocateFunctionalUnit(VanadisInstruction* ins);
    bool mapInstructiontoFunctionalUnit(VanadisInstruction* ins, std::vector<VanadisFunctionalUnit*>& functional_units);
    void printRob(int rob_num, VanadisCircularQueue<VanadisInstruction*>* rob);

    bool checkVerboseAddr( uint64_t addr ) {
        for ( auto& it : start_verbose_when_issue_address ) {
            if ( it == addr ) return true;
        }
        return false;
    }

    void setVerboseWhenIssueAddress( std::string addrs ) {
        while ( ! addrs.empty() ) {
            auto pos = addrs.find(',');
            std::string addr;
            if ( pos == std::string::npos ) {
                addr = addrs;
                addrs.clear();
            } else  {
                addr = addrs.substr(0,pos);
                addrs = addrs.substr(pos+1);
            }
            start_verbose_when_issue_address.push_back(  strtol( addr.c_str(), NULL , 16 ) );
        }
    }

    void resetHwThread(uint32_t thr);

    SST::Output* output = nullptr;

    uint16_t core_id;
    uint64_t current_cycle;
    uint64_t max_cycle;
    uint32_t hw_threads;

    uint32_t fetches_per_cycle;
    uint32_t decodes_per_cycle;
    uint32_t issues_per_cycle;
    uint32_t retires_per_cycle;

    uint32_t m_curRetireHwThread;
    uint32_t m_curIssueHwThread;

    std::vector<VanadisCircularQueue<VanadisInstruction*>*> rob;
    std::vector<VanadisCircularQueue<VanadisInstruction*>*> v_warp_rob;
    std::vector<VanadisDecoder*>                            thread_decoders;
    std::vector<const VanadisDecoderOptions*>               isa_options;

    std::vector<VanadisFunctionalUnit*> fu_int_arith;
    std::vector<VanadisFunctionalUnit*> fu_int_div;
    std::vector<VanadisFunctionalUnit*> fu_branch;
    std::vector<VanadisFunctionalUnit*> fu_fp_arith;
    std::vector<VanadisFunctionalUnit*> fu_fp_div;

    std::vector<VanadisRegisterFile*>  register_files;
    // rename_at_dispatch: the instruction in the reorder buffer that writes each physical register, nullptr when the
    // value is in place; and the number of entries at the front of each reorder buffer that are renamed.
    bool rename_at_dispatch = false;
    std::vector<VanadisInstruction*> int_phys_reg_producer;
    std::vector<VanadisInstruction*> fp_phys_reg_producer;
    std::vector<size_t> renamed_in_rob;
    // rename_at_dispatch: the program order of memory operations; the load that read before an older store knew its
    // address, whose instruction is fetched again when it reaches the front of the reorder buffer; and the loads that
    // did this, which wait for older stores from then on, until the table is cleared.
    std::vector<uint64_t> next_memory_sequence;
    std::vector<uint64_t> violating_load_address;
    std::vector<uint64_t> violating_load_sequence;
    std::vector<uint8_t> has_order_violation;
    std::vector<uint64_t> last_retired_address;
    std::vector<uint8_t> load_waits_for_stores;
    static constexpr uint32_t LOAD_WAIT_TABLE_ENTRIES = 4096;
    static constexpr uint64_t LOAD_WAIT_CLEAR_CYCLES = 1048576;
    void onStoreAddressKnown(const VanadisInstruction* store, const uint64_t address, const uint64_t width);
    bool loadWaitsForStores(const VanadisInstruction* load) const;

    // rename_at_dispatch: what older memory operations have not issued this cycle.
    static constexpr int UNISSUED_LOAD = 1;
    static constexpr int UNISSUED_STORE = 2;
    static constexpr int UNISSUED_ORDERED = 4;

    uint64_t issue_stage_cycle = 0;
    uint32_t renames_per_cycle = 0;
    uint64_t misprediction_extra_cycles = 0;
    std::vector<uint64_t> decode_resume_cycle;
    // recover_at_execute: the branches of each thread that have issued and are not resolved yet, and the next program
    // sequence number.
    bool recover_at_execute = false;
    uint32_t rob_instruction_limit = 0;
    std::vector<std::vector<VanadisInstruction*>> unresolved_branches;
    std::vector<uint64_t> next_program_sequence;

    VanadisRegisterStack* int_register_stack = nullptr;
    VanadisRegisterStack* fp_register_stack = nullptr;

    std::vector<VanadisISATable*> issue_isa_tables;
    std::vector<VanadisISATable*> retire_isa_tables;

    std::vector<uint8_t*> tmp_not_issued_int_reg_read;
    std::vector<uint8_t*> tmp_int_reg_write;
    std::vector<uint8_t*> tmp_not_issued_fp_reg_read;
    std::vector<uint8_t*> tmp_fp_reg_write;

    std::list<VanadisInsCacheLoadRecord*>* icache_load_records = nullptr;

    VanadisLoadStoreQueue* lsq = nullptr;
    StandardMem*           memInstInterface = nullptr;

    std::vector<VanadisRoCCInterface*> roccs_;
    std::vector<std::deque<VanadisInstruction*>> rocc_queues_;

    uint32_t decode_start_thread_ = 0;

    bool* halted_masks = nullptr;
    bool  print_int_reg;
    bool  print_fp_reg;
    bool  print_issue_tables;
    bool  print_retire_tables;
    bool  print_rob;
    bool enable_simt; //for future use

    // Tracing, debug, etc.
    #ifdef VANADIS_BUILD_DEBUG
    char*    inst_print_buffer_ = nullptr;
    #endif
    FILE*    pipeline_trace_file_ = nullptr;

    uint64_t dCacheLineWidth;
    uint64_t iCacheLineWidth;

    TimeConverter           clock_tc_;
    Clock::HandlerBase*     clock_handler_ = nullptr;

    Statistic<uint64_t>* stat_ins_retired = nullptr;
    Statistic<uint64_t>* stat_ins_decoded = nullptr;
    Statistic<uint64_t>* stat_ins_issued = nullptr;
    Statistic<uint64_t>* stat_loads_issued = nullptr;
    Statistic<uint64_t>* stat_stores_issued = nullptr;
    Statistic<uint64_t>* stat_branch_mispredicts = nullptr;
    Statistic<uint64_t>* stat_order_violations = nullptr;
    Statistic<uint64_t>* stat_branches = nullptr;
    Statistic<uint64_t>* stat_cycles = nullptr;
    Statistic<uint64_t>* stat_rob_entries = nullptr;
    Statistic<uint64_t>* stat_rob_cleared_entries = nullptr;
    Statistic<uint64_t>* stat_syscall_cycles = nullptr;
    Statistic<uint64_t>* stat_int_phys_regs_in_use = nullptr;
    Statistic<uint64_t>* stat_fp_phys_regs_in_use = nullptr;

    uint32_t ins_issued_this_cycle;
    uint32_t ins_retired_this_cycle;
    uint32_t ins_decoded_this_cycle;

    uint64_t pause_on_retire_address;
    std::deque<uint64_t> start_verbose_when_issue_address;
    uint64_t stop_verbose_when_retire_address;

    std::vector<VanadisFloatingPointFlags*> fp_flags;
    SST::Link* os_link = nullptr;

    bool* m_checkpointing = nullptr;
    std::string m_checkpointDir;
    enum { NO_CHECKPOINT, CHECKPOINT_LOAD, CHECKPOINT_SAVE } m_checkpoint;
    void checkpoint(FILE*);
    void checkpointLoad(FILE*);
};

} // namespace Vanadis
} // namespace SST

#endif /* _VANADIS_COMPONENT_H */
