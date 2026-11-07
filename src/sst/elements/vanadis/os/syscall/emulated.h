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

#ifndef _H_VANADIS_OS_SYSCALL_EMULATED
#define _H_VANADIS_OS_SYSCALL_EMULATED

// The system calls added for a user-mode DPDK environment (EAL with --no-huge --no-pci --in-memory): pipes, epoll,
// timer files, sleeps, memory files, mlockall, the identity calls, faccessat and mkdirat. Each reads its arguments
// from a VanadisSyscallArgsEvent (the six argument registers). The files are emulated (os/include/emulatedFile.h);
// a call that has to wait blocks its hardware thread through VanadisNodeOSComponent::blockSyscall().
//
// Layouts are those of the asm-generic ABI that AArch64 uses: struct epoll_event is { u32 events; u64 data; } with
// natural alignment (16 bytes), struct timespec is { s64 tv_sec; s64 tv_nsec; }, struct itimerspec is
// { timespec it_interval; timespec it_value; }.

#include "os/syscall/syscall.h"
#include "os/callev/voscallargs.h"
#include "os/callev/voscallfstat.h"
#include "os/callev/voscallfstatat.h"
#include "os/include/emulatedFile.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace SST {
namespace Vanadis {

#define VANADIS_LINUX_O_NONBLOCK      04000
#define VANADIS_LINUX_AT_FDCWD        -100
#define VANADIS_LINUX_AT_SYMLINK_NOFOLLOW 0x100
#define VANADIS_LINUX_AT_EMPTY_PATH   0x1000
#define VANADIS_LINUX_STAT_BYTES      128
#define VANADIS_LINUX_EPOLL_CTL_ADD   1
#define VANADIS_LINUX_EPOLL_CTL_DEL   2
#define VANADIS_LINUX_EPOLL_CTL_MOD   3
#define VANADIS_LINUX_EPOLLIN         0x001
#define VANADIS_LINUX_EPOLLOUT        0x004
#define VANADIS_LINUX_EPOLLHUP        0x010
#define VANADIS_LINUX_EPOLL_EVENT_BYTES 16
#define VANADIS_LINUX_TIMER_ABSTIME   1
#define VANADIS_LINUX_PIPE_BYTES      65536
#define VANADIS_NS_PER_S              1000000000ULL
#define VANADIS_NS_PER_MS             1000000ULL

// Base of the new calls: the argument registers and the node time.
class VanadisArgsSyscall : public VanadisSyscall {
public:
    VanadisArgsSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event,
                        const char* name )
        : VanadisSyscall( os, coreLink, process, event, name ) {}

protected:
    uint64_t arg( int k ) { return getEvent<VanadisSyscallArgsEvent*>()->arg(k); }
    static uint64_t timespecNs( const uint8_t* p ) {
        int64_t sec, nsec;
        memcpy( &sec, p, sizeof(sec) );
        memcpy( &nsec, p + sizeof(sec), sizeof(nsec) );
        return (uint64_t)sec * VANADIS_NS_PER_S + (uint64_t)nsec;
    }
    static void putTimespec( uint8_t* p, uint64_t ns ) {
        int64_t sec = ns / VANADIS_NS_PER_S, nsec = ns % VANADIS_NS_PER_S;
        memcpy( p, &sec, sizeof(sec) );
        memcpy( p + sizeof(sec), &nsec, sizeof(nsec) );
    }
};

// pipe2(int fds[2], int flags): two emulated ends that share a buffer of VANADIS_LINUX_PIPE_BYTES bytes.
class VanadisPipe2Syscall : public VanadisArgsSyscall {
public:
    VanadisPipe2Syscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event )
        : VanadisArgsSyscall( os, coreLink, process, event, "pipe2" )
    {
        auto buf = std::make_shared<OS::PipeBuffer>( VANADIS_LINUX_PIPE_BYTES );
        auto rd = std::make_shared<OS::PipeReadEnd>( buf );
        auto wr = std::make_shared<OS::PipeWriteEnd>( buf );
        bool nonblock = arg(1) & VANADIS_LINUX_O_NONBLOCK;
        rd->setNonblock( nonblock );
        wr->setNonblock( nonblock );
        int fds[2];
        fds[0] = process->openEmulatedFile( "pipe", rd, O_RDONLY );
        fds[1] = process->openEmulatedFile( "pipe", wr, O_WRONLY );
        if ( fds[0] < 0 || fds[1] < 0 ) {
            setReturnFail( fds[0] < 0 ? fds[0] : fds[1] );
            return;
        }
        m_buffer.resize( sizeof(fds) );
        memcpy( m_buffer.data(), fds, sizeof(fds) );
        writeMemory( arg(0), m_buffer );
    }
    void memReqIsDone(bool) override { setReturnSuccess(0); }

