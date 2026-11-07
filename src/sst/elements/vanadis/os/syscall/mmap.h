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

#ifndef _H_VANADIS_OS_SYSCALL_MMAP
#define _H_VANADIS_OS_SYSCALL_MMAP

#include "os/syscall/syscall.h"
#include "os/callev/voscallmmap.h"
#include "os/include/device.h"
#include "os/include/emulatedFile.h"
#include <memory>

namespace SST {
namespace Vanadis {

class VanadisMmapSyscall : public VanadisSyscall {
public:
    VanadisMmapSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallMemoryMapEvent* event )
        : VanadisSyscall( os, coreLink, process, event, "mmap" )
    {
        uint64_t address = event->getAllocationAddress();
        uint64_t length = event->getAllocationLength();
        int64_t protect = event->getProtectionFlags();
        int64_t flags = event->getAllocationFlags();
        auto fd = event->getFd();
        auto offset = event->getOffset();
        uint64_t call_stack = event->getStackPointer();
        uint64_t offset_units = event->getOffsetUnits();

        #ifdef VANADIS_BUILD_DEBUG
        m_output->verbose(CALL_INFO, 2, VANADIS_OS_DBG_SYSCALL, "[syscall-mmap] addr=%#" PRIx64 " length=%#" PRIx64 " prot=%#" PRIx64 " flags=%#" PRIx64
                " fd=%d offset=%" PRIu64 " call_stack=%#" PRIx64 " offset_units=%" PRIu64 "\n",
                    address,length,protect,flags,fd,offset,call_stack,offset_units);
        #endif

        // if that stackAddr is valid  we need to get mmap() arguments that are not in registers
        if ( event->getStackPointer() ) {
            if ( event->getOSBitType() == VanadisOSBitType::VANADIS_OS_32B ) {
                // int fd, off_t offset
                m_buffer.resize(sizeof(uint32_t)*2);
            } else {
                m_buffer.resize(sizeof(uint64_t)*2);
            }
            readMemory( event->getStackPointer() + 16, m_buffer );

        } else {
            // ARM64 passes the byte offset in the sixth register; the 32-bit ISAs pass page units on the stack.
            mmap( address, length, protect, flags, fd, offset_units ? offset_units : offset );
        }
    }

    void mmap( uint64_t address, uint64_t length, int protect, int flags, int fd, uint64_t offset ) {

        // MAP_SHARED, MAP_NORESERVE, MAP_POPULATE and MAP_FIXED follow Linux only when the OS parameter
        // mmap_fixed_replace is set; without it MAP_FIXED fails with EEXIST and the other flags are fatal, as upstream.
        const int linux_flags = MAP_SHARED | MAP_NORESERVE | MAP_POPULATE | MAP_FIXED;
        if ( ! m_os->mmapFixedReplace() ) {
            if ( flags & MAP_FIXED ) {
                setReturnFail( -EEXIST );
                return;
            }
            if ( flags & ~( MAP_ANONYMOUS | MAP_PRIVATE ) ) {
                m_output->fatal(CALL_INFO, -1, "-> error: mmap() flag %#" PRIx32 " not supported\n",flags);
            }
        } else if ( flags & ~( MAP_ANONYMOUS | MAP_PRIVATE | linux_flags ) ) {
            m_output->fatal(CALL_INFO, -1, "-> error: mmap() flag %#" PRIx32 " not supported\n",flags);
        }

        OS::Device* dev = nullptr;
        if ( -1000 == fd || -2000 == fd) {
            dev = m_os->getDevice( fd );
	    length = dev->getLength();
        } else if ( ! ( flags & MAP_ANONYMOUS ) && fd >= 0 ) {
            auto file = m_process->getEmulatedFile( fd );
            if ( file && OS::EmulatedFile::Kind::DEVICE == file->kind() ) {
                // A device file maps its register window; the offset selects a page of it.
                dev = std::static_pointer_cast<OS::DeviceFile>( file )->device();
                if ( offset + length > dev->getLength() ) {
                    setReturnFail( -EINVAL );
                    return;
                }
                if ( offset ) {
                    // The mapping keeps this window for its life (MemoryBacking holds the pointer).
                    dev = new OS::Device( dev->getName(), dev->getPhysAddr() + offset, length );
                }
            } else if ( file && OS::EmulatedFile::Kind::MEMORY == file->kind() ) {
                // A memory file of one process is anonymous memory; its pages are zero until written.
                dev = nullptr;
            } else if ( ! file && ! m_process->isFileOpen( fd ) ) {
                setReturnFail( -EBADF );
                return;
            }
            // A host file is mapped as zeroed memory, as upstream (its bytes are not read).
        }

        uint64_t addr;
        if ( m_os->mmapFixedReplace() ) {
            bool fixed = flags & MAP_FIXED;
            if ( fixed ) {
                // The replaced pages leave the page table and every TLB of the process.
                for ( const auto iter : m_process->getThreadList() ) {
                    if ( iter.second ) {
                        m_os->getMMU()->flushTlb( iter.second->getCore(), iter.second->getHwThread() );
                    }
                }
                uint64_t pages = ( length + m_os->getPageSize() - 1 ) / m_os->getPageSize();
                m_os->getMMU()->unmap( m_process->getpid(), address >> m_os->getPageShift(), pages );
            }
            addr = m_process->mmapAt( address, length, protect, dev, fixed );
        } else {
            addr = m_process->mmap( address, length, protect, flags, dev, offset );
        }

        if ( 0 == addr ) {
            setReturnFail( -ENOMEM );
        } else {
            setReturnSuccess( addr );
        }
    }

    void memReqIsDone(bool) {
        auto event = getEvent<VanadisSyscallMemoryMapEvent*>();
        uint32_t* tmp  = (uint32_t*)m_buffer.data();
        uint64_t address = event->getAllocationAddress();
        uint64_t length = event->getAllocationLength();
        int64_t protect = event->getProtectionFlags();
        int64_t flags = event->getAllocationFlags();
        m_output->verbose(CALL_INFO, 16, VANADIS_OS_DBG_SYSCALL, "mmap memReqIsDone [syscall-mmap] fd=%d offset=%" PRIu32 "\n", tmp[0],tmp[1]);

        mmap( address, length, protect, flags, tmp[0], tmp[1] );
    }

private:
    std::vector<uint8_t> m_buffer;
};

} // namespace Vanadis
} // namespace SST

#endif
