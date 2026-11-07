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

#ifndef _H_VANADIS_OS_SYSCALL_READ
#define _H_VANADIS_OS_SYSCALL_READ

#include "os/syscall/syscall.h"
#include "os/callev/voscallread.h"

namespace SST {
namespace Vanadis {

class VanadisReadSyscall : public VanadisSyscall {
public:
    VanadisReadSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallReadEvent* event  )
        : VanadisSyscall( os, coreLink, process, event, "read" ), m_numRead(0), m_eof(false)
    {
        #ifdef VANADIS_BUILD_DEBUG
        m_output->verbose(CALL_INFO, 16, 0, "-> call is read( %" PRId64 ", 0x%0" PRI_ADDR ", %" PRId64 " )\n",
                            event->getFileDescriptor(), event->getBufferAddress(), event->getBufferCount());
        #endif
        m_emulated = process->getEmulatedFile( event->getFileDescriptor() );
        m_fd = process->getFileDescriptor( event->getFileDescriptor());
        if ( m_emulated ) {
            if ( event->getBufferCount() == 0 ) {
                setReturnSuccess(0);
            } else if ( ! readEmulated( os->nodeTimeNs() ) ) {
                os->blockSyscall( this );
            }
        } else if (-1 == m_fd ) {
            m_output->verbose(CALL_INFO, 16, 0,
                                "[syscall-read] -> file handle %" PRId64
                                " is not currently open, return an error code.\n",
                                event->getFileDescriptor());

            setReturnFail( -LINUX_EINVAL);
        } else if (event->getBufferCount() == 0) {
            setReturnSuccess(0);
        } else {
            // get the length of the first memory access, it cannot span a cacheline
            uint64_t length = vanadis_line_remainder(event->getBufferAddress(),64);
            length = event->getBufferCount() < length ? event->getBufferCount() : length;
            readChunk( length );
        }
    }

    // Reads from the emulated file what it has now (at most the count) and writes it to the buffer; false when the
    // call has to wait (nothing to read, the file is blocking).
    bool readEmulated( uint64_t now_ns ) {
        m_data.resize( getEvent<VanadisSyscallReadEvent*>()->getBufferCount() );
        int64_t ret = m_emulated->read( m_data.data(), m_data.size(), now_ns );
        if ( -EAGAIN == ret && ! m_emulated->nonblock() ) {
            return false;
        }
        if ( ret <= 0 ) {
            if ( ret < 0 ) {
                setReturnFail( ret );
            } else {
                setReturnSuccess( 0 );
            }
            return true;
        }
        m_data.resize( ret );
        m_numRead = ret;
        writeMemory( getEvent<VanadisSyscallReadEvent*>()->getBufferAddress(), m_data );
        return true;
    }

    bool retry( uint64_t now_ns ) override { return readEmulated( now_ns ); }
    // A timer file or a stdin that is not ready yet becomes readable at a known time.
    uint64_t wakeAtNs( uint64_t now_ns ) override { return m_emulated ? m_emulated->readyAtNs( now_ns ) : 0; }

    // The bytes of an emulated read were written to the buffer.
    void memReqIsDone(bool) override {
        if ( m_emulated ) {
            setReturnSuccess( m_numRead );
        }
    }

    void readChunk(size_t length) {

        m_data.resize(length);
        ssize_t retval = read( m_fd, m_data.data(), length );
        #ifdef VANADIS_BUILD_DEBUG
        m_output->verbose(CALL_INFO, 16, 0,"SST %zu = read( %d, %zu)\n",retval,m_fd,length);
        #endif
        assert(retval>=0);

        if ( retval ) {
            m_data.resize( retval );

            writeMemory( getEvent<VanadisSyscallReadEvent*>()->getBufferAddress() + m_numRead, m_data );

            m_numRead += retval;

            if ( retval < length ) {
                #ifdef VANADIS_BUILD_DEBUG
                m_output->verbose(CALL_INFO, 16, 0,"read EOF\n");
                #endif
                m_eof = true;
            }
        } else {
            setReturnSuccess( m_numRead );
        }
    }

    bool handleMemResp( StandardMem::Request* req ) {
        delete req; // we don't use the request
        if ( m_emulated ) {
            return false;
        }

        if ( m_eof || m_numRead == getEvent<VanadisSyscallReadEvent*>()->getBufferCount() ) {
            setReturnSuccess( m_numRead );
        } else {
            size_t length = getEvent<VanadisSyscallReadEvent*>()->getBufferCount() - m_numRead;
            if ( length > 64 ) { length = 64; }
            readChunk( length );
        }
        return false;
    }

 private:
    std::vector<uint8_t>    m_data;
    bool                    m_eof;
    int                     m_fd;
    size_t                  m_numRead;
    std::shared_ptr<OS::EmulatedFile> m_emulated;
};

} // namespace Vanadis
} // namespace SST

#endif