private:
    std::vector<uint8_t> m_buffer;
};

// epoll_create1(int flags).
class VanadisEpollCreate1Syscall : public VanadisArgsSyscall {
public:
    VanadisEpollCreate1Syscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event )
        : VanadisArgsSyscall( os, coreLink, process, event, "epoll_create1" )
    {
        int fd = process->openEmulatedFile( "epoll", std::make_shared<OS::EpollFile>(), O_RDWR );
        if ( fd < 0 ) {
            setReturnFail( fd );
        } else {
            setReturnSuccess( fd );
        }
    }
};

// epoll_ctl(int epfd, int op, int fd, struct epoll_event* event).
class VanadisEpollCtlSyscall : public VanadisArgsSyscall {
public:
    VanadisEpollCtlSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event )
        : VanadisArgsSyscall( os, coreLink, process, event, "epoll_ctl" )
    {
        auto ep = process->getEmulatedFile( arg(0) );
        int fd = arg(2);
        int op = arg(1);
        if ( ! ep || OS::EmulatedFile::Kind::EPOLL != ep->kind() ) {
            setReturnFail( -LINUX_EINVAL );
            return;
        }
        if ( ! process->isFileOpen( fd ) ) {
            setReturnFail( -LINUX_EBADF );
            return;
        }
        // Linux refuses regular files (EPERM); only emulated files can be waited for here.
        if ( ! process->getEmulatedFile( fd ) ) {
            setReturnFail( -EPERM );
            return;
        }
        m_epoll = std::static_pointer_cast<OS::EpollFile>( ep );
        if ( VANADIS_LINUX_EPOLL_CTL_DEL == op ) {
            if ( 0 == m_epoll->interest_.erase( fd ) ) {
                setReturnFail( -ENOENT );
            } else {
                setReturnSuccess( 0 );
            }
            return;
        }
        if ( VANADIS_LINUX_EPOLL_CTL_ADD != op && VANADIS_LINUX_EPOLL_CTL_MOD != op ) {
            setReturnFail( -LINUX_EINVAL );
            return;
        }
        bool present = m_epoll->interest_.count( fd );
        if ( VANADIS_LINUX_EPOLL_CTL_ADD == op && present ) {
            setReturnFail( -EEXIST );
            return;
        }
        if ( VANADIS_LINUX_EPOLL_CTL_MOD == op && ! present ) {
            setReturnFail( -ENOENT );
            return;
        }
        m_buffer.resize( VANADIS_LINUX_EPOLL_EVENT_BYTES );
        readMemory( arg(3), m_buffer );
    }
    void memReqIsDone(bool) override {
        OS::EpollFile::Interest in;
        memcpy( &in.events, m_buffer.data(), sizeof(in.events) );
        memcpy( &in.data, m_buffer.data() + sizeof(uint64_t), sizeof(in.data) );
        m_epoll->interest_[ (int)arg(2) ] = in;
        setReturnSuccess( 0 );
    }

private:
    std::shared_ptr<OS::EpollFile> m_epoll;
    std::vector<uint8_t> m_buffer;
};

