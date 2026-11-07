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

#ifndef _H_VANADIS_INSTRUCTION
#define _H_VANADIS_INSTRUCTION

#include "decoder/visaopts.h"
#include "inst/regfile.h"
#include "inst/regstack.h"
#include "inst/vinsttype.h"
#include "inst/vtiming.h"
#include "inst/vregfmt.h"

#include <cstring>
#include <map>
#include <sst/core/output.h>
#include <string>
#include <sstream>



namespace SST {
namespace Vanadis {
class VanadisInstruction
{
    public:
        VanadisInstruction(const uint64_t address, const uint32_t hw_thr, const VanadisDecoderOptions* isa_opts,
            const uint16_t c_phys_int_reg_in, const uint16_t c_phys_int_reg_out, const uint16_t c_isa_int_reg_in,
            const uint16_t c_isa_int_reg_out, const uint16_t c_phys_fp_reg_in, const uint16_t c_phys_fp_reg_out,
            const uint16_t c_isa_fp_reg_in, const uint16_t c_isa_fp_reg_out) :
            ins_address(address),
            hw_thread(hw_thr),
            isa_options(isa_opts),
            count_phys_int_reg_in(c_phys_int_reg_in),
            count_phys_int_reg_out(c_phys_int_reg_out),
            count_isa_int_reg_in(c_isa_int_reg_in),
            count_isa_int_reg_out(c_isa_int_reg_out),
            count_phys_fp_reg_in(c_phys_fp_reg_in),
            count_phys_fp_reg_out(c_phys_fp_reg_out),
            count_isa_fp_reg_in(c_isa_fp_reg_in),
            count_isa_fp_reg_out(c_isa_fp_reg_out)
        {
            phys_int_regs_in = (count_phys_int_reg_in > 0) ? new uint16_t[count_phys_int_reg_in] : nullptr;
            std::memset(phys_int_regs_in, 0, count_phys_int_reg_in * sizeof( uint16_t ));

            phys_int_regs_out = (count_phys_int_reg_out > 0) ? new uint16_t[count_phys_int_reg_out] : nullptr;
            std::memset(phys_int_regs_out, 0, count_phys_int_reg_out * sizeof( uint16_t ) );

            isa_int_regs_in = (count_isa_int_reg_in > 0) ? new uint16_t[count_isa_int_reg_in] : nullptr;
            std::memset(isa_int_regs_in, 0, count_isa_int_reg_in * sizeof( uint16_t ));

            isa_int_regs_out = (count_isa_int_reg_out > 0) ? new uint16_t[count_isa_int_reg_out] : nullptr;
            std::memset(isa_int_regs_out, 0, count_isa_int_reg_out * sizeof( uint16_t ) );

            phys_fp_regs_in = (count_phys_fp_reg_in > 0) ? new uint16_t[count_phys_fp_reg_in] : nullptr;
            std::memset(phys_fp_regs_in, 0, count_phys_fp_reg_in * sizeof( uint16_t ));

            phys_fp_regs_out = (count_phys_fp_reg_out > 0) ? new uint16_t[count_phys_fp_reg_out] : nullptr;
            std::memset(phys_fp_regs_out, 0, count_phys_fp_reg_out * sizeof( uint16_t ));

            isa_fp_regs_in = (count_isa_fp_reg_in > 0) ? new uint16_t[count_isa_fp_reg_in] : nullptr;
            std::memset(isa_fp_regs_in, 0, count_isa_fp_reg_in * sizeof( uint16_t ) );

            isa_fp_regs_out = (count_isa_fp_reg_out > 0) ? new uint16_t[count_isa_fp_reg_out] : nullptr;
            std::memset(isa_fp_regs_out, 0, count_isa_fp_reg_out * sizeof( uint16_t ));

            trap_error_           = false;
            has_executed_         = false;
            has_issued_           = false;
            has_registers_assigned_ = false;
            is_serializing_       = false;
            extra_execute_cycles_ = 0;
            memory_sequence_      = 0;
            accumulator_fp_reg_in_ = NO_ACCUMULATOR;
            accumulator_cycles_   = 0;
            accumulator_ready_cycle_ = 0;
            fast_accumulate_      = false;
            prev_alternate_sequence_ = 0;
            accumulator_int_reg_in_ = NO_ACCUMULATOR;
            timing_               = nullptr;
            dynamic_cycles_       = 0;
            issue_cycle_          = 0;
            executed_cycle_       = 0;
            decode_cycle_         = 0;
            rename_cycle_         = 0;
            program_sequence_     = 0;
            end_uop_group_        = false;
            is_front_of_rob_      = false;
            has_rob_slot_         = false;
            sw_thread             = hw_thr;
        }

