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

#ifndef _H_VANADIS_ARM64_CPU_OS
#define _H_VANADIS_ARM64_CPU_OS

#define PRIuXX PRIu64
#define PRIdXX PRId64
#define PRIxXX PRIx64

#include "os/callev/voscallall.h"
#include "os/vcpuos2.h"
#include "os/voscallev.h"
#include <functional>

#include <fcntl.h>

#define ARM64_CONVERT( x ) \
    if ( flags & ARM64_O_##x ) {\
        flags &= ~ARM64_O_##x;\
        out |= O_##x;\
    }

// This was generated on Ubuntu compiled with GCC.
// Does this need to generated with the cross compiler?
#define ARM64_O_RDONLY      0
#define ARM64_O_WRONLY      1
#define ARM64_O_RDWR        2
#define ARM64_O_APPEND      02000
#define ARM64_O_ASYNC       020000
#define ARM64_O_CLOEXEC     02000000
#define ARM64_O_CREAT       0100
#define ARM64_O_DIRECTORY   040000
#define ARM64_O_DSYNC       010000
#define ARM64_O_EXCL        0200
#define ARM64_O_NOCTTY      0400
#define ARM64_O_NOFOLLOW   0100000
#define ARM64_O_SYNC        04010000
#define ARM64_O_TRUNC       01000
#define ARM64_O_NONBLOCK    04000
#define ARM64_O_NDELAY      ARM64_O_NONBLOCK
#define ARM64_O_LARGEFILE   0400000

#ifndef SST_COMPILE_MACOSX
#define ARM64_O_DIRECT   0200000
#define ARM64_O_NOATIME     01000000
#define ARM64_O_PATH        010000000
#define ARM64_O_TMPFILE     020200000
#endif

#define ARM64_MAP_STACK 0x20000
#define ARM64_MAP_ANONYMOUS 0x20
#define ARM64_MAP_PRIVATE 0x2
#define ARM64_MAP_FIXED 0x10
#define ARM64_MAP_SHARED 0x1
#define ARM64_MAP_NORESERVE 0x4000
#define ARM64_MAP_POPULATE 0x8000

#define VANADIS_SYSCALL_ARM64_IOCTL 29
#define VANADIS_SYSCALL_ARM64_RM_INOTIFY 28
#define VANADIS_SYSCALL_ARM64_UNLINKAT 35
#define VANADIS_SYSCALL_ARM64_OPENAT 56
#define VANADIS_SYSCALL_ARM64_CLOSE 57
#define VANADIS_SYSCALL_ARM64_LSEEK 62
#define VANADIS_SYSCALL_ARM64_READ 63
#define VANADIS_SYSCALL_ARM64_WRITE 64
#define VANADIS_SYSCALL_ARM64_READV 65
#define VANADIS_SYSCALL_ARM64_WRITEV 66
#define VANADIS_SYSCALL_ARM64_SPLICE 76
#define VANADIS_SYSCALL_ARM64_READLINKAT 78
#define VANADIS_SYSCALL_ARM64_FSTATAT 79
#define VANADIS_SYSCALL_ARM64_FSTAT 80
#define VANADIS_SYSCALL_ARM64_EXIT 93
#define VANADIS_SYSCALL_ARM64_EXIT_GROUP 94
#define VANADIS_SYSCALL_ARM64_SET_TID_ADDRESS 96
#define VANADIS_SYSCALL_ARM64_FUTEX 98
#define VANADIS_SYSCALL_ARM64_SET_ROBUST_LIST 99
#define VANADIS_SYSCALL_ARM64_GET_RLIST 100
#define VANADIS_SYSCALL_ARM64_CLOCK_GETTIME 113
#define VANADIS_SYSCALL_ARM64_SCHED_SETAFFINITY 122
#define VANADIS_SYSCALL_ARM64_SCHED_GETAFFINITY 123
#define VANADIS_SYSCALL_ARM64_SCHED_YIELD 124
#define VANADIS_SYSCALL_ARM64_KILL 129
#define VANADIS_SYSCALL_ARM64_TGKILL 131
#define VANADIS_SYSCALL_ARM64_RT_SIGACTION 134
#define VANADIS_SYSCALL_ARM64_RT_SIGPROCMASK 135
#define VANADIS_SYSCALL_ARM64_GETPGID 155
#define VANADIS_SYSCALL_ARM64_UNAME 160
#define VANADIS_SYSCALL_ARM64_GETCPU 168
#define VANADIS_SYSCALL_ARM64_GETPID 172
#define VANADIS_SYSCALL_ARM64_GETPPID 173
#define VANADIS_SYSCALL_ARM64_GETTID 178
#define VANADIS_SYSCALL_ARM64_BRK 214
#define VANADIS_SYSCALL_ARM64_UNMAP 215
#define VANADIS_SYSCALL_ARM64_CLONE 220
#define VANADIS_SYSCALL_ARM64_MMAP 222
#define VANADIS_SYSCALL_ARM64_MPROTECT 226
#define VANADIS_SYSCALL_ARM64_MADVISE 233
#define VANADIS_SYSCALL_ARM64_HWPROBE 258
#define VANADIS_SYSCALL_ARM64_PRLIMIT 261
#define VANADIS_SYSCALL_ARM64_GETRANDOM 278
#define VANADIS_SYSCALL_ARM64_RSEQ 293
#define VANADIS_SYSCALL_ARM64_CLONE3 435
#define VANADIS_SYSCALL_ARM64_CHECKPOINT 500