// epoll_pwait(int epfd, struct epoll_event* events, int maxevents, int timeout_ms, const sigset_t*, size_t): the
// ready files of the interest list (level-triggered), at most maxevents; it blocks until one is ready or the
// timeout passes (-1: no timeout). The signal mask is ignored: the node OS delivers no signals.
class VanadisEpollPwaitSyscall : public VanadisArgsSyscall {
public:
    VanadisEpollPwaitSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event,
                              uint64_t now_ns )
        : VanadisArgsSyscall( os, coreLink, process, event, "epoll_pwait" ), m_deadline_ns(0)
    {
        auto ep = process->getEmulatedFile( arg(0) );
        if ( ! ep || OS::EmulatedFile::Kind::EPOLL != ep->kind() || (int)arg(2) <= 0 ) {
            setReturnFail( -LINUX_EINVAL );
            return;
        }
        m_epoll = std::static_pointer_cast<OS::EpollFile>( ep );
        int timeout = (int)arg(3);
        if ( ! collect( now_ns ) ) {
            if ( 0 == timeout ) {
                setReturnSuccess( 0 );
            } else {
                m_deadline_ns = timeout > 0 ? now_ns + (uint64_t)timeout * VANADIS_NS_PER_MS : 0;
                os->blockSyscall( this );
            }
        }
    }
    bool retry( uint64_t now_ns ) override {
        if ( collect( now_ns ) ) {
            return true;
        }
        if ( m_deadline_ns && now_ns >= m_deadline_ns ) {
            setReturnSuccess( 0 );
            return true;
        }
        return false;
    }
    uint64_t wakeAtNs( uint64_t now_ns ) override {
        uint64_t at = m_deadline_ns > now_ns ? m_deadline_ns : 0;
        for ( auto& kv : m_epoll->interest_ ) {
            auto file = m_process->getEmulatedFile( kv.first );
            uint64_t t = file ? file->readyAtNs( now_ns ) : 0;
            if ( t && ( 0 == at || t < at ) ) {
                at = t;
            }
        }
        return at;
    }
    void memReqIsDone(bool) override { setReturnSuccess( m_count ); }

private:
    // Writes the ready events to memory and returns true, or returns false when none is ready.
    bool collect( uint64_t now_ns ) {
        m_buffer.clear();
        m_count = 0;
        for ( auto& kv : m_epoll->interest_ ) {
            if ( m_count >= (int)arg(2) ) {
                break;
            }
            auto file = m_process->getEmulatedFile( kv.first );
            if ( ! file ) {
                continue;
            }
            uint32_t ready = 0;
            if ( ( kv.second.events & VANADIS_LINUX_EPOLLIN ) && file->readable( now_ns ) ) {
                ready |= VANADIS_LINUX_EPOLLIN;
            }
            if ( ( kv.second.events & VANADIS_LINUX_EPOLLOUT ) && file->writable( now_ns ) ) {
                ready |= VANADIS_LINUX_EPOLLOUT;
            }
            if ( 0 == ready ) {
                continue;
            }
            size_t at = m_buffer.size();
            m_buffer.resize( at + VANADIS_LINUX_EPOLL_EVENT_BYTES, 0 );
            memcpy( m_buffer.data() + at, &ready, sizeof(ready) );
            memcpy( m_buffer.data() + at + sizeof(uint64_t), &kv.second.data, sizeof(uint64_t) );
            m_count++;
        }
        if ( 0 == m_count ) {
            return false;
        }
        writeMemory( arg(1), m_buffer );
        return true;
    }

    std::shared_ptr<OS::EpollFile> m_epoll;
    std::vector<uint8_t> m_buffer;
    int m_count;
    uint64_t m_deadline_ns;
};

// timerfd_create(int clockid, int flags): every clock of the node is the simulated time (clock_gettime).
class VanadisTimerfdCreateSyscall : public VanadisArgsSyscall {
public:
    VanadisTimerfdCreateSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event )
        : VanadisArgsSyscall( os, coreLink, process, event, "timerfd_create" )
    {
        auto timer = std::make_shared<OS::TimerFile>();
        timer->setNonblock( arg(1) & VANADIS_LINUX_O_NONBLOCK );
        int fd = process->openEmulatedFile( "timerfd", timer, O_RDONLY );
        if ( fd < 0 ) {
            setReturnFail( fd );
        } else {
            setReturnSuccess( fd );
        }
    }
};

