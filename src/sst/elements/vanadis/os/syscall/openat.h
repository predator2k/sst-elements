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

#ifndef _H_VANADIS_OS_SYSCALL_OPENAT
#define _H_VANADIS_OS_SYSCALL_OPENAT

#include "os/syscall/syscall.h"
#include "os/callev/voscallopenat.h"
#include "os/include/emulatedFile.h"
#include <memory>

namespace SST {
namespace Vanadis {

class VanadisOpenatSyscall : public VanadisSyscall {
public:
    VanadisOpenatSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallOpenatEvent* event )
        : VanadisSyscall( os, coreLink, process, event, "openat" )
    {
        #ifdef VANADIS_BUILD_DEBUG
        m_output->verbose( CALL_INFO, 16, 0, "[syscall-openat] -> call is openat( %" PRId64 " )\n", event->getPathPointer());
        #endif

        m_dirFd  = event->getDirectoryFileDescriptor();
        // if the directory fd passed by the syscall is positive it should point to a entry in the file_descriptor table
        // if the directory fd is negative pass that to to the openat handler ( AT_FDCWD is negative )
        if ( m_dirFd > -1 ) {
            m_dirFd = process->getFileDescriptor( m_dirFd );

            if ( -1 == m_dirFd) {
                m_output->verbose(CALL_INFO, 16, 0, "can't find m_dirFd=%d in open file descriptor table\n", m_dirFd);
                setReturnFail(-LINUX_EBADF);
                return;
            }
            // get the FD that SST will use
            #ifdef VANADIS_BUILD_DEBUG
            m_output->verbose(CALL_INFO, 16, 0, "sst fd=%d pathname=%s\n", m_dirFd, process->getFilePath(m_dirFd).c_str());
            #endif
        }

        #ifdef VANADIS_BUILD_DEBUG
        m_output->verbose( CALL_INFO, 16, 0, "[syscall-openat] -> call is openat( %" PRId64 ", 0x%0" PRI_ADDR ", %" PRId64 " )\n",
                event->getDirectoryFileDescriptor(), event->getPathPointer(), event->getFlags());
        #endif
        readString(event->getPathPointer(),m_filename);
    }

    void memReqIsDone(bool) {
        #ifdef VANADIS_BUILD_DEBUG
        m_output->verbose(CALL_INFO, 16, 0, "[syscall-openat] path: \"%s\"\n", m_filename.c_str());
        #endif

        std::string path( m_filename.c_str() );
        int flags = getEvent<VanadisSyscallOpenatEvent*>()->getFlags();

        // A device file of the parameter device_paths: an emulated file whose mmap maps the device window.
        OS::Device* dev = m_os->findDeviceByPath( path );
        if ( dev ) {
            setReturnSuccess( m_process->openEmulatedFile( path, std::make_shared<OS::DeviceFile>( dev ), flags ) );
            return;
        }
        // The thread-name files of a node with its own /proc (mapped_prefixes holds /proc): pthread_setname_np
        // writes the name there.
        if ( m_os->hostPath( "/proc" ) != "/proc" && isThreadNamePath( path ) ) {
            setReturnSuccess( m_process->openEmulatedFile( path, std::make_shared<OS::ThreadNameFile>( m_os->threadName( path ) ), flags ) );
            return;
        }
        // The node's own /sys and /proc entries (mapped_prefixes); every other path is a host path, as upstream.
        if ( '/' == path[0] ) {
            path = m_os->hostPath( path );
        }

        int fd = m_process->openFile( path.c_str(), m_dirFd, flags, getEvent<VanadisSyscallOpenatEvent*>()->getMode() );

        if ( fd < 0 ) {
            // we need to save this because any other library calls could change it
            auto myErrno = errno;
            char buf[100];
            char* str = buf;
#ifdef SST_COMPILE_MACOSX
            if ( myErrno == EROFS ) {
                myErrno = EACCES;
            }
            strerror_r(myErrno,buf,100);
#else
            str = strerror_r(myErrno,buf,100);
#endif
            #ifdef VANADIS_BUILD_DEBUG
            m_output->verbose(CALL_INFO, 16, 0, "[syscall-openat] open of %s failed, errno=%d `%s`\n", m_filename.c_str(), myErrno, str );
            #endif

            setReturnFail( -myErrno );
        } else {
            setReturnSuccess( fd );
        }

    }

 private:
    // /proc/self/comm, /proc/thread-self/comm and /proc/self/task/<tid>/comm (also with a pid instead of self).
    static bool isThreadNamePath( const std::string& path ) {
        const std::string proc = "/proc/", comm = "/comm";
        if ( 0 != path.compare( 0, proc.size(), proc ) || path.size() < proc.size() + comm.size()
            || 0 != path.compare( path.size() - comm.size(), comm.size(), comm ) ) {
            return false;
        }
        std::string mid = path.substr( proc.size(), path.size() - proc.size() - comm.size() );
        if ( "self" == mid || "thread-self" == mid ) {
            return true;
        }
        size_t slash = mid.find( "/task/" );
        return std::string::npos != slash;
    }

    int m_dirFd;
    std::string m_filename;
};

} // namespace Vanadis
} // namespace SST

#endif