#define VANADIS_SYSCALL_ARM64_RET_REG 0

// Calls added for the user-mode DPDK environment (asm-generic numbers, include/uapi/asm-generic/unistd.h).
#define VANADIS_SYSCALL_ARM64_EPOLL_CREATE1 20
#define VANADIS_SYSCALL_ARM64_EPOLL_CTL 21
#define VANADIS_SYSCALL_ARM64_EPOLL_PWAIT 22
#define VANADIS_SYSCALL_ARM64_MKDIRAT 34
#define VANADIS_SYSCALL_ARM64_FTRUNCATE 46
#define VANADIS_SYSCALL_ARM64_FACCESSAT 48
#define VANADIS_SYSCALL_ARM64_PIPE2 59
#define VANADIS_SYSCALL_ARM64_TIMERFD_CREATE 85
#define VANADIS_SYSCALL_ARM64_TIMERFD_SETTIME 86
#define VANADIS_SYSCALL_ARM64_TIMERFD_GETTIME 87
#define VANADIS_SYSCALL_ARM64_NANOSLEEP 101
#define VANADIS_SYSCALL_ARM64_CLOCK_NANOSLEEP 115
#define VANADIS_SYSCALL_ARM64_GETUID 174
#define VANADIS_SYSCALL_ARM64_GETEUID 175
#define VANADIS_SYSCALL_ARM64_GETGID 176
#define VANADIS_SYSCALL_ARM64_GETEGID 177
#define VANADIS_SYSCALL_ARM64_MLOCKALL 230
#define VANADIS_SYSCALL_ARM64_MUNLOCKALL 231
#define VANADIS_SYSCALL_ARM64_MEMFD_CREATE 279
#define VANADIS_SYSCALL_ARM64_FACCESSAT2 439

// A call whose OS side takes the six argument registers as they are (VanadisSyscallArgsEvent).
#define ARM64_ARGS_SYSCALL( name ) \
    VanadisSyscallEvent* name( int hw_thr ) { \
        return new VanadisSyscallArgsEvent( core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, SYSCALL_OP_##name, \
            getArgRegister(0), getArgRegister(1), getArgRegister(2), getArgRegister(3), getArgRegister(4), \
            getArgRegister(5) ); \
    }

#define InstallARM64FuncPtr( funcName ) Install_ISA_FuncPtr( ARM64, funcName )

namespace SST {
namespace Vanadis {


/* A handler for syscalls. Each decoder (and thus each hardware thread) has its own handler instance.
 * The handler should handle syscalls that do not require global state locally and dispatch all other
 * syscalls to the OS instance which is shared across all Vanadis cores in a multicore simulation.
 *
 * Note: Syscalls are implemented as needed and many are implemented partially - just to the degree needed
 * to successfully simulate an application.
 */

template <typename T1, VanadisOSBitType BitType, int RegZero, int OsCodeReg, int LinkReg >
class VanadisARM64OSHandler2 : public VanadisCPUOSHandler2< T1, BitType, RegZero, OsCodeReg, LinkReg > {

