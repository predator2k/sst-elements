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

#ifndef _H_VANADIS_NODE_OS_INCLUDE_FD_TABLE
#define _H_VANADIS_NODE_OS_INCLUDE_FD_TABLE

#include <assert.h>
#include <stdint.h>
#include <string>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "os/include/emulatedFile.h"

namespace SST {
namespace Vanadis {

namespace OS {

class FileDescriptor {
public:
    FileDescriptor( const FileDescriptor& obj ) {
        path=obj.path;
        emulated = obj.emulated;
        if ( emulated ) {
            // A forked process shares the open file, as after dup().
            fd = -1;
            flags = obj.flags;
            mode = obj.mode;
            return;
        }

        int flags = fcntl(obj.fd, F_GETFL);

        // the mode could be 0 which will prevent us from opening it, so temporarily change the mode to RWX
        // but first we need to get the actual mode so we can set it after the open

        off_t pos = lseek( obj.fd, 0, SEEK_CUR );
        assert( -1 != pos );

        struct stat buf;
        int f = fstat(obj.fd,&buf);
        assert( 0 == f );

        f = fchmod( obj.fd, S_IRWXU );
        assert( 0 == f );

        fd = open( path.c_str(), flags );
        assert( -1 != fd );

        f = fchmod( obj.fd, buf.st_mode );
        assert( 0 == f );

        pos = lseek( obj.fd, pos, SEEK_SET );
        assert( -1 != pos );

        //printf("%s() %s oldfd=%d flags=%#x mode=%#x newfd=%d\n",__func__,path.c_str(), obj.fd,flags,buf.st_mode,fd);
    }

    FileDescriptor(std::string& file_path, int flags, mode_t mode, bool lazy) : path(file_path), flags(flags), mode(mode)
    {
        if ( ! lazy ) {
            fd = open(file_path.c_str(), flags, mode );
            if ( -1 == fd ) {
                printf("%s() FAILED: %s flags=%#x mode=0x%" PRIxMAX "\n",__func__,path.c_str(),flags, (uintmax_t)mode);
                throw errno;
            }
        } else {
            fd = -1;
        }
        //printf("%s() %s flags=%#x mode=%#x fd=%d\n",__func__,path.c_str(),flags,mode,fd);
    }

    FileDescriptor(std::string& file_path, int dirfd, int flags, mode_t mode) : path(file_path), fd( -1 )
    {
        fd = openat(dirfd, file_path.c_str(), flags, mode );
        if ( -1 == fd ) {
            throw errno;
        }
    }


    // A file the node OS keeps itself; path names it for messages and the checkpoint.
    FileDescriptor( const std::string& name, std::shared_ptr<EmulatedFile> file, int flags ) : path(name), fd(-1),
        flags(flags), mode(0), emulated(file) {}

    ~FileDescriptor() {
        if ( -1 != fd ) {
            close(fd);
        }
    }

    std::shared_ptr<EmulatedFile> getEmulated() const { return emulated; }

    int openLate() {
        if ( emulated ) {
            return -1;
        }
        fd = open(path.c_str(), flags, mode );
        if ( -1 == fd ) {
            printf("%s() FAILED: %s flags=%#x mode=0x%" PRIxMAX "\n",__func__,path.c_str(),flags, (uintmax_t)mode);
            throw errno;
        } else {
            printf("%s() %s flags=%#x mode=%" PRIxMAX "\n",__func__,path.c_str(),flags, (uintmax_t)mode);
        }
        return fd;
    }

    std::string getPath() const {
        return path;
    }

    int getFileDescriptor() {
        return fd;
    }

    FileDescriptor( SST::Output* output, FILE* fp ) {
        char str[80];
        int f = fscanf(fp, "path: %s\n", str);
        assert( 1 == f );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"path: %s\n",str );
        path = str;
        if ( 0 == path.compare( 0, strlen("emulated:"), "emulated:" ) ) {
            output->fatal(CALL_INFO, -1, "Error: the checkpoint holds the emulated file %s (pipe, epoll, timer, device, "
                "memory or configured file); the node OS cannot restore emulated files\n", str );
        }

        f = fscanf(fp,"fd: %d\n", &fd);
        assert( 1 == f );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"fd: %d\n", fd);

        f = fscanf(fp,"flags: %d\n", &flags );
        assert( 1 == f );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"flags: %d\n", flags );

        f = fscanf(fp,"mode: %" PRIuMAX "\n", (uintmax_t*)&mode);
        assert( 1 == f );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"mode: %" PRIuMAX "\n", (uintmax_t)mode);
    }

    void checkpoint( FILE* fp) {
        // An emulated file (pipe, epoll, timer, device, ...) has no host file to reopen; its path is written with the
        // prefix "emulated:" and a restore of it is reported as an error (see FileDescriptorTable).
        if ( emulated ) {
            fprintf(fp, "path: emulated:%s\n", path.c_str() );
        } else {
            fprintf(fp, "path: %s\n", path.c_str() );
        }
        fprintf(fp, "fd: %d\n", fd );
        fprintf(fp, "flags: %d\n", flags );
        fprintf(fp, "mode: %" PRIuMAX "\n", (uintmax_t)mode );
    }

protected:
    std::string path;
    int fd;
    int flags;
    mode_t mode;
    std::shared_ptr<EmulatedFile> emulated;
};

class FileDescriptorTable {

public:
    FileDescriptorTable( int maxFd ) : m_maxFD(maxFd), m_refCnt(1) {}

    void update( FileDescriptorTable* old ) {
        for ( const auto& kv: old->m_fileDescriptors ) {
            if ( m_fileDescriptors.find(kv.first) == m_fileDescriptors.end()) {
                m_fileDescriptors[kv.first] = new FileDescriptor( *kv.second );
            }
        }
    }

