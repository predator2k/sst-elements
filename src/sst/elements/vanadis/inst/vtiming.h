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

#ifndef _H_VANADIS_INST_TIMING
#define _H_VANADIS_INST_TIMING

#include <cstdint>

namespace SST {
namespace Vanadis {

// How the latency of a micro-op is found when it issues.
enum class VanadisTimingDynamic : uint8_t {
    FIXED,          // the latency of the record
    INTEGER_DIVIDE  // an iterative divider: the bit lengths of the operands set the latency
};

// The timing of one class of micro-ops, as the decoder's timing table gives it. A micro-op without a record keeps the
// latency of its functional unit (the upstream behaviour).
struct VanadisTimingRecord
{
    // Cycles from the issue of the micro-op to the issue of a dependent one; 0 executes the micro-op at issue.
    uint16_t latency = 1;
    // Pipes of the functional unit that can take the micro-op, one bit per pipe index; 0 is any pipe.
    uint32_t pipe_mask = 0;
    // Cycles a pipe that took the micro-op takes no other micro-op of the unit; 1 is fully pipelined.
    uint16_t issue_interval = 1;
    // Cycles after the issue of the micro-op at which a micro-op that takes its result as an accumulator can issue; 0 is no
    // late forwarding.
    uint16_t accumulator_cycles = 0;
    // Forwarding regions of a vector-pipe micro-op as a producer and as a consumer, one bit per region; a producer and a
    // consumer that share no region take one more cycle, and a consumer of region 0 takes none.
    uint8_t region_out = 0;
    uint8_t region_in = 0;
    // The micro-op runs in a vector pipe, where the forwarding regions apply.
    bool vector_pipe = false;
    VanadisTimingDynamic dynamic = VanadisTimingDynamic::FIXED;
    // For INTEGER_DIVIDE: dividend bits retired per cycle above the base latency.
    uint16_t divide_bits_per_cycle = 1;
};

} // namespace Vanadis
} // namespace SST

#endif