    AddUsing;

public:
    VanadisARM64OSHandler2(ComponentId_t id, Params& params) :
        VanadisCPUOSHandler2< T1, BitType, RegZero, OsCodeReg, LinkReg >(id, params, "ARM64" )
    {
        InstallCommonFuncs( ARM64 )

        InstallARM64FuncPtr( MMAP );
        InstallARM64FuncPtr( CLONE );
        InstallARM64FuncPtr( CLONE3 );
        InstallARM64FuncPtr( FUTEX );
        InstallARM64FuncPtr( CLOCK_GETTIME );
        InstallARM64FuncPtr( SET_ROBUST_LIST );
        InstallARM64FuncPtr( PRLIMIT );
        InstallARM64FuncPtr( READLINKAT );
        InstallARM64FuncPtr( GETRANDOM );
        InstallARM64FuncPtr( FSTATAT );
        InstallARM64FuncPtr( LSEEK );
        InstallARM64FuncPtr( HWPROBE );
        InstallARM64FuncPtr( RSEQ );
        InstallARM64FuncPtr( CHECKPOINT );

        InstallARM64FuncPtr( EPOLL_CREATE1 );
        InstallARM64FuncPtr( EPOLL_CTL );
        InstallARM64FuncPtr( EPOLL_PWAIT );
        InstallARM64FuncPtr( MKDIRAT );
        InstallARM64FuncPtr( FTRUNCATE );
        InstallARM64FuncPtr( FACCESSAT );
        install( VANADIS_SYSCALL_ARM64_FACCESSAT2, Make_ISA_FuncPtr( ARM64, FACCESSAT ) );
        InstallARM64FuncPtr( PIPE2 );
        InstallARM64FuncPtr( TIMERFD_CREATE );
        InstallARM64FuncPtr( TIMERFD_SETTIME );
        InstallARM64FuncPtr( TIMERFD_GETTIME );
        InstallARM64FuncPtr( NANOSLEEP );
        InstallARM64FuncPtr( CLOCK_NANOSLEEP );
        InstallARM64FuncPtr( GETUID );
        InstallARM64FuncPtr( GETEUID );
        InstallARM64FuncPtr( GETGID );
        InstallARM64FuncPtr( GETEGID );
        InstallARM64FuncPtr( MLOCKALL );
        InstallARM64FuncPtr( MUNLOCKALL );
        InstallARM64FuncPtr( MEMFD_CREATE );
    }

    ARM64_ARGS_SYSCALL( EPOLL_CREATE1 )
    ARM64_ARGS_SYSCALL( EPOLL_CTL )
    ARM64_ARGS_SYSCALL( EPOLL_PWAIT )
    ARM64_ARGS_SYSCALL( MKDIRAT )
    ARM64_ARGS_SYSCALL( FTRUNCATE )
    ARM64_ARGS_SYSCALL( FACCESSAT )
    ARM64_ARGS_SYSCALL( PIPE2 )
    ARM64_ARGS_SYSCALL( TIMERFD_CREATE )
    ARM64_ARGS_SYSCALL( TIMERFD_SETTIME )
    ARM64_ARGS_SYSCALL( TIMERFD_GETTIME )
    ARM64_ARGS_SYSCALL( NANOSLEEP )
    ARM64_ARGS_SYSCALL( CLOCK_NANOSLEEP )
    ARM64_ARGS_SYSCALL( GETUID )
    ARM64_ARGS_SYSCALL( GETEUID )
    ARM64_ARGS_SYSCALL( GETGID )
    ARM64_ARGS_SYSCALL( GETEGID )
    ARM64_ARGS_SYSCALL( MLOCKALL )
    ARM64_ARGS_SYSCALL( MUNLOCKALL )
    ARM64_ARGS_SYSCALL( MEMFD_CREATE )

    virtual ~VanadisARM64OSHandler2() {}

    VanadisSyscallEvent* CHECKPOINT( int hw_thr ) {
        output_->verbose(CALL_INFO, 8, 0, "checkpoint()\n");
        return new VanadisSyscallCheckpointEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B );
    }