// timerfd_settime(int fd, int flags, const struct itimerspec* new, struct itimerspec* old).
class VanadisTimerfdSettimeSyscall : public VanadisArgsSyscall {
public:
    VanadisTimerfdSettimeSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event,
                                  uint64_t now_ns )
        : VanadisArgsSyscall( os, coreLink, process, event, "timerfd_settime" ), m_now_ns(now_ns), m_state(READ_NEW)
    {
        auto file = process->getEmulatedFile( arg(0) );
        if ( ! file || OS::EmulatedFile::Kind::TIMER != file->kind() ) {
            setReturnFail( -LINUX_EINVAL );
            return;
        }
        m_timer = std::static_pointer_cast<OS::TimerFile>( file );
        m_buffer.resize( 4 * sizeof(int64_t) );
        readMemory( arg(2), m_buffer );
    }
    void memReqIsDone(bool) override {
        if ( READ_NEW == m_state ) {
            uint64_t interval = timespecNs( m_buffer.data() );
            uint64_t value = timespecNs( m_buffer.data() + 2 * sizeof(int64_t) );
            uint64_t old_expire = m_timer->expireNs(), old_interval = m_timer->intervalNs();
            uint64_t expire = 0;
            if ( value ) {
                expire = ( arg(1) & VANADIS_LINUX_TIMER_ABSTIME ) ? value : m_now_ns + value;
            }
            m_timer->set( expire, interval );
            if ( arg(3) ) {
                putTimespec( m_buffer.data(), old_interval );
                putTimespec( m_buffer.data() + 2 * sizeof(int64_t), old_expire > m_now_ns ? old_expire - m_now_ns : 0 );
                m_state = WRITE_OLD;
                writeMemory( arg(3), m_buffer );
                return;
            }
        }
        setReturnSuccess( 0 );
    }

private:
    enum { READ_NEW, WRITE_OLD };
    std::shared_ptr<OS::TimerFile> m_timer;
    std::vector<uint8_t> m_buffer;
    uint64_t m_now_ns;
    int m_state;
};

// timerfd_gettime(int fd, struct itimerspec* curr).
class VanadisTimerfdGettimeSyscall : public VanadisArgsSyscall {
public:
    VanadisTimerfdGettimeSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event,
                                  uint64_t now_ns )
        : VanadisArgsSyscall( os, coreLink, process, event, "timerfd_gettime" )
    {
        auto file = process->getEmulatedFile( arg(0) );
        if ( ! file || OS::EmulatedFile::Kind::TIMER != file->kind() ) {
            setReturnFail( -LINUX_EINVAL );
            return;
        }
        auto timer = std::static_pointer_cast<OS::TimerFile>( file );
        m_buffer.resize( 4 * sizeof(int64_t) );
        putTimespec( m_buffer.data(), timer->intervalNs() );
        putTimespec( m_buffer.data() + 2 * sizeof(int64_t), timer->expireNs() > now_ns ? timer->expireNs() - now_ns : 0 );
        writeMemory( arg(1), m_buffer );
    }
    void memReqIsDone(bool) override { setReturnSuccess( 0 ); }

private:
    std::vector<uint8_t> m_buffer;
};

// clock_nanosleep(clockid, flags, const timespec* req, timespec* rem) and nanosleep(req, rem): the hardware thread
// waits until the node time reaches the request (an absolute time with TIMER_ABSTIME). No signal interrupts it, so
// rem is never written.
class VanadisNanosleepSyscall : public VanadisArgsSyscall {
public:
    VanadisNanosleepSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event,
                             uint64_t now_ns, bool clock )
        : VanadisArgsSyscall( os, coreLink, process, event, clock ? "clock_nanosleep" : "nanosleep" ), m_now_ns(now_ns),
          m_absolute( clock && ( arg(1) & VANADIS_LINUX_TIMER_ABSTIME ) ), m_wake_ns(0), m_reading(true)
    {
        m_buffer.resize( 2 * sizeof(int64_t) );
        readMemory( clock ? arg(2) : arg(0), m_buffer );
    }
    void memReqIsDone(bool) override {
        if ( ! m_reading ) {
            return;
        }
        m_reading = false;
        uint64_t req = timespecNs( m_buffer.data() );
        m_wake_ns = m_absolute ? req : m_now_ns + req;
        if ( m_wake_ns <= m_now_ns ) {
            setReturnSuccess( 0 );
        } else {
            m_os->blockSyscall( this );
        }
    }
    bool retry( uint64_t now_ns ) override {
        if ( now_ns >= m_wake_ns ) {
            setReturnSuccess( 0 );
            return true;
        }
        return false;
    }
    uint64_t wakeAtNs( uint64_t now_ns ) override { return m_wake_ns > now_ns ? m_wake_ns : 0; }

private:
    std::vector<uint8_t> m_buffer;
    uint64_t m_now_ns;
    bool m_absolute;
    uint64_t m_wake_ns;
    bool m_reading;
};

