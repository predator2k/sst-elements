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

#ifndef _H_VANADIS_NODE_OS_INCLUDE_EMULATED_FILE
#define _H_VANADIS_NODE_OS_INCLUDE_EMULATED_FILE

// Files whose contents the node OS keeps itself rather than in a host file: pipes, epoll instances, timer
// files, memory files, devices, the thread-name files of /proc and files whose bytes the configuration gives
// (the node's own /sys and /proc entries, a delayed stdin).
//
// A host file would block the simulator (a read of an empty pipe) or describe the host instead of the node
// (/sys/devices/system/cpu), so these live in the OS. A read or write that cannot proceed now returns
// -EAGAIN; the system call then blocks its hardware thread (VanadisNodeOSComponent::blockSyscall) and is
// retried whenever a file changes or a timer expires, unless the file was opened O_NONBLOCK.

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace SST {
namespace Vanadis {

namespace OS {

class Device;

/** One open file of the node OS that has no host file behind it. */
class EmulatedFile {
public:
    /** What the file is. */
    enum class Kind : uint8_t { PIPE_READ, PIPE_WRITE, EPOLL, TIMER, MEMORY, STATIC, DEVICE, THREAD_NAME };

    explicit EmulatedFile( Kind kind ) : kind_(kind), nonblock_(false) {}
    virtual ~EmulatedFile() {}

    Kind kind() const { return kind_; }
    bool nonblock() const { return nonblock_; }
    void setNonblock( bool nonblock ) { nonblock_ = nonblock; }

    /** Reads up to n bytes. Returns the bytes read, 0 at end of file, -EAGAIN when it would block, or -errno. */
    virtual int64_t read( uint8_t* buf, size_t n, uint64_t now_ns ) { return -EINVAL; }
    /** Writes n bytes. Returns the bytes written, -EAGAIN when it would block, or -errno. */
    virtual int64_t write( const uint8_t* buf, size_t n, uint64_t now_ns ) { return -EINVAL; }
    /** Whether a read would not block now (EPOLLIN). */
    virtual bool readable( uint64_t now_ns ) const { return false; }
    /** Whether a write would not block now (EPOLLOUT). */
    virtual bool writable( uint64_t now_ns ) const { return false; }
    /** The earliest later time at which the file becomes readable by itself (a timer, a delayed stdin), or 0. */
    virtual uint64_t readyAtNs( uint64_t now_ns ) const { return 0; }
    /** Bytes of the file for fstat (st_size). */
    virtual uint64_t size() const { return 0; }
    /** Moves the file position (lseek); -ESPIPE for files without one. */
    virtual int64_t seek( int64_t offset, int whence ) { return -ESPIPE; }

private:
    Kind kind_;
    bool nonblock_;
};

/** The buffer both ends of a pipe share. */
class PipeBuffer {
public:
    explicit PipeBuffer( size_t capacity ) : capacity_(capacity), readers_(0), writers_(0) {}
    std::deque<uint8_t> data_;
    size_t capacity_;
    int readers_;
    int writers_;
};

/** The read end of a pipe (pipe2[0]). */
class PipeReadEnd : public EmulatedFile {
public:
    explicit PipeReadEnd( std::shared_ptr<PipeBuffer> buf ) : EmulatedFile(Kind::PIPE_READ), buf_(buf) { buf_->readers_++; }
    ~PipeReadEnd() { buf_->readers_--; }

    int64_t read( uint8_t* out, size_t n, uint64_t ) override {
        if ( buf_->data_.empty() ) {
            return buf_->writers_ ? -EAGAIN : 0;
        }
        size_t k = 0;
        while ( k < n && ! buf_->data_.empty() ) {
            out[k++] = buf_->data_.front();
            buf_->data_.pop_front();
        }
        return k;
    }
    bool readable( uint64_t ) const override { return ! buf_->data_.empty() || 0 == buf_->writers_; }
    uint64_t size() const override { return buf_->data_.size(); }

private:
    std::shared_ptr<PipeBuffer> buf_;
};

/** The write end of a pipe (pipe2[1]). */
class PipeWriteEnd : public EmulatedFile {
public:
    explicit PipeWriteEnd( std::shared_ptr<PipeBuffer> buf ) : EmulatedFile(Kind::PIPE_WRITE), buf_(buf) { buf_->writers_++; }
    ~PipeWriteEnd() { buf_->writers_--; }

    int64_t write( const uint8_t* in, size_t n, uint64_t ) override {
        if ( 0 == buf_->readers_ ) {
            return -EPIPE;
        }
        size_t room = buf_->capacity_ - buf_->data_.size();
        if ( 0 == room ) {
            return -EAGAIN;
        }
        size_t k = n < room ? n : room;
        buf_->data_.insert( buf_->data_.end(), in, in + k );
        return k;
    }
    bool writable( uint64_t ) const override { return buf_->data_.size() < buf_->capacity_; }

private:
    std::shared_ptr<PipeBuffer> buf_;
};

/** A timer file (timerfd_create): expirations at a time and then every interval. */
class TimerFile : public EmulatedFile {
public:
    TimerFile() : EmulatedFile(Kind::TIMER), expire_ns_(0), interval_ns_(0) {}

    /** Arms (expire_ns > 0) or disarms the timer; both times are in the node's clock. */
    void set( uint64_t expire_ns, uint64_t interval_ns ) { expire_ns_ = expire_ns; interval_ns_ = interval_ns; }
    uint64_t expireNs() const { return expire_ns_; }
    uint64_t intervalNs() const { return interval_ns_; }

    /** Expirations since the last read; moves the next expiration past now. */
    uint64_t take( uint64_t now_ns ) {
        if ( 0 == expire_ns_ || now_ns < expire_ns_ ) {
            return 0;
        }
        uint64_t count = 1;
        if ( interval_ns_ ) {
            count += ( now_ns - expire_ns_ ) / interval_ns_;
            expire_ns_ += count * interval_ns_;
        } else {
            expire_ns_ = 0;
        }
        return count;
    }
    int64_t read( uint8_t* out, size_t n, uint64_t now_ns ) override {
        if ( n < sizeof(uint64_t) ) {
            return -EINVAL;
        }
        uint64_t count = take( now_ns );
        if ( 0 == count ) {
            return -EAGAIN;
        }
        memcpy( out, &count, sizeof(count) );
        return sizeof(count);
    }
    bool readable( uint64_t now_ns ) const override { return expire_ns_ && now_ns >= expire_ns_; }
    uint64_t readyAtNs( uint64_t now_ns ) const override { return expire_ns_ > now_ns ? expire_ns_ : 0; }

private:
    uint64_t expire_ns_;
    uint64_t interval_ns_;
};

/** An epoll instance: the files it watches and the events of each (level-triggered). */
class EpollFile : public EmulatedFile {
public:
    struct Interest {
        uint32_t events;
        uint64_t data;
    };
    EpollFile() : EmulatedFile(Kind::EPOLL) {}
    std::map<int, Interest> interest_;
};

/** A file of anonymous memory (memfd_create); its pages come from the mapping, so only its size is kept. */
class MemoryFile : public EmulatedFile {
public:
    explicit MemoryFile( const std::string& name ) : EmulatedFile(Kind::MEMORY), name_(name), size_(0) {}
    void truncate( uint64_t size ) { size_ = size; }
    uint64_t size() const override { return size_; }
    std::string name_;

private:
    uint64_t size_;
};

/** A file whose bytes the configuration gives: an entry of the node's /sys or /proc, or the process's stdin,
 * which becomes readable only at ready_ns (until then a read blocks). */
class StaticFile : public EmulatedFile {
public:
    StaticFile( const std::vector<uint8_t>& bytes, uint64_t ready_ns ) : EmulatedFile(Kind::STATIC), bytes_(bytes),
        pos_(0), ready_ns_(ready_ns) {}

    int64_t read( uint8_t* out, size_t n, uint64_t now_ns ) override {
        if ( now_ns < ready_ns_ ) {
            return -EAGAIN;
        }
        size_t k = 0;
        while ( k < n && pos_ < bytes_.size() ) {
            out[k++] = bytes_[pos_++];
        }
        return k;
    }
    int64_t write( const uint8_t*, size_t n, uint64_t ) override { return -EACCES; }
    bool readable( uint64_t now_ns ) const override { return now_ns >= ready_ns_; }
    uint64_t readyAtNs( uint64_t now_ns ) const override { return ready_ns_ > now_ns ? ready_ns_ : 0; }
    uint64_t size() const override { return bytes_.size(); }
    int64_t seek( int64_t offset, int whence ) override {
        int64_t base = SEEK_SET == whence ? 0 : SEEK_CUR == whence ? (int64_t)pos_ : (int64_t)bytes_.size();
        if ( base + offset < 0 ) {
            return -EINVAL;
        }
        pos_ = base + offset;
        return pos_;
    }

private:
    std::vector<uint8_t> bytes_;
    size_t pos_;
    uint64_t ready_ns_;
};

/** An open device file (a register window that mmap maps). */
class DeviceFile : public EmulatedFile {
public:
    explicit DeviceFile( Device* dev ) : EmulatedFile(Kind::DEVICE), dev_(dev) {}
    Device* device() const { return dev_; }

private:
    Device* dev_;
};

/** /proc/self/task/<tid>/comm: the thread's name, which pthread_setname_np writes and pthread_getname_np reads. The
 * name lives in the node OS (one per path), so a later open of the same path reads it. */
class ThreadNameFile : public EmulatedFile {
public:
    explicit ThreadNameFile( std::shared_ptr<std::string> name ) : EmulatedFile(Kind::THREAD_NAME), name_(name), pos_(0) {}
    int64_t write( const uint8_t* in, size_t n, uint64_t ) override {
        name_->assign( (const char*)in, n );
        return n;
    }
    int64_t read( uint8_t* out, size_t n, uint64_t ) override {
        size_t k = 0;
        while ( k < n && pos_ < name_->size() ) {
            out[k++] = (*name_)[pos_++];
        }
        return k;
    }
    bool readable( uint64_t ) const override { return true; }
    bool writable( uint64_t ) const override { return true; }

private:
    std::shared_ptr<std::string> name_;
    size_t pos_;
};

}
}
}

#endif