    VanadisSyscallEvent* CLONE( int hw_thr ) {
        uint64_t flags          = getArgRegister(0);
        uint64_t threadStack    = getArgRegister(1);
        int64_t  ptid           = getArgRegister(2);
        int64_t  tls            = getArgRegister(3);
        int64_t  ctid           = getArgRegister(4);

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0,
                    "clone( %#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 ")\n",
                    instPtr,threadStack,flags,ptid,tls,ctid);
        #endif
        return new VanadisSyscallCloneEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, instPtr,
                    threadStack, flags, ptid, tls, ctid );
    }

    VanadisSyscallEvent* CLONE3( int hw_thr ) {
        uint64_t cl_args_addr  = getArgRegister( 0 ); // struct clone_args *cl_args
        uint64_t cl_args_size = getArgRegister( 1 );  // size_t size

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0,
                    "clone3( %#" PRIx64 ", %#" PRIx64 ", %" PRIu64 ")\n",
                    instPtr,cl_args_addr, cl_args_size);
        #endif
        return new VanadisSyscallClone3Event(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, instPtr,
                    cl_args_addr, cl_args_size);
    }

    VanadisSyscallEvent* GETRANDOM( int hw_thr ) {
        uint64_t buf    = getArgRegister( 0 );
        uint64_t buflen = getArgRegister( 1 );
        uint64_t flags  = getArgRegister( 2 );

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0, "getrandom( %" PRIu64 ", %" PRIu64 ", %#" PRIx64 ")\n", buf, buflen, flags);
        #endif

        return new VanadisSyscallGetrandomEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, buf, buflen, flags );
    }

    VanadisSyscallEvent* READLINKAT( int hw_thr ) {
        uint64_t dirfd      = getArgRegister( 0 );
        uint64_t pathname   = getArgRegister( 1 );
        uint64_t buf        = getArgRegister( 2 );
        uint64_t bufsize    = getArgRegister( 3 );

#ifdef SST_COMPILE_MACOSX
        if (  VANADIS_AT_FDCWD == dirfd ) {
            dirfd = AT_FDCWD;
        }
#endif

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0, "readlinkat( %" PRIu64 ", %#" PRIx64 ", %#" PRIx64 ", %" PRIu64 ")\n",
                    dirfd, pathname, buf, bufsize);
        #endif

        return new VanadisSyscallReadLinkAtEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, dirfd, pathname, buf, bufsize);
    }

    VanadisSyscallEvent* FSTATAT( int hw_thr ) {
        int32_t  dirfd      = getArgRegister(0);
        uint64_t pathname   = getArgRegister(1);
        uint64_t statbuf    = getArgRegister(2);
        uint64_t flags      = getArgRegister(3);

#ifdef SST_COMPILE_MACOSX
        if (  VANADIS_AT_FDCWD == dirfd ) {
            dirfd = AT_FDCWD;
        }
#endif
        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0, "fstat( %d, %#" PRIx64 ", %#" PRIx64 ", %#" PRIx64 ")\n",dirfd,pathname,statbuf,flags);
        #endif
        return new VanadisSyscallFstatAtEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, dirfd, pathname, statbuf, flags );
    }

    VanadisSyscallEvent* FUTEX( int hw_thr ) {
        uint64_t addr           = getArgRegister(0);
        int32_t  op             = getArgRegister(1);
        uint32_t val            = getArgRegister(2);
        uint64_t timeout_addr   = getArgRegister(3);
        uint64_t addr2          = getArgRegister(4);
        uint32_t val3           = getArgRegister(5);

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0,
                            "futex( %#" PRIx64 ", %" PRIx32 ", %" PRIx32 ", %" PRIx64 ", %" PRIx64 ", %" PRIu32 " )\n",
                            addr, op, val, timeout_addr, addr2, val3);
        #endif
        return new VanadisSyscallFutexEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, addr, op, val, timeout_addr, addr2, val3 );
    }

    VanadisSyscallEvent* SET_ROBUST_LIST( int hw_thr ) {
        uint64_t  head  = getArgRegister( 0 );
        uint64_t  len   = getArgRegister( 1 );

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0, "set_robust_list(  %#" PRIx64 ", %" PRIu64" )\n",head,len);
        #endif

        return new VanadisSyscallSetRobustListEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, head, len );
    }

    VanadisSyscallEvent* MMAP( int hw_thr ) {
        uint64_t map_addr   = getArgRegister( 0 );
        uint64_t map_len    = getArgRegister( 1 );
        int32_t  map_prot   = getArgRegister( 2 );
        int32_t  map_flags  = getArgRegister( 3 );
        int32_t  fd         = getArgRegister( 4 );
        int64_t  offset     = getArgRegister( 5 );

        int32_t hostFlags = 0;

        // ignore this flag
        if ( map_flags & ARM64_MAP_STACK ) {
            map_flags &= ~ARM64_MAP_STACK;
        }
        if ( map_flags & ARM64_MAP_ANONYMOUS ) {
            hostFlags |= MAP_ANONYMOUS;
            map_flags &= ~ARM64_MAP_ANONYMOUS;
        }
        if ( map_flags & ARM64_MAP_PRIVATE ) {
            hostFlags |= MAP_PRIVATE;
            map_flags &= ~ARM64_MAP_PRIVATE;
        }
        // The node OS decides about these (os/syscall/mmap.h): MAP_FIXED fails with EEXIST unless its parameter
        // mmap_fixed_replace is set, as upstream; MAP_SHARED, MAP_NORESERVE, MAP_POPULATE need it too.
        if ( map_flags & ARM64_MAP_FIXED ) {
            hostFlags |= MAP_FIXED;
            map_flags &= ~ARM64_MAP_FIXED;
        }
        if ( map_flags & ARM64_MAP_SHARED ) {
            hostFlags |= MAP_SHARED;
            map_flags &= ~ARM64_MAP_SHARED;
        }
        if ( map_flags & ARM64_MAP_NORESERVE ) {
            hostFlags |= MAP_NORESERVE;
            map_flags &= ~ARM64_MAP_NORESERVE;
        }
        if ( map_flags & ARM64_MAP_POPULATE ) {
            hostFlags |= MAP_POPULATE;
            map_flags &= ~ARM64_MAP_POPULATE;
        }
        this->sst_assert( map_flags == 0, CALL_INFO, -1, "Unhandled mmap flags" );

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0,
                    "mmap2( 0x%" PRI_ADDR ", %" PRIu64 ", %" PRId32 ", %" PRId32 ", %d, %" PRIu64 ")\n",
                    map_addr, map_len, map_prot, map_flags, fd, offset );
        #endif

        return new VanadisSyscallMemoryMapEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B,
                map_addr, map_len, map_prot, hostFlags, fd, offset, 0, 0);
    }

    VanadisSyscallEvent* CLOCK_GETTIME( int hw_thr ) {
        int64_t  clk_type   = getArgRegister( 0 );
        uint64_t time_addr  = getArgRegister( 1 );

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0,
                            "clock_gettime64( %" PRId64 ", 0x%" PRI_ADDR " )\n", clk_type, time_addr);
        #endif
        return new VanadisSyscallGetTime64Event(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, clk_type, time_addr);
    }

    VanadisSyscallEvent* HWPROBE( int hw_thr ) {
        #ifdef VANADIS_BUILD_DEBUG
        // Warning but ifdef'd because this rarely causes a problem
        output_->output("Warning: VANADIS_SYSCALL_ARM64_HWPROBE not implemented return success\n");
        #endif
        recvSyscallResp(new VanadisSyscallResponse(0));
        return nullptr;
    }

    VanadisSyscallEvent* RSEQ( int hw_thr ) {
        #ifdef VANADIS_BUILD_DEBUG
        // Warning but ifdef'd because this rarely causes a problem
        output_->output("Warning: VANADIS_SYSCALL_ARM64_RSEQ not implemented return success\n");
        #endif
        recvSyscallResp(new VanadisSyscallResponse(0));
        return nullptr;
    }

    VanadisSyscallEvent* RT_SIGPROCMASK( int hw_thr ) {
        int32_t  how                  = getArgRegister( 0 );
        uint64_t signal_set_in   = getArgRegister( 1 );
        uint64_t signal_set_out = getArgRegister( 2 );
        int32_t  signal_set_size = getArgRegister( 3 );

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0,
                            "rt_sigprocmask( %" PRId32 ", 0x%" PRI_ADDR ", 0x%" PRI_ADDR ", %" PRId32 ")\n",
                            how, signal_set_in, signal_set_out, signal_set_size);

        output_->output("Warning: VANADIS_SYSCALL_ARM64_RT_SIGPROCMASK not implemented return success\n");
        #endif

        recvSyscallResp(new VanadisSyscallResponse(0));
        return nullptr;
    }

    VanadisSyscallEvent* PRLIMIT( int hw_thr ) {
        uint64_t pid        = getArgRegister( 0 );
        uint64_t resource   = getArgRegister( 1 );
        uint64_t new_limit  = getArgRegister( 2 );
        uint64_t old_limit  = getArgRegister( 3 );

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0,
                            "prlimit( %" PRIu64 ", %" PRIu64 ",  %#" PRIx64 ", %#" PRIx64 ")\n", pid, resource, new_limit, old_limit );
        #endif

        return new VanadisSyscallPrlimitEvent(core_id_, hw_thr, VanadisOSBitType::VANADIS_OS_64B, pid, resource, new_limit, old_limit);
    }

    VanadisSyscallEvent* LSEEK( int hw_thr ) {
        int32_t fd       = getArgRegister(0);
        uint64_t offset   = getArgRegister(1);
        int32_t whence   = getArgRegister(2);

        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0, "lseek( %" PRId32 ", %" PRIu64 ", %" PRId32 " )\n", fd, offset, whence);
        #endif
        return new VanadisSyscallLseekEvent(core_id_, hw_thr, BitType, fd, offset, whence);
    }


    void recvSyscallResp( VanadisSyscallResponse* os_resp ) {
        #ifdef VANADIS_BUILD_DEBUG
        output_->verbose(CALL_INFO, 8, 0, "return-code: %" PRId64 " (success: %3s)\n",
                            os_resp->getReturnCode(), os_resp->isSuccessful() ? "yes" : "no");
        output_->verbose(CALL_INFO, 8, 0, "issuing call-backs to clear syscall ROB stops...\n");
        #endif
        // Set up the return code (according to ABI, this goes in r10)
        const uint16_t rc_reg = isa_table_->getIntPhysReg( VANADIS_SYSCALL_ARM64_RET_REG );
        const int64_t rc_val = (int64_t)os_resp->getReturnCode();
        reg_file_->setIntReg(rc_reg, rc_val);

        delete os_resp;
    }