// memfd_create(const char* name, unsigned flags): a memory file whose mappings are anonymous memory.
class VanadisMemfdCreateSyscall : public VanadisArgsSyscall {
public:
    VanadisMemfdCreateSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event )
        : VanadisArgsSyscall( os, coreLink, process, event, "memfd_create" )
    {
        readString( arg(0), m_name );
    }
    void memReqIsDone(bool) override {
        m_name.resize( strlen( m_name.c_str() ) );
        int fd = m_process->openEmulatedFile( "memfd:" + m_name, std::make_shared<OS::MemoryFile>( m_name ), O_RDWR );
        if ( fd < 0 ) {
            setReturnFail( fd );
        } else {
            setReturnSuccess( fd );
        }
    }

private:
    std::string m_name;
};

// ftruncate(int fd, off_t length): a memory file takes the size; a host file is truncated on the host.
class VanadisFtruncateSyscall : public VanadisArgsSyscall {
public:
    VanadisFtruncateSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event )
        : VanadisArgsSyscall( os, coreLink, process, event, "ftruncate" )
    {
        auto file = process->getEmulatedFile( arg(0) );
        if ( file ) {
            if ( OS::EmulatedFile::Kind::MEMORY != file->kind() ) {
                setReturnFail( -LINUX_EINVAL );
                return;
            }
            std::static_pointer_cast<OS::MemoryFile>( file )->truncate( arg(1) );
            setReturnSuccess( 0 );
            return;
        }
        int hostfd = process->getFileDescriptor( arg(0) );
        if ( -1 == hostfd ) {
            setReturnFail( -LINUX_EBADF );
        } else if ( ftruncate( hostfd, arg(1) ) ) {
            setReturnFail( -errno );
        } else {
            setReturnSuccess( 0 );
        }
    }
};

// mlockall(int flags), munlockall(), getuid(), geteuid(), getgid(), getegid(): answered at once. The node has no
// paging to disk, so locking memory does nothing.
class VanadisImmediateSyscall : public VanadisArgsSyscall {
public:
    VanadisImmediateSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event,
                             const char* name, int64_t value )
        : VanadisArgsSyscall( os, coreLink, process, event, name )
    {
        setReturnSuccess( value );
    }
};

// faccessat(int dirfd, const char* path, int mode, int flags) and mkdirat(int dirfd, const char* path, mode_t mode):
// the path goes through VanadisNodeOSComponent::hostPath(), so the node's own /sys and /proc entries are found.
class VanadisPathSyscall : public VanadisArgsSyscall {
public:
    VanadisPathSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallArgsEvent* event,
                        bool mkdir )
        : VanadisArgsSyscall( os, coreLink, process, event, mkdir ? "mkdirat" : "faccessat" ), m_mkdir(mkdir), m_dirfd(AT_FDCWD)
    {
        int dirfd = arg(0);
        if ( VANADIS_LINUX_AT_FDCWD != dirfd ) {
            m_dirfd = process->getFileDescriptor( dirfd );
            if ( -1 == m_dirfd ) {
                setReturnFail( -LINUX_EBADF );
                return;
            }
        }
        readString( arg(1), m_path );
    }
    void memReqIsDone(bool) override {
        m_path.resize( strlen( m_path.c_str() ) );
        std::string path = m_os->hostPath( m_path );
        int ret = m_mkdir ? mkdirat( m_dirfd, path.c_str(), arg(2) ) : faccessat( m_dirfd, path.c_str(), arg(2), 0 );
        if ( ret ) {
            setReturnFail( -errno );
        } else {
            setReturnSuccess( 0 );
        }
    }

private:
    bool m_mkdir;
    int m_dirfd;
    std::string m_path;
};

