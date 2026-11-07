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

#ifndef _H_VANADIS_OS_SYSCALL_WRITE
#define _H_VANADIS_OS_SYSCALL_WRITE

#include "os/syscall/syscall.h"
#include "os/callev/voscallwrite.h"

namespace SST {
namespace Vanadis {

class VanadisWriteSyscall : public VanadisSyscall {
public:
    VanadisWriteSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallWriteEvent* event )
        : VanadisSyscall( os, coreLink, process, event, "write" ), m_numWritten(0)
    {
        #ifdef VANADIS_BUILD_DEBUG
        m_output->verbose(CALL_INFO, 16, 0, "[syscall-write] -> call is write( %" PRId64 ", 0x%0" PRI_ADDR ", %" PRId64 " )\n",
                            event->getFileDescriptor(), event->getBufferAddress(), event->getBufferCount());
        #endif
        m_emulated = process->getEmulatedFile( event->getFileDescriptor() );
        m_fd = process->getFileDescriptor( event->getFileDescriptor());
        if ( m_emulated ) {
            // An emulated file (pipe, thread name): the whole buffer is read first, then written in one go.
            if ( event->getBufferCount() == 0 ) {
                setReturnSuccess(0);
            } else {
                m_data.resize( event->getBufferCount() );
                readMemory( event->getBufferAddress(), m_data );
            }
        } else if ( -1 == m_fd ) {
            m_output->verbose(CALL_INFO, 16, 0,
                            "[syscall-write] -> file handle %" PRId64
                            " is not currently open, return an error code.\n",
                            event->getFileDescriptor());

            setReturnFail(-LINUX_EINVAL);
        } else if (event->getBufferCount() == 0) {
            setReturnSuccess(0);
        } else {

            // get the length of the first memory access, it cannot span a cacheline
            uint64_t length = vanadis_line_remainder(event->getBufferAddress(),64);
            length = event->getBufferCount() < length ? event->getBufferCount() : length;

            m_data.resize( length );
            readMemory( event->getBufferAddress(), m_data );
        }
    }

    // Writes what is left of the buffer into the emulated file; true when the call finished.
    bool writeEmulated( uint64_t now_ns ) {
        int64_t ret = m_emulated->write( m_data.data() + m_numWritten, m_data.size() - m_numWritten, now_ns );
        if ( ret >= 0 ) {
            m_numWritten += ret;
        }
        if ( m_numWritten == m_data.size() || ( ret < 0 && -EAGAIN != ret ) || ( m_emulated->nonblock() && m_numWritten ) ) {
            if ( ret < 0 && 0 == m_numWritten ) {
                setReturnFail( ret );
            } else {
                setReturnSuccess( m_numWritten );
            }
            return true;
        }
        if ( m_emulated->nonblock() ) {
            setReturnFail( -EAGAIN );
            return true;
        }
        return false;
    }

    bool retry( uint64_t now_ns ) override { return writeEmulated( now_ns ); }

    void memReqIsDone(bool) {

        if ( m_emulated ) {
            if ( ! writeEmulated( m_os->nodeTimeNs() ) ) {
                m_os->blockSyscall( this );
            }
            return;
        }

        int retval = write( m_fd, m_data.data(), m_data.size() );
        assert( retval >= 0 );
        m_numWritten += m_data.size();

        if ( m_numWritten == getEvent<VanadisSyscallWriteEvent*>()->getBufferCount() ) {
            setReturnSuccess( m_numWritten );
        } else {
            size_t length = getEvent<VanadisSyscallWriteEvent*>()->getBufferCount() - m_numWritten;
            if ( length > 64 ) { length = 64; }

            m_data.resize(length);
            readMemory( getEvent<VanadisSyscallWriteEvent*>()->getBufferAddress() + m_numWritten, m_data );
        }
    }

 private:
    std::vector<uint8_t>    m_data;
    int                     m_fd;
    size_t                  m_numWritten;
    std::shared_ptr<OS::EmulatedFile> m_emulated;
};

} // namespace Vanadis
} // namespace SST

#endif