private:

    uint64_t convertFlags( uint64_t flags ) {
        uint64_t out = 0;

        ARM64_CONVERT( RDONLY );
        ARM64_CONVERT( WRONLY );
        ARM64_CONVERT( RDWR );
        ARM64_CONVERT( APPEND );
        ARM64_CONVERT( ASYNC );
        ARM64_CONVERT( CLOEXEC );
        ARM64_CONVERT( CREAT );
        ARM64_CONVERT( DIRECTORY );
        ARM64_CONVERT( DSYNC );
        ARM64_CONVERT( EXCL );
        ARM64_CONVERT( NOCTTY );
        ARM64_CONVERT( NOFOLLOW );
        ARM64_CONVERT( SYNC );
        ARM64_CONVERT( TRUNC );
        ARM64_CONVERT( NONBLOCK );
        ARM64_CONVERT( NDELAY );

#ifdef SST_COMPILE_MACOSX
        flags &= ~ARM64_O_LARGEFILE;
#else
        ARM64_CONVERT( DIRECT );
        ARM64_CONVERT( LARGEFILE );
        ARM64_CONVERT( NOATIME );
        ARM64_CONVERT( PATH );
        ARM64_CONVERT( TMPFILE );
#endif
        if ( flags ) {
            this->fatal(CALL_INFO, -1, "Unable to convert remaining flags %#" PRIx64 "\n", flags );
        }

        return out;
    }
};

