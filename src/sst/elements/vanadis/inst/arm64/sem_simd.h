// Copyright 2009-2025 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2025, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// of the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

// Register access helpers of the A64 floating-point and vector micro-ops, and the micro-ops of the Advanced SIMD
// structure loads and stores. Vanadis holds a vector register as VL bytes in the floating-point class.

#ifndef _H_VANADIS_ARM64_SEM_SIMD
#define _H_VANADIS_ARM64_SEM_SIMD

#include "inst/arm64/varm64inst.h"

#include <cstdint>
#include <cstring>

namespace SST {
namespace Vanadis {
namespace A64Simd {

template <typename T>
inline T
vget(const uint8_t* p, unsigned i)
{
    T v;
    std::memcpy(&v, p + i * sizeof(T), sizeof(T));
    return v;
}

template <typename T>
inline void
vset(uint8_t* p, unsigned i, T v)
{
    std::memcpy(p + i * sizeof(T), &v, sizeof(T));
}

// An A64 write of n bytes to a vector register zeroes every byte above them, up to the vector length.
inline void
writeLow(VanadisArm64Inst& op, VanadisRegisterFile* rf, uint16_t pos, const void* data, unsigned n)
{
    uint8_t* d = op.Vout(rf, pos);
    std::memcpy(d, data, n);
    std::memset(d + n, 0, rf->getFPRegWidth() - n);
}

// Calls f with a value of the element type of a log2 byte size (1, 2, 3 for H, S, D).
template <typename F>
inline void
byFormat(uint32_t log2_bytes, F&& f)
{
    switch ( log2_bytes ) {
    case 1: f(uint16_t {}); break;
    case 2: f(uint32_t {}); break;
    default: f(uint64_t {}); break;
    }
}

// Structure loads and stores. a = element bytes, b = bytes of one register (8 or 16), c = register count.

// LD2, LD3, LD4 (multiple structures): sources hold the bytes in memory order, destinations the registers.
inline void
structDeinterleave(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t mem[64];
    uint8_t reg[16];
    for ( uint32_t k = 0; k < op.c; ++k ) std::memcpy(mem + k * op.b, op.Vin(rf, k), op.b);
    const uint32_t elements = op.b / op.a;
    for ( uint32_t k = 0; k < op.c; ++k ) {
        for ( uint32_t e = 0; e < elements; ++e ) {
            std::memcpy(reg + e * op.a, mem + (e * op.c + k) * op.a, op.a);
        }
        writeLow(op, rf, k, reg, op.b);
    }
}

// ST2, ST3, ST4 (multiple structures): sources are the registers, destinations hold the bytes in memory order.
inline void
structInterleave(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t mem[64];
    const uint32_t elements = op.b / op.a;
    for ( uint32_t k = 0; k < op.c; ++k ) {
        const uint8_t* s = op.Vin(rf, k);
        for ( uint32_t e = 0; e < elements; ++e ) {
            std::memcpy(mem + (e * op.c + k) * op.a, s + e * op.a, op.a);
        }
    }
    for ( uint32_t k = 0; k < op.c; ++k ) writeLow(op, rf, k, mem + k * op.b, op.b);
}

// LD1 to LD4 (single structure): replaces one element of a 128-bit register. Source 0 is the register, source 1
// the loaded element. a = element bytes, c = byte offset of the element.
inline void
laneInsert(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t v[16];
    std::memcpy(v, op.Vin(rf, 0), sizeof(v));
    std::memcpy(v + op.c, op.Vin(rf, 1), op.a);
    writeLow(op, rf, 0, v, sizeof(v));
}

// ST1 to ST4 (single structure): moves one element to the bottom of a temporary. a = element bytes, c = byte offset.
inline void
laneExtract(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    writeLow(op, rf, 0, op.Vin(rf, 0) + op.c, op.a);
}

// LD1R to LD4R: repeats the loaded element over b bytes. a = element bytes, b = bytes of the register.
inline void
laneReplicate(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t v[16];
    for ( uint32_t e = 0; e < op.b / op.a; ++e ) std::memcpy(v + e * op.a, op.Vin(rf, 0), op.a);
    writeLow(op, rf, 0, v, op.b);
}

} // namespace A64Simd
} // namespace Vanadis
} // namespace SST

#endif