// fstat(fd, struct stat*) and newfstatat(dirfd, path, struct stat*, flags) of a 64-bit process when the node OS
// parameter stat_files is set (otherwise both fail with EINVAL, as upstream): a host file is stat()ed on the host,
// with the path through VanadisNodeOSComponent::hostPath(); an emulated file is described by its kind and size. The
// layout is struct stat of asm-generic (AArch64 and RISC-V 64), 128 bytes.
class VanadisStatSyscall : public VanadisSyscall {
public:
    VanadisStatSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallFstatEvent* event )
        : VanadisSyscall( os, coreLink, process, event, "fstat" ), m_statbuf( event->getStructAddress() ), m_dirfd(AT_FDCWD),
          m_flags(0), m_path_read(true)
    {
        describeFd( event->getFileHandle() );
    }
    VanadisStatSyscall( VanadisNodeOSComponent* os, SST::Link* coreLink, OS::ProcessInfo* process, VanadisSyscallFstatAtEvent* event )
        : VanadisSyscall( os, coreLink, process, event, "fstatat" ), m_statbuf( event->getStatbuf() ), m_dirfd(AT_FDCWD),
          m_flags( event->getFlags() ), m_path_read(false)
    {
        m_guest_dirfd = (int)event->getDirfd();
        readString( event->getPathname(), m_path );
    }
    void memReqIsDone(bool) override {
        if ( m_path_read ) {
            setReturnSuccess( 0 );
            return;
        }
        m_path_read = true;
        m_path.resize( strlen( m_path.c_str() ) );
        if ( m_path.empty() && ( m_flags & VANADIS_LINUX_AT_EMPTY_PATH ) ) {
            describeFd( m_guest_dirfd );
            return;
        }
        if ( VANADIS_LINUX_AT_FDCWD != m_guest_dirfd && '/' != m_path[0] ) {
            m_dirfd = m_process->getFileDescriptor( m_guest_dirfd );
            if ( -1 == m_dirfd ) {
                setReturnFail( -LINUX_EBADF );
                return;
            }
        }
        struct stat st;
        std::string path = '/' == m_path[0] ? m_os->hostPath( m_path ) : m_path;
        if ( fstatat( m_dirfd, path.c_str(), &st, m_flags & VANADIS_LINUX_AT_SYMLINK_NOFOLLOW ? AT_SYMLINK_NOFOLLOW : 0 ) ) {
            setReturnFail( -errno );
            return;
        }
        writeStat( st );
    }

private:
    // Describes an open descriptor (emulated or host) and writes the result.
    void describeFd( int fd ) {
        auto file = m_process->getEmulatedFile( fd );
        struct stat st;
        memset( &st, 0, sizeof(st) );
        if ( file ) {
            switch ( file->kind() ) {
              case OS::EmulatedFile::Kind::PIPE_READ:
              case OS::EmulatedFile::Kind::PIPE_WRITE: st.st_mode = S_IFIFO | 0600; break;
              case OS::EmulatedFile::Kind::DEVICE:     st.st_mode = S_IFCHR | 0600; break;
              case OS::EmulatedFile::Kind::MEMORY:
              case OS::EmulatedFile::Kind::STATIC:
              case OS::EmulatedFile::Kind::THREAD_NAME: st.st_mode = S_IFREG | 0600; break;
              default:                                  st.st_mode = 0600; break; // an anonymous inode
            }
            st.st_nlink = 1;
            st.st_size = file->size();
            writeStat( st );
            return;
        }
        int hostfd = m_process->getFileDescriptor( fd );
        if ( -1 == hostfd ) {
            setReturnFail( -LINUX_EBADF );
            return;
        }
        if ( fstat( hostfd, &st ) ) {
            setReturnFail( -errno );
            return;
        }
        writeStat( st );
    }
    // Writes the asm-generic struct stat of a host stat.
    void writeStat( const struct stat& st ) {
        m_buffer.assign( VANADIS_LINUX_STAT_BYTES, 0 );
        auto put64 = [this]( size_t at, uint64_t v ) { memcpy( m_buffer.data() + at, &v, sizeof(v) ); };
        auto put32 = [this]( size_t at, uint32_t v ) { memcpy( m_buffer.data() + at, &v, sizeof(v) ); };
        put64( 0, st.st_dev );
        put64( 8, st.st_ino );
        put32( 16, st.st_mode );
        put32( 20, st.st_nlink );
        put32( 24, st.st_uid );
        put32( 28, st.st_gid );
        put64( 32, st.st_rdev );
        put64( 48, st.st_size );
        put32( 56, st.st_blksize );
        put64( 64, st.st_blocks );
        put64( 72, st.st_atim.tv_sec );
        put64( 80, st.st_atim.tv_nsec );
        put64( 88, st.st_mtim.tv_sec );
        put64( 96, st.st_mtim.tv_nsec );
        put64( 104, st.st_ctim.tv_sec );
        put64( 112, st.st_ctim.tv_nsec );
        writeMemory( m_statbuf, m_buffer );
    }

    uint64_t m_statbuf;
    int m_dirfd;
    int m_guest_dirfd;
    int64_t m_flags;
    bool m_path_read;
    std::string m_path;
    std::vector<uint8_t> m_buffer;
};

} // namespace Vanadis
} // namespace SST

#endif