        virtual ~VanadisInstruction()
        {
            if ( phys_int_regs_in != nullptr ) delete[] phys_int_regs_in;
            if ( phys_int_regs_out != nullptr ) delete[] phys_int_regs_out;
            if ( isa_int_regs_in != nullptr ) delete[] isa_int_regs_in;
            if ( isa_int_regs_out != nullptr ) delete[] isa_int_regs_out;
            if ( phys_fp_regs_in != nullptr ) delete[] phys_fp_regs_in;
            if ( phys_fp_regs_out != nullptr ) delete[] phys_fp_regs_out;
            if ( isa_fp_regs_in != nullptr ) delete[] isa_fp_regs_in;
            if ( isa_fp_regs_out != nullptr ) delete[] isa_fp_regs_out;
        }

        VanadisInstruction(const VanadisInstruction& copy_me) :
            ins_address(copy_me.ins_address),
            hw_thread(copy_me.hw_thread),
            isa_options(copy_me.isa_options),
            count_phys_int_reg_in(copy_me.count_phys_int_reg_in),
            count_phys_int_reg_out(copy_me.count_phys_int_reg_out),
            count_isa_int_reg_in(copy_me.count_isa_int_reg_in),
            count_isa_int_reg_out(copy_me.count_isa_int_reg_out),
            count_phys_fp_reg_in(copy_me.count_phys_fp_reg_in),
            count_phys_fp_reg_out(copy_me.count_phys_fp_reg_out),
            count_isa_fp_reg_in(copy_me.count_isa_fp_reg_in),
            count_isa_fp_reg_out(copy_me.count_isa_fp_reg_out)
        {
            trap_error_           = copy_me.trap_error_;
            has_executed_         = copy_me.has_executed_;
            has_issued_           = copy_me.has_issued_;
            has_registers_assigned_ = copy_me.has_registers_assigned_;
            is_serializing_       = copy_me.is_serializing_;
            extra_execute_cycles_ = copy_me.extra_execute_cycles_;
            memory_sequence_      = copy_me.memory_sequence_;
            accumulator_fp_reg_in_ = copy_me.accumulator_fp_reg_in_;
            accumulator_cycles_   = copy_me.accumulator_cycles_;
            accumulator_ready_cycle_ = copy_me.accumulator_ready_cycle_;
            fast_accumulate_      = copy_me.fast_accumulate_;
            prev_alternate_sequence_ = copy_me.prev_alternate_sequence_;
            accumulator_int_reg_in_ = copy_me.accumulator_int_reg_in_;
            timing_               = copy_me.timing_;
            dynamic_cycles_       = copy_me.dynamic_cycles_;
            issue_cycle_          = copy_me.issue_cycle_;
            executed_cycle_       = copy_me.executed_cycle_;
            decode_cycle_         = copy_me.decode_cycle_;
            rename_cycle_         = copy_me.rename_cycle_;
            program_sequence_     = copy_me.program_sequence_;
            end_uop_group_        = copy_me.end_uop_group_;
            is_front_of_rob_      = false;
            has_rob_slot_         = false;
            sw_thread             = copy_me.sw_thread;

            phys_int_regs_in  = (count_phys_int_reg_in > 0) ? new uint16_t[count_phys_int_reg_in] : nullptr;
            phys_int_regs_out = (count_phys_int_reg_out > 0) ? new uint16_t[count_phys_int_reg_out] : nullptr;

            isa_int_regs_in  = (count_isa_int_reg_in > 0) ? new uint16_t[count_isa_int_reg_in] : nullptr;
            isa_int_regs_out = (count_isa_int_reg_out > 0) ? new uint16_t[count_isa_int_reg_out] : nullptr;

            phys_fp_regs_in  = (count_phys_fp_reg_in > 0) ? new uint16_t[count_phys_fp_reg_in] : nullptr;
            phys_fp_regs_out = (count_phys_fp_reg_out > 0) ? new uint16_t[count_phys_fp_reg_out] : nullptr;

            isa_fp_regs_in  = (count_isa_fp_reg_in > 0) ? new uint16_t[count_isa_fp_reg_in] : nullptr;
            isa_fp_regs_out = (count_isa_fp_reg_out > 0) ? new uint16_t[count_isa_fp_reg_out] : nullptr;

            for ( uint16_t i = 0; i < count_phys_int_reg_in; ++i ) {
                phys_int_regs_in[i] = copy_me.phys_int_regs_in[i];
            }

            for ( uint16_t i = 0; i < count_phys_int_reg_out; ++i ) {
                phys_int_regs_out[i] = copy_me.phys_int_regs_out[i];
            }

            for ( uint16_t i = 0; i < count_isa_int_reg_in; ++i ) {
                isa_int_regs_in[i] = copy_me.isa_int_regs_in[i];
            }

            for ( uint16_t i = 0; i < count_isa_int_reg_out; ++i ) {
                isa_int_regs_out[i] = copy_me.isa_int_regs_out[i];
            }

            for ( uint16_t i = 0; i < count_phys_fp_reg_in; ++i ) {
                phys_fp_regs_in[i] = copy_me.phys_fp_regs_in[i];
            }

            for ( uint16_t i = 0; i < count_phys_fp_reg_out; ++i ) {
                phys_fp_regs_out[i] = copy_me.phys_fp_regs_out[i];
            }

            for ( uint16_t i = 0; i < count_isa_fp_reg_in; ++i ) {
                isa_fp_regs_in[i] = copy_me.isa_fp_regs_in[i];
            }

            for ( uint16_t i = 0; i < count_isa_fp_reg_out; ++i ) {
                isa_fp_regs_out[i] = copy_me.isa_fp_regs_out[i];
            }
        }

