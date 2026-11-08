// Copyright 2009-2026 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2026, NTESS
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _H_VANADIS_DEVICE_MAP_EVENT
#define _H_VANADIS_DEVICE_MAP_EVENT

#include <sst/core/event.h>

#include <cstdint>

namespace SST {
namespace Vanadis {

// The node OS tells every core that a virtual range of the process maps a device (mmap of a device file) or no longer
// does (munmap): the pages are Device memory (Arm ARM DDI 0487, B2.7.2), which a core does not read speculatively
// (device_loads_in_order of vanadis.core).
class VanadisDeviceMapEvent : public SST::Event {
public:
    VanadisDeviceMapEvent() : SST::Event(), address(0), length(0), mapped(false) {}
    VanadisDeviceMapEvent( uint64_t address, uint64_t length, bool mapped ) :
        SST::Event(), address(address), length(length), mapped(mapped) {}

    // First virtual address of the range.
    uint64_t getAddress() const { return address; }
    // Bytes of the range.
    uint64_t getLength() const { return length; }
    // True: the range maps a device; false: the range was unmapped.
    bool     isMapped() const { return mapped; }

private:
    uint64_t address;
    uint64_t length;
    bool     mapped;

    void serialize_order(SST::Core::Serialization::serializer& ser) override {
        Event::serialize_order(ser);
        SST_SER(address);
        SST_SER(length);
        SST_SER(mapped);
    }

    ImplementSerializable(SST::Vanadis::VanadisDeviceMapEvent);
};

} // namespace Vanadis
} // namespace SST

#endif
