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

#ifndef _H_VANADIS_SYSCALL_ARGS
#define _H_VANADIS_SYSCALL_ARGS

#include "os/voscallev.h"

namespace SST {
namespace Vanadis {

// A system call whose OS side reads its arguments as the six argument registers (x0 to x5 on ARM64): the calls
// added for the user-mode DPDK environment (pipes, epoll, timer files, sleeps, memory files and the simple identity
// and file-system calls). The operation says which call it is.
class VanadisSyscallArgsEvent : public VanadisSyscallEvent {
public:
    VanadisSyscallArgsEvent() : VanadisSyscallEvent(), op(SYSCALL_OP_UNKNOWN) {}
    VanadisSyscallArgsEvent(uint32_t core, uint32_t thr, VanadisOSBitType bittype, VanadisSyscallOp op,
                            uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
        : VanadisSyscallEvent(core, thr, bittype), op(op)
    {
        args[0] = a0; args[1] = a1; args[2] = a2; args[3] = a3; args[4] = a4; args[5] = a5;
    }

    VanadisSyscallOp getOperation() override { return op; }

    // Argument register k (0 to 5).
    uint64_t arg( int k ) const { return args[k]; }

private:
    void serialize_order(SST::Core::Serialization::serializer& ser) override {
        VanadisSyscallEvent::serialize_order(ser);
        SST_SER(op);
        for ( int k = 0; k < 6; k++ ) {
            SST_SER(args[k]);
        }
    }
    ImplementSerializable(SST::Vanadis::VanadisSyscallArgsEvent);

    VanadisSyscallOp op;
    uint64_t args[6];
};

} // namespace Vanadis
} // namespace SST

#endif