    ~FileDescriptorTable() {
        for (auto next_file = m_fileDescriptors.begin(); next_file != m_fileDescriptors.end(); next_file++) {
            delete next_file->second;
        }
    }

    void decRefCnt() {
        assert( m_refCnt > 0 );
        --m_refCnt;
    };
    void incRefCnt() { ++m_refCnt; };

    int refCnt() { return m_refCnt; }

    int close( int fd ) {
        if (m_fileDescriptors.find(fd) == m_fileDescriptors.end()) {
            return -EBADF;
        }
        delete m_fileDescriptors[fd];
        m_fileDescriptors.erase(fd);

        return 0;
    }

    int open(std::string file_path, int flags, mode_t mode, int fd = -1 ){
        if ( -1 == fd ) {
            fd = findFreeFd();
        }
        if ( fd >= 0 ) {
            try {
        //        m_fileDescriptors[fd] = new FileDescriptor(file_path,flags,mode, fd <= 2 );
                m_fileDescriptors[fd] = new FileDescriptor(file_path,flags,mode, false );
            } catch ( int err ) {
                fd = -err;
            }
        }
        return fd;
    }

    int openat(std::string file_path, int dirfd, int flags, mode_t mode) {
        int fd = findFreeFd();
        if ( fd >= 0 ) {
            try {
                m_fileDescriptors[fd] = new FileDescriptor(file_path,dirfd,flags,mode);
            } catch ( int err ) {
                fd = -err;
            }
        }
        return fd;
    }

    // Opens a file the node OS keeps itself; fd -1 takes the lowest free descriptor.
    int openEmulated( const std::string& name, std::shared_ptr<EmulatedFile> file, int flags, int fd = -1 ) {
        if ( -1 == fd ) {
            fd = findFreeFd();
        } else if ( m_fileDescriptors.find(fd) != m_fileDescriptors.end() ) {
            delete m_fileDescriptors[fd];
        }
        if ( fd >= 0 ) {
            m_fileDescriptors[fd] = new FileDescriptor( name, file, flags );
        }
        return fd;
    }

    // The emulated file of a descriptor, or null when it is a host file or not open.
    std::shared_ptr<EmulatedFile> getEmulated( uint32_t handle ) {
        auto iter = m_fileDescriptors.find(handle);
        if ( iter == m_fileDescriptors.end() ) {
            return nullptr;
        }
        return iter->second->getEmulated();
    }

    // Whether a descriptor is open (host or emulated file).
    bool isOpen( uint32_t handle ) { return m_fileDescriptors.find(handle) != m_fileDescriptors.end(); }

    int findFreeFd() {
        int fd = 3;
        while (m_fileDescriptors.find(fd) != m_fileDescriptors.end() && fd < m_maxFD) {
            fd++;
        }
        if ( m_maxFD == fd ) {
            fd = -EMFILE;
        }
        return fd;
    }

    int getDescriptor( uint32_t handle ) {
        auto iter= m_fileDescriptors.find(handle);
        if (iter == m_fileDescriptors.end()) {
            return -1;
        }
        if ( iter->second->getEmulated() ) {
            return -1;
        }
        auto fd = iter->second->getFileDescriptor();
        if ( -1 == fd ) {
            fd = iter->second->openLate();
        }
        return fd;
    }

    std::string getPath( uint32_t handle ) {
        auto iter= m_fileDescriptors.find(handle);
        if (iter == m_fileDescriptors.end()) {
            return "";
        }
        return iter->second->getPath();
    }

    FileDescriptorTable( SST::Output* output, FILE* fp ) {
        char* tmp = nullptr;
        size_t num = 0;
        (void) !getline( &tmp, &num, fp );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"%s",tmp);
        assert( 0 == strcmp(tmp,"#FileDescriptorTable start\n") );
        free(tmp);

        int f = fscanf(fp,"m_refCnt: %d\n",&m_refCnt);
        assert( 1 == f );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"m_refCnt: %d\n",m_refCnt);

        f = fscanf(fp,"m_maxFD: %d\n",&m_maxFD);
        assert( 1 == f );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"m_maxFD: %d\n",m_maxFD);

        size_t size;
        f = fscanf(fp,"m_fileDescriptors.size(): %zu\n",&size);
        assert( 1 == f );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"m_fileDescriptors.size(): %zu\n",size);

        for ( auto i = 0; i < size; i++ ) {
            int fd;
            int f = fscanf(fp,"fd: %d\n", &fd );
            assert( 1 == f );
            output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"fd: %d\n", fd );
            m_fileDescriptors[fd] = new FileDescriptor(output, fp);
        }

        tmp = nullptr;
        num = 0;
        (void) !getline( &tmp, &num, fp );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"%s",tmp);
        assert( 0 == strcmp(tmp,"#FileDescriptorTable end\n") );
        free(tmp);
    }

    void checkpoint( FILE* fp ) {
        fprintf(fp,"#FileDescriptorTable start\n");
        fprintf(fp,"m_refCnt: %d\n",m_refCnt);
        fprintf(fp,"m_maxFD: %d\n",m_maxFD);

        fprintf(fp,"m_fileDescriptors.size(): %zu\n",m_fileDescriptors.size());

        for ( auto & x : m_fileDescriptors ) {
            fprintf(fp,"fd: %d\n",x.first);
            x.second->checkpoint(fp);
        }
        fprintf(fp,"#FileDescriptorTable end\n");
    }
private:

    std::unordered_map< uint32_t, FileDescriptor* > m_fileDescriptors;

    int m_refCnt;
    int m_maxFD;
};

}
}
}

#endif
