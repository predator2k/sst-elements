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

#ifndef _H_VANADIS_FUNCTIONAL_UNIT
#define _H_VANADIS_FUNCTIONAL_UNIT

#include <cinttypes>
#include <climits>
#include <cstdint>
#include <deque>

#include "inst/vinst.h"


namespace SST {
namespace Vanadis {

class VanadisFunctionalUnitInsRecord {
public:
    VanadisFunctionalUnitInsRecord(VanadisInstruction* base_ins, uint16_t cycles)
        : ins(base_ins), cycles_left(cycles) {}
    ~VanadisFunctionalUnitInsRecord() {}

    uint16_t getCycles() const { return cycles_left; }

    bool readyToExecute() const { return (0 == cycles_left); }

    void tick() { cycles_left = (cycles_left > 0) ? cycles_left - 1 : 0; }

    uint32_t getHardwareThread() const { return ins->getHWThread(); }

    VanadisInstruction* getInstruction() { return ins; }

private:
    VanadisInstruction* ins;
    uint16_t cycles_left;
};

class VanadisFunctionalUnit {

public:
    VanadisFunctionalUnit(uint16_t id, VanadisFunctionalUnitType unit_type, uint16_t lat, bool blocks_while_busy = false,
        uint16_t pipe_index = 0)
        : fu_id(id), fu_type(unit_type), latency(lat), accept_this_cycle(true), blocking(blocks_while_busy), pipe(pipe_index) {
    }

    ~VanadisFunctionalUnit() {
        for (auto q_itr = pending_execute.begin(); q_itr != pending_execute.end(); q_itr++) {
            delete (*q_itr);
        }
    }

    VanadisFunctionalUnitType getType() const { return fu_type; }

    bool isInstructionSlotFree() const { return accept_this_cycle && 0 == busy_cycles && ! (blocking && ! pending_execute.empty()); }

    // The pipe can take this micro-op: a timing class names the pipes that can.
    bool canTake(const VanadisInstruction* ins) const
    {
        const VanadisTimingRecord* timing = ins->getTiming();
        return isInstructionSlotFree() && ( nullptr == timing || 0 == timing->pipe_mask || ( (timing->pipe_mask >> pipe) & 1 ) );
    }

    // The index of the pipe within its unit, the bit the pipe masks of the timing classes use.
    uint16_t getPipe() const { return pipe; }

    void insertInstruction(VanadisInstruction* ins) {
        //assert(accept_this_cycle == true);
        const VanadisTimingRecord* timing = ins->getTiming();
        // A unit with latency L executes L + 1 ticks after the issue; a timing class gives the issue-to-issue latency.
        const uint16_t cycles = nullptr == timing ? latency + ins->getExtraExecuteCycles()
                                                  : ( ins->getTimedLatency() > 0 ? ins->getTimedLatency() - 1 : 0 );
        pending_execute.push_back(new VanadisFunctionalUnitInsRecord(ins, cycles));
        accept_this_cycle = false;
        // The pipe takes nothing in the issue_interval - 1 cycles after this one; the count drops at each tick before issue.
        busy_cycles = nullptr == timing || timing->issue_interval <= 1 ? 0 : timing->issue_interval;
    }

    uint16_t getUnitID() const { return fu_id; }

    void tick(const uint64_t cycle, SST::Output* output, std::vector<VanadisRegisterFile*>& regFile) {
        int k_in=0;
        for(auto q_itr = pending_execute.begin(); q_itr != pending_execute.end();) {
            VanadisFunctionalUnitInsRecord* q_item = (*q_itr);

            if(q_item->readyToExecute())
            {
                VanadisInstruction* inner_ins = q_item->getInstruction();
                inner_ins->execute(output, regFile);
                inner_ins->setExecutedCycle(cycle);

                if(LIKELY(inner_ins->completedExecution()))
                {
                    // Delete the record entry for functional unit if the instruction marked itself executed
                    delete q_item;

                    // ready to execute, remove from pending queue
                    q_itr = pending_execute.erase(q_itr);
                }
                else
                {
                    q_itr++;
                }
            }
            else
            {
                q_item->tick();
                q_itr++;
            }
        }

        accept_this_cycle = true;
        if ( busy_cycles > 0 ) busy_cycles--;
    }

    void clearByHWThreadID(SST::Output* output, const uint16_t hw_thr) {
        #ifdef VANADIS_BUILD_DEBUG
        output->verbose(CALL_INFO, 16, 0, "-> Function Unit, clearing by hardware thread %" PRIu32 "...\n", hw_thr);
        #endif

        busy_cycles = 0;
        for (auto q_itr = pending_execute.begin(); q_itr != pending_execute.end();) {
            // if we get a hardware thread match, remove and carry out
            if ((*q_itr)->getHardwareThread() == hw_thr) {
                delete (*q_itr);
                q_itr = pending_execute.erase(q_itr);
            } else {
                q_itr++;
            }
        }
    }

    // Removes the micro-ops of a thread younger than a resolved branch (program sequence above sequence).
    void clearYounger(const uint16_t hw_thr, const uint64_t sequence) {
        for (auto q_itr = pending_execute.begin(); q_itr != pending_execute.end();) {
            if ((*q_itr)->getHardwareThread() == hw_thr && (*q_itr)->getInstruction()->getProgramSequence() > sequence) {
                delete (*q_itr);
                q_itr = pending_execute.erase(q_itr);
            } else {
                q_itr++;
            }
        }
        if ( pending_execute.empty() ) busy_cycles = 0;
    }

    void print(SST::Output* output) {
        uint16_t index = 0;

        for (auto q_itr = pending_execute.begin(); q_itr != pending_execute.end(); q_itr++) {
            VanadisFunctionalUnitInsRecord* q_front = pending_execute.front();
            output->verbose(CALL_INFO, 16, 0, "----> func-unit: %" PRIu16 " %" PRIu16 " entries / entry: %" PRIu16 " / %s / 0x%" PRI_ADDR " / cycles: %" PRIu16 " out of %" PRIu16 "\n",
                fu_id, (uint16_t) pending_execute.size(), index++, q_front->getInstruction()->getInstCode(),
                q_front->getInstruction()->getInstructionAddress(), q_front->getCycles(), latency);
        }
    }

private:
    std::deque<VanadisFunctionalUnitInsRecord*> pending_execute;

    const uint16_t latency;
    VanadisFunctionalUnitType fu_type;
    const uint16_t fu_id;
    bool accept_this_cycle;
    // The unit takes no new instruction until the current one has executed, as a divider does.
    bool blocking;
    // The index of the pipe within its unit.
    uint16_t pipe;
    // Cycles before the pipe takes another micro-op, from the issue interval of the last one it took.
    uint16_t busy_cycles = 0;
};

} // namespace Vanadis
} // namespace SST

#endif