#define ARM64_ARG_REG_ZERO 0
#define ARM64_OS_CODE_REG 8
#define ARM64_LINK_REG 30

class VanadisARM64OSHandler :
    public VanadisARM64OSHandler2< uint64_t, VanadisOSBitType::VANADIS_OS_64B, ARM64_ARG_REG_ZERO, ARM64_OS_CODE_REG, ARM64_LINK_REG > {

public:
    SST_ELI_REGISTER_SUBCOMPONENT(
        VanadisARM64OSHandler,
    #ifdef VANADIS_BUILD_DEBUG
        "vanadisdbg",
    #else
        "vanadis",
    #endif
        "VanadisARM64OSHandler",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Provides SYSCALL handling for a AArch64 decoding core",
        SST::Vanadis::VanadisCPUOSHandler)

    SST_ELI_DOCUMENT_PARAMS(
        { "verbose", "Verbosity of the handler", "0" },
        { "unknown_syscall", "A system call number without a handler: ignore (returns at once, x0 unchanged, as upstream), enosys (returns -ENOSYS, as Linux) or fatal", "ignore" })

    VanadisARM64OSHandler(ComponentId_t id, Params& params) :
        VanadisARM64OSHandler2<uint64_t, VanadisOSBitType::VANADIS_OS_64B, ARM64_ARG_REG_ZERO, ARM64_OS_CODE_REG, ARM64_LINK_REG >(id, params) { }
};

} // namespace Vanadis
} // namespace SST

#endif