        // different
        void writeIntRegs(char* buffer, size_t max_buff_size)
        {
            size_t index_so_far = 0;

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "in: { ");

            if ( count_isa_int_reg_in > 0 ) {
                index_so_far +=
                    snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "%" PRIu16 "", isa_int_regs_in[0]);

                for ( int i = 1; i < count_isa_int_reg_in; ++i ) {
                    index_so_far +=
                        snprintf(&buffer[index_so_far], max_buff_size - index_so_far, ", %" PRIu16 "", isa_int_regs_in[i]);
                }
            }

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, " } -> { ");

            if ( count_phys_int_reg_in > 0 ) {
                index_so_far +=
                    snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "%" PRIu16 "", phys_int_regs_in[0]);

                for ( int i = 1; i < count_phys_int_reg_in; ++i ) {
                    index_so_far +=
                        snprintf(&buffer[index_so_far], max_buff_size - index_so_far, ", %" PRIu16 "", phys_int_regs_in[i]);
                }
            }

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, " } / out: { ");

            if ( count_isa_int_reg_out > 0 ) {
                index_so_far +=
                    snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "%" PRIu16 "", isa_int_regs_out[0]);

                for ( int i = 1; i < count_isa_int_reg_out; ++i ) {
                    index_so_far +=
                        snprintf(&buffer[index_so_far], max_buff_size - index_so_far, ", %" PRIu16 "", isa_int_regs_out[i]);
                }
            }

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, " } -> { ");

            if ( count_phys_int_reg_out > 0 ) {
                index_so_far +=
                    snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "%" PRIu16 "", phys_int_regs_out[0]);

                for ( int i = 1; i < count_phys_int_reg_out; ++i ) {
                    index_so_far += snprintf(
                        &buffer[index_so_far], max_buff_size - index_so_far, ", %" PRIu16 "", phys_int_regs_out[i]);
                }
            }

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, " }");
        }

        // different
        void writeFPRegs(char* buffer, size_t max_buff_size)
        {
            size_t index_so_far = 0;

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "in: { ");

            if ( count_isa_fp_reg_in > 0 ) {
                index_so_far +=
                    snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "%" PRIu16 "", isa_fp_regs_in[0]);

                for ( int i = 1; i < count_isa_fp_reg_in; ++i ) {
                    index_so_far +=
                        snprintf(&buffer[index_so_far], max_buff_size - index_so_far, ", %" PRIu16 "", isa_fp_regs_in[i]);
                }
            }

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, " } -> { ");

            if ( count_phys_fp_reg_in > 0 ) {
                index_so_far +=
                    snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "%" PRIu16 "", phys_fp_regs_in[0]);

                for ( int i = 1; i < count_phys_fp_reg_in; ++i ) {
                    index_so_far +=
                        snprintf(&buffer[index_so_far], max_buff_size - index_so_far, ", %" PRIu16 "", phys_fp_regs_in[i]);
                }
            }

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, " } / out: { ");

            if ( count_isa_fp_reg_out > 0 ) {
                index_so_far +=
                    snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "%" PRIu16 "", isa_fp_regs_out[0]);

                for ( int i = 1; i < count_isa_fp_reg_out; ++i ) {
                    index_so_far +=
                        snprintf(&buffer[index_so_far], max_buff_size - index_so_far, ", %" PRIu16 "", isa_fp_regs_out[i]);
                }
            }

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, " } -> { ");

            if ( count_phys_fp_reg_out > 0 ) {
                index_so_far +=
                    snprintf(&buffer[index_so_far], max_buff_size - index_so_far, "%" PRIu16 "", phys_fp_regs_out[0]);

                for ( int i = 1; i < count_phys_fp_reg_out; ++i ) {
                    index_so_far +=
                        snprintf(&buffer[index_so_far], max_buff_size - index_so_far, ", %" PRIu16 "", phys_fp_regs_out[i]);
                }
            }

            index_so_far += snprintf(&buffer[index_so_far], max_buff_size - index_so_far, " }");
        }


        uint16_t countPhysIntRegIn() const { return count_phys_int_reg_in; }
        uint16_t countPhysIntRegOut() const { return count_phys_int_reg_out; }
        uint16_t countPhysFPRegIn() const { return count_phys_fp_reg_in; }
        uint16_t countPhysFPRegOut() const { return count_phys_fp_reg_out; }


        uint16_t countISAIntRegIn() const { return count_isa_int_reg_in; }
        uint16_t countISAIntRegOut() const { return count_isa_int_reg_out; }
        uint16_t countISAFPRegIn() const { return count_isa_fp_reg_in; }
        uint16_t countISAFPRegOut() const { return count_isa_fp_reg_out; }

        uint16_t getPhysIntRegIn(const uint16_t index) const { return phys_int_regs_in[index]; }
        uint16_t getPhysIntRegOut(const uint16_t index) const { return phys_int_regs_out[index]; }
        uint16_t getISAIntRegIn(const uint16_t index) const { return isa_int_regs_in[index]; }
        uint16_t getISAIntRegOut(const uint16_t index) const { return isa_int_regs_out[index]; }


        uint16_t getPhysFPRegIn(const uint16_t index) const { return phys_fp_regs_in[index]; }
        uint16_t getPhysFPRegOut(const uint16_t index) const { return phys_fp_regs_out[index]; }
        uint16_t getISAFPRegIn(const uint16_t index) const { return isa_fp_regs_in[index]; }
        uint16_t getISAFPRegOut(const uint16_t index) const { return isa_fp_regs_out[index]; }

        void setPhysIntRegIn(const uint16_t index, const uint16_t reg) { phys_int_regs_in[index] = reg; }
        void setPhysIntRegOut(const uint16_t index, const uint16_t reg) { phys_int_regs_out[index] = reg; }
        void setPhysFPRegIn(const uint16_t index, const uint16_t reg) { phys_fp_regs_in[index] = reg; }
        void setPhysFPRegOut(const uint16_t index, const uint16_t reg) { phys_fp_regs_out[index] = reg; }

        virtual VanadisInstruction* clone() = 0;

        void markEndOfMicroOpGroup() { end_uop_group_ = true; }
        bool endsMicroOpGroup() const { return end_uop_group_; }
        bool trapsError() const { return trap_error_; }

        uint64_t getInstructionAddress() const { return ins_address; }
        uint32_t getHWThread() const { return hw_thread; }

        void setSWThread(uint32_t thr) { sw_thread=thr;}
        uint32_t getSWThread() {return sw_thread;}

        virtual const char* getInstCode() const = 0 ;

        virtual void printToBuffer(char* buffer, size_t buffer_size) { snprintf(buffer, buffer_size, "%s", getInstCode()); }

        virtual VanadisFunctionalUnitType getInstFuncType() const = 0;


        virtual void instOp(VanadisRegisterFile* regFile,
                            uint16_t phys_int_regs_out_0, uint16_t phys_int_regs_in_0,
                            uint16_t phys_int_regs_in_1)
        {
            ;
        }

        virtual void instOp(VanadisRegisterFile* regFile,
                                uint16_t phys_int_regs_out_0, uint16_t phys_int_regs_in_0)
        {
            ;
        }

        virtual void scalarExecute(SST::Output* output, VanadisRegisterFile* regFile)
        {
            uint16_t phys_int_regs_out_0 = getPhysIntRegOut(0);
            uint16_t phys_int_regs_in_0 = getPhysIntRegIn(0);
            uint16_t phys_int_regs_in_1 = getPhysIntRegIn(1);
            #ifdef VANADIS_BUILD_DEBUG
            log(output, 16, 65535,phys_int_regs_out_0,phys_int_regs_in_0,phys_int_regs_in_1);
            #endif
            instOp(regFile,phys_int_regs_out_0, phys_int_regs_in_0, phys_int_regs_in_1);
            markExecuted();
        }

        virtual void log(SST::Output* output, int verboselevel, uint16_t sw_thr,
                uint16_t phys_int_regs_out_0,uint16_t phys_int_regs_in_0)
        {
            #ifdef VANADIS_BUILD_DEBUG
            if(output->getVerboseLevel() >= verboselevel) {

                std::ostringstream ss;
                ss << "hw_thr="<<getHWThread()<<" sw_thr=" <<sw_thr;
                ss << " Execute: 0x" << std::hex << getInstructionAddress() << std::dec << " " << getInstCode();
                ss << " phys: out=" <<  phys_int_regs_out_0 << " in=" << phys_int_regs_in_0;
                // ss << " imm=" << imm_value;
                ss << ", isa: out=" <<  isa_int_regs_out[0]  << " in=" << isa_int_regs_in[0];
                output->verbose( CALL_INFO, verboselevel, 0, "%s\n", ss.str().c_str());
            }
            #endif
        }

        virtual void  execute(SST::Output* output, std::vector<VanadisRegisterFile*>& regFiles)
        {
            scalarExecute(output, regFiles[hw_thread]);
        }

        virtual void log(SST::Output* output, int verboselevel, uint16_t sw_thr,
                            uint16_t phys_int_regs_out_0,uint16_t phys_int_regs_in_0,
                                    uint16_t phys_int_regs_in_1)
        {
            #ifdef VANADIS_BUILD_DEBUG
            if(output->getVerboseLevel() >= verboselevel) {
                std::string instcode = getInstCode();
                std::string fpinst = "FP";
                if(instcode.find(fpinst) != std::string::npos )
                {
                    output->verbose(
                    CALL_INFO, verboselevel, 0,
                    "hw_thr=%d sw_thr = %d Execute: 0x%" PRI_ADDR " %s phys: out=%" PRIu16 " in=%" PRIu16 ", %" PRIu16 ", isa: out=%" PRIu16
                    " / in=%" PRIu16 ", %" PRIu16 "\n",
                    getHWThread(),sw_thr, getInstructionAddress(), getInstCode(), phys_int_regs_out_0, phys_int_regs_in_0,
                    phys_int_regs_in_1, isa_fp_regs_out[0], isa_fp_regs_in[0], isa_fp_regs_in[1]);
                }
                else
                output->verbose(
                    CALL_INFO, verboselevel, 0,
                    "hw_thr=%d sw_thr = %d Execute: 0x%" PRI_ADDR " %s phys: out=%" PRIu16 " in=%" PRIu16 ", %" PRIu16 ", isa: out=%" PRIu16
                    " / in=%" PRIu16 ", %" PRIu16 "\n",
                    getHWThread(),sw_thr, getInstructionAddress(), getInstCode(), phys_int_regs_out_0, phys_int_regs_in_0,
                    phys_int_regs_in_1, isa_int_regs_out[0], isa_int_regs_in[0], isa_int_regs_in[1]);
            }
            #endif
        }


        virtual void print(SST::Output* output) { output->verbose(CALL_INFO, 8, 0, "%s", getInstCode()); }

        // Is the instruction predicted (speculation point).
        // for normal instructions this is false
        // but branches and jumps will get predicte
        virtual bool isSpeculated() const { return false; }

        bool completedExecution() const { return has_executed_; }
        bool completedIssue() const { return has_issued_; }

        virtual void markExecuted() { has_executed_ = true; }
        void markIssued() { has_issued_ = true; }

        // The instruction holds its physical registers: at issue, or at dispatch with rename_at_dispatch.
        bool registersAssigned() const { return has_registers_assigned_; }
        void markRegistersAssigned() { has_registers_assigned_ = true; }

        // With rename_at_dispatch a serializing instruction issues at the front of the ROB, and nothing younger issues
        // until it retires.
        bool isSerializing() const { return is_serializing_; }
        void markSerializing() { is_serializing_ = true; }

        // Cycles the instruction takes in its functional unit above the latency of the unit.
        uint16_t getExtraExecuteCycles() const { return extra_execute_cycles_; }
        void setExtraExecuteCycles(const uint16_t cycles) { extra_execute_cycles_ = cycles; }

        // With rename_at_dispatch the program order of a memory operation, from 1; 0 without.
        uint64_t getMemorySequence() const { return memory_sequence_; }
        void setMemorySequence(const uint64_t sequence) { memory_sequence_ = sequence; }

        // A multiply-add: index is the FP input that is the accumulator, and another multiply-add can take this one's result
        // on that input cycles after this one issues, ahead of the full latency of the multiplier.
        static constexpr uint16_t NO_ACCUMULATOR = 0xFFFF;
        void setAccumulatorFPRegIn(const uint16_t index, const uint16_t cycles) { accumulator_fp_reg_in_ = index; accumulator_cycles_ = cycles; }
        uint16_t getAccumulatorFPRegIn() const { return accumulator_fp_reg_in_; }
        uint16_t getAccumulatorCycles() const { return accumulator_cycles_; }
        void setAccumulatorReadyCycle(const uint64_t cycle) { accumulator_ready_cycle_ = cycle; }
        uint64_t getAccumulatorReadyCycle() const { return accumulator_ready_cycle_; }
        // The integer input that is the accumulator, for integer multiply-adds and CRC.
        void setAccumulatorIntRegIn(const uint16_t index) { accumulator_int_reg_in_ = index; }
        uint16_t getAccumulatorIntRegIn() const { return accumulator_int_reg_in_; }
        void setAccumulatorCycles(const uint16_t cycles) { accumulator_cycles_ = cycles; }
        // Whether the micro-op took its accumulator at the accumulator cycles of its producer (accumulator_alternate).
        void setFastAccumulate(const bool fast) { fast_accumulate_ = fast; }
        bool tookFastAccumulate() const { return fast_accumulate_; }
        // accumulator_alternate: the program sequence of the micro-op of such a class before this one, 0 for none.
        void setPrevAlternateSequence(const uint64_t sequence) { prev_alternate_sequence_ = sequence; }
        uint64_t getPrevAlternateSequence() const { return prev_alternate_sequence_; }

        // The timing class of the micro-op, nullptr for the latency of its functional unit.
        const VanadisTimingRecord* getTiming() const { return timing_; }
        void setTiming(const VanadisTimingRecord* record) { timing_ = record; }
        // Latency from issue to the issue of a consumer, with a dynamic latency found at issue.
        uint16_t getTimedLatency() const
        {
            return timing_->dynamic == VanadisTimingDynamic::FIXED ? timing_->latency : dynamic_cycles_;
        }
        // Found at issue for a dynamic class; the registers hold the operands then.
        virtual uint16_t dynamicLatency(VanadisRegisterFile* regFile) const { return timing_->latency; }
        void setDynamicCycles(const uint16_t cycles) { dynamic_cycles_ = cycles; }
        // The cycle the micro-op issued and the cycle it executed, for the timing of its consumers.
        void setIssueCycle(const uint64_t cycle) { issue_cycle_ = cycle; }
        uint64_t getIssueCycle() const { return issue_cycle_; }
        void setExecutedCycle(const uint64_t cycle) { executed_cycle_ = cycle; }
        uint64_t getExecutedCycle() const { return executed_cycle_; }
        // The cycles the micro-op entered the reorder buffer and was renamed, for the timeline trace of the core.
        void setDecodeCycle(const uint64_t cycle) { decode_cycle_ = cycle; }
        uint64_t getDecodeCycle() const { return decode_cycle_; }
        void setRenameCycle(const uint64_t cycle) { rename_cycle_ = cycle; }
        uint64_t getRenameCycle() const { return rename_cycle_; }
        // With rename_at_dispatch the program order of every micro-op, from 1: a flush at a branch removes the larger ones.
        void setProgramSequence(const uint64_t sequence) { program_sequence_ = sequence; }
        uint64_t getProgramSequence() const { return program_sequence_; }

        // With rename_at_dispatch a store issues when its address is known; these inputs are read when it goes to memory.
        virtual bool isStoreDataIntRegIn(const uint16_t index) const { return false; }
        virtual bool isStoreDataFPRegIn(const uint16_t index) const { return false; }

        bool checkFrontOfROB() const { return is_front_of_rob_; }
        void markFrontOfROB() { is_front_of_rob_ = true; }

        bool has_rob_slot_Issued() const { return has_rob_slot_; }
        void markROBSlotIssued() { has_rob_slot_ = true; }

        const VanadisDecoderOptions* getISAOptions() const { return isa_options; }

        void flagError() { trap_error_ = true; }

        virtual bool performIntRegisterRecovery() const { return true; }
        virtual bool performFPRegisterRecovery() const { return true; }

        virtual bool updatesFPFlags() const { return false; }
        virtual void updateFPFlags() {}

        virtual void returnOutRegs( VanadisRegisterStack* int_stack, VanadisRegisterStack* fp_stack )
        {
            for ( auto i = 0; i < countPhysIntRegOut(); i++ ) {
                int_stack->push( getPhysIntRegOut(i) );
            }
            for ( auto i = 0; i < countPhysFPRegOut(); i++ ) {
                fp_stack->push( getPhysFPRegOut(i) );
            }
        }

        uint16_t getNumStores()
        {
            return 0;
        }



    protected:

        const uint64_t ins_address;
        const uint32_t hw_thread;

        uint16_t count_isa_int_reg_in;
        uint16_t count_isa_int_reg_out;
        uint16_t count_isa_fp_reg_in;
        uint16_t count_isa_fp_reg_out;


        uint16_t count_phys_int_reg_in;
        uint16_t count_phys_int_reg_out;
        uint16_t count_phys_fp_reg_in;
        uint16_t count_phys_fp_reg_out;

        bool trap_error_ = false;
        bool has_executed_;
        bool has_issued_;
        bool has_registers_assigned_;
        bool is_serializing_;
        uint16_t extra_execute_cycles_;
        uint64_t memory_sequence_;
        uint16_t accumulator_fp_reg_in_;
        uint16_t accumulator_cycles_;
        uint64_t accumulator_ready_cycle_;
        bool fast_accumulate_;
        uint64_t prev_alternate_sequence_;
        uint16_t accumulator_int_reg_in_;
        const VanadisTimingRecord* timing_;
        uint16_t dynamic_cycles_;
        uint64_t issue_cycle_;
        uint64_t executed_cycle_;
        uint64_t decode_cycle_;
        uint64_t rename_cycle_;
        uint64_t program_sequence_;
        bool end_uop_group_;
        bool is_front_of_rob_;
        bool has_rob_slot_;

        const VanadisDecoderOptions* isa_options;
        uint32_t sw_thread;
        uint16_t* isa_int_regs_in;
        uint16_t* isa_int_regs_out;
        uint16_t* isa_fp_regs_in;
        uint16_t* isa_fp_regs_out;

        uint16_t* phys_int_regs_in;
        uint16_t* phys_int_regs_out;
        uint16_t* phys_fp_regs_in;
        uint16_t* phys_fp_regs_out;


};

} // namespace Vanadis
} // namespace SST

#endif
