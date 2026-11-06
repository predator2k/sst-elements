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

// Micro-ops of the cryptographic extensions of Advanced SIMD: AES, SHA1, SHA256, SHA512, SHA3, SM3 and SM4.

#ifndef _H_VANADIS_ARM64_SEM_CRYPTO
#define _H_VANADIS_ARM64_SEM_CRYPTO

#include "inst/arm64/sem_simd.h"

namespace SST {
namespace Vanadis {
namespace A64Crypto {

using A64Simd::vget;
using A64Simd::vset;
using A64Simd::writeLow;

inline uint8_t
gfMul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while ( b ) {
        if ( b & 1 ) r ^= a;
        a = static_cast<uint8_t>((a << 1) ^ ((a & 0x80) ? 0x1B : 0));
        b >>= 1;
    }
    return r;
}

// The AES S-box and its inverse, computed from their definition in FIPS 197.
struct AesBoxes
{
    uint8_t sbox[256];
    uint8_t inv[256];
    AesBoxes()
    {
        for ( unsigned x = 0; x < 256; ++x ) {
            uint8_t v = 0;
            if ( x ) {
                for ( unsigned y = 1; y < 256; ++y ) {
                    if ( gfMul(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) == 1 ) {
                        v = static_cast<uint8_t>(y);
                        break;
                    }
                }
            }
            uint8_t s = v;
            for ( unsigned k = 1; k < 5; ++k ) s ^= static_cast<uint8_t>((v << k) | (v >> (8 - k)));
            s ^= 0x63;
            sbox[x] = s;
            inv[s] = static_cast<uint8_t>(x);
        }
    }
};

inline const AesBoxes&
aesBoxes()
{
    static const AesBoxes boxes;
    return boxes;
}

inline void
aesShiftAndSub(const uint8_t* s, uint8_t* r, bool inverse)
{
    const AesBoxes& b = aesBoxes();
    for ( unsigned col = 0; col < 4; ++col ) {
        for ( unsigned row = 0; row < 4; ++row ) {
            const unsigned from = (inverse ? (col + 4 - row) : (col + row)) % 4;
            const uint8_t  v = s[from * 4 + row];
            r[col * 4 + row] = inverse ? b.inv[v] : b.sbox[v];
        }
    }
}

// AESE (c 0), AESD (c 1): sources Vd, Vn. AESMC (c 2), AESIMC (c 3): source Vn.
inline void
aes(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint8_t r[16];
    if ( op.c < 2 ) {
        uint8_t s[16];
        for ( unsigned k = 0; k < 16; ++k ) s[k] = op.Vin(rf, 0)[k] ^ op.Vin(rf, 1)[k];
        aesShiftAndSub(s, r, op.c == 1);
    }
    else {
        const uint8_t* s = op.Vin(rf, 0);
        for ( unsigned col = 0; col < 4; ++col ) {
            const uint8_t* c = s + col * 4;
            for ( unsigned row = 0; row < 4; ++row ) {
                const uint8_t a0 = c[row], a1 = c[(row + 1) % 4], a2 = c[(row + 2) % 4], a3 = c[(row + 3) % 4];
                r[col * 4 + row] = op.c == 2 ? static_cast<uint8_t>(gfMul(a0, 2) ^ gfMul(a1, 3) ^ a2 ^ a3)
                                             : static_cast<uint8_t>(gfMul(a0, 14) ^ gfMul(a1, 11) ^ gfMul(a2, 13) ^ gfMul(a3, 9));
            }
        }
    }
    writeLow(op, rf, 0, r, 16);
}

inline uint32_t rol32(uint32_t v, unsigned n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
inline uint32_t ror32(uint32_t v, unsigned n) { n &= 31; return n ? (v >> n) | (v << (32 - n)) : v; }
inline uint64_t ror64(uint64_t v, unsigned n) { n &= 63; return n ? (v >> n) | (v << (64 - n)) : v; }
inline uint64_t rol64(uint64_t v, unsigned n) { n &= 63; return n ? (v << n) | (v >> (64 - n)) : v; }

// SHA1C (c 0), SHA1P (c 1), SHA1M (c 2). Sources Vd, Sn, Vm.
inline void
sha1Hash(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t x[4], w[4];
    std::memcpy(x, op.Vin(rf, 0), 16);
    uint32_t y = vget<uint32_t>(op.Vin(rf, 1), 0);
    std::memcpy(w, op.Vin(rf, 2), 16);
    for ( unsigned e = 0; e < 4; ++e ) {
        uint32_t t;
        if ( op.c == 0 ) t = ((x[2] ^ x[3]) & x[1]) ^ x[3];
        else if ( op.c == 1 ) t = x[1] ^ x[2] ^ x[3];
        else t = (x[1] & x[2]) | ((x[1] | x[2]) & x[3]);
        y = y + rol32(x[0], 5) + t + w[e];
        x[1] = rol32(x[1], 30);
        const uint32_t top = x[3];
        x[3] = x[2];
        x[2] = x[1];
        x[1] = x[0];
        x[0] = y;
        y = top;
    }
    writeLow(op, rf, 0, x, 16);
}

// SHA1H. Source Sn.
inline void
sha1Fixed(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    const uint32_t v = rol32(vget<uint32_t>(op.Vin(rf, 0), 0), 30);
    writeLow(op, rf, 0, &v, 4);
}

// SHA1SU0: sources Vd, Vn, Vm.
inline void
sha1Su0(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t d[2], n[2], m[2], r[2];
    std::memcpy(d, op.Vin(rf, 0), 16);
    std::memcpy(n, op.Vin(rf, 1), 16);
    std::memcpy(m, op.Vin(rf, 2), 16);
    r[0] = d[1] ^ d[0] ^ m[0];
    r[1] = n[0] ^ d[1] ^ m[1];
    writeLow(op, rf, 0, r, 16);
}

// SHA1SU1: sources Vd, Vn.
inline void
sha1Su1(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t d[4], n[4], t[4], r[4];
    std::memcpy(d, op.Vin(rf, 0), 16);
    std::memcpy(n, op.Vin(rf, 1), 16);
    t[0] = d[0] ^ n[1];
    t[1] = d[1] ^ n[2];
    t[2] = d[2] ^ n[3];
    t[3] = d[3];
    r[0] = rol32(t[0], 1);
    r[1] = rol32(t[1], 1);
    r[2] = rol32(t[2], 1);
    r[3] = rol32(t[3], 1) ^ rol32(t[0], 2);
    writeLow(op, rf, 0, r, 16);
}

// SHA256H (c 0): sources Vd, Vn, Vm. SHA256H2 (c 1): the same sources, with the state registers exchanged.
inline void
sha256Hash(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t x[4], y[4], w[4];
    std::memcpy(op.c ? y : x, op.Vin(rf, 0), 16);
    std::memcpy(op.c ? x : y, op.Vin(rf, 1), 16);
    std::memcpy(w, op.Vin(rf, 2), 16);
    for ( unsigned e = 0; e < 4; ++e ) {
        const uint32_t chs = ((y[1] ^ y[2]) & y[0]) ^ y[2];
        const uint32_t maj = (x[0] & x[1]) | ((x[0] | x[1]) & x[2]);
        const uint32_t s1 = ror32(y[0], 6) ^ ror32(y[0], 11) ^ ror32(y[0], 25);
        const uint32_t s0 = ror32(x[0], 2) ^ ror32(x[0], 13) ^ ror32(x[0], 22);
        const uint32_t t = y[3] + s1 + chs + w[e];
        const uint32_t new_x = t + x[3];
        const uint32_t new_y = t + s0 + maj;
        // ROL(Y:X, 32): the top word of X moves to the bottom of Y, the top word of Y to the bottom of X.
        y[3] = y[2];
        y[2] = y[1];
        y[1] = y[0];
        y[0] = new_x;
        x[3] = x[2];
        x[2] = x[1];
        x[1] = x[0];
        x[0] = new_y;
    }
    writeLow(op, rf, 0, op.c ? y : x, 16);
}

// SHA256SU0: sources Vd, Vn.
inline void
sha256Su0(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t d[4], n[4], t[4], r[4];
    std::memcpy(d, op.Vin(rf, 0), 16);
    std::memcpy(n, op.Vin(rf, 1), 16);
    t[0] = d[1];
    t[1] = d[2];
    t[2] = d[3];
    t[3] = n[0];
    for ( unsigned e = 0; e < 4; ++e ) r[e] = (ror32(t[e], 7) ^ ror32(t[e], 18) ^ (t[e] >> 3)) + d[e];
    writeLow(op, rf, 0, r, 16);
}

// SHA256SU1: sources Vd, Vn, Vm.
inline void
sha256Su1(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t d[4], n[4], m[4], t0[4], r[4];
    std::memcpy(d, op.Vin(rf, 0), 16);
    std::memcpy(n, op.Vin(rf, 1), 16);
    std::memcpy(m, op.Vin(rf, 2), 16);
    t0[0] = n[1];
    t0[1] = n[2];
    t0[2] = n[3];
    t0[3] = m[0];
    const auto sig = [](uint32_t v) { return ror32(v, 17) ^ ror32(v, 19) ^ (v >> 10); };
    r[0] = sig(m[2]) + d[0] + t0[0];
    r[1] = sig(m[3]) + d[1] + t0[1];
    r[2] = sig(r[0]) + d[2] + t0[2];
    r[3] = sig(r[1]) + d[3] + t0[3];
    writeLow(op, rf, 0, r, 16);
}

// SHA512H (c 0), SHA512H2 (c 1), SHA512SU1 (c 2). Sources Vd, Vn, Vm.
inline void
sha512Three(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t w[2], x[2], y[2], v[2];
    std::memcpy(w, op.Vin(rf, 0), 16);
    std::memcpy(x, op.Vin(rf, 1), 16);
    std::memcpy(y, op.Vin(rf, 2), 16);
    const auto big1 = [](uint64_t a) { return ror64(a, 14) ^ ror64(a, 18) ^ ror64(a, 41); };
    const auto big0 = [](uint64_t a) { return ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39); };
    if ( op.c == 0 ) {
        v[1] = ((y[1] & x[0]) ^ (~y[1] & x[1])) + big1(y[1]) + w[1];
        const uint64_t tmp = v[1] + y[0];
        v[0] = ((tmp & y[1]) ^ (~tmp & x[0])) + big1(tmp) + w[0];
    }
    else if ( op.c == 1 ) {
        v[1] = ((x[0] & y[1]) ^ (x[0] & y[0]) ^ (y[1] & y[0])) + big0(y[0]) + w[1];
        v[0] = ((v[1] & y[0]) ^ (v[1] & y[1]) ^ (y[1] & y[0])) + big0(v[1]) + w[0];
    }
    else {
        const auto sig1 = [](uint64_t a) { return ror64(a, 19) ^ ror64(a, 61) ^ (a >> 6); };
        v[1] = w[1] + sig1(x[1]) + y[1];
        v[0] = w[0] + sig1(x[0]) + y[0];
    }
    writeLow(op, rf, 0, v, 16);
}

// SHA512SU0: sources Vd, Vn.
inline void
sha512Su0(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t w[2], x[2], v[2];
    std::memcpy(w, op.Vin(rf, 0), 16);
    std::memcpy(x, op.Vin(rf, 1), 16);
    const auto sig0 = [](uint64_t a) { return ror64(a, 1) ^ ror64(a, 8) ^ (a >> 7); };
    v[0] = w[0] + sig0(w[1]);
    v[1] = w[1] + sig0(x[0]);
    writeLow(op, rf, 0, v, 16);
}

// EOR3 (c 0), BCAX (c 1): sources Vn, Vm, Va. RAX1 (c 2): Vn, Vm. XAR (c 3): Vn, Vm, imm = rotation.
inline void
sha3Op(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint64_t r[2];
    for ( unsigned k = 0; k < 2; ++k ) {
        const uint64_t n = vget<uint64_t>(op.Vin(rf, 0), k), m = vget<uint64_t>(op.Vin(rf, 1), k);
        switch ( op.c ) {
        case 0: r[k] = n ^ m ^ vget<uint64_t>(op.Vin(rf, 2), k); break;
        case 1: r[k] = n ^ (m & ~vget<uint64_t>(op.Vin(rf, 2), k)); break;
        case 2: r[k] = n ^ rol64(m, 1); break;
        default: r[k] = ror64(n ^ m, static_cast<unsigned>(op.imm)); break;
        }
    }
    writeLow(op, rf, 0, r, 16);
}

// SM3SS1: sources Vn, Vm, Va.
inline void
sm3Ss1(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t r[4] = { 0, 0, 0, 0 };
    r[3] = rol32(rol32(vget<uint32_t>(op.Vin(rf, 0), 3), 12) + vget<uint32_t>(op.Vin(rf, 1), 3) + vget<uint32_t>(op.Vin(rf, 2), 3), 7);
    writeLow(op, rf, 0, r, 16);
}

// SM3TT1A (c 0), SM3TT1B (c 1), SM3TT2A (c 2), SM3TT2B (c 3). Sources Vd, Vn, Vm; imm = the word index of Vm.
inline void
sm3Tt(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t d[4], r[4];
    std::memcpy(d, op.Vin(rf, 0), 16);
    const uint32_t n3 = vget<uint32_t>(op.Vin(rf, 1), 3);
    const uint32_t wj = vget<uint32_t>(op.Vin(rf, 2), static_cast<unsigned>(op.imm));
    r[0] = d[1];
    r[2] = d[3];
    if ( op.c < 2 ) {
        const uint32_t ss2 = n3 ^ rol32(d[3], 12);
        const uint32_t tt = op.c == 0 ? (d[1] ^ d[3] ^ d[2]) : ((d[3] & d[1]) | (d[3] & d[2]) | (d[1] & d[2]));
        r[1] = rol32(d[2], 9);
        r[3] = tt + d[0] + ss2 + wj;
    }
    else {
        const uint32_t tt = (op.c == 2 ? (d[1] ^ d[3] ^ d[2]) : ((d[3] & d[2]) | (~d[3] & d[1]))) + d[0] + n3 + wj;
        r[1] = rol32(d[2], 19);
        r[3] = tt ^ rol32(tt, 9) ^ rol32(tt, 17);
    }
    writeLow(op, rf, 0, r, 16);
}

// SM3PARTW1 (c 0), SM3PARTW2 (c 1). Sources Vd, Vn, Vm.
inline void
sm3Part(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t d[4], n[4], m[4], r[4];
    std::memcpy(d, op.Vin(rf, 0), 16);
    std::memcpy(n, op.Vin(rf, 1), 16);
    std::memcpy(m, op.Vin(rf, 2), 16);
    if ( op.c == 0 ) {
        r[0] = (d[0] ^ n[0]) ^ rol32(m[1], 15);
        r[1] = (d[1] ^ n[1]) ^ rol32(m[2], 15);
        r[2] = (d[2] ^ n[2]) ^ rol32(m[3], 15);
        for ( unsigned i = 0; i < 4; ++i ) {
            if ( i == 3 ) r[3] = (d[3] ^ n[3]) ^ rol32(r[0], 15);
            r[i] = r[i] ^ rol32(r[i], 15) ^ rol32(r[i], 23);
        }
    }
    else {
        uint32_t tmp[4];
        for ( unsigned i = 0; i < 4; ++i ) {
            tmp[i] = n[i] ^ rol32(m[i], 7);
            r[i] = d[i] ^ tmp[i];
        }
        uint32_t t2 = rol32(tmp[0], 15);
        t2 = t2 ^ rol32(t2, 15) ^ rol32(t2, 23);
        r[3] ^= t2;
    }
    writeLow(op, rf, 0, r, 16);
}

inline uint8_t
sm4Sbox(uint8_t v)
{
    static const uint8_t box[256] = {
        0xd6, 0x90, 0xe9, 0xfe, 0xcc, 0xe1, 0x3d, 0xb7, 0x16, 0xb6, 0x14, 0xc2, 0x28, 0xfb, 0x2c, 0x05,
        0x2b, 0x67, 0x9a, 0x76, 0x2a, 0xbe, 0x04, 0xc3, 0xaa, 0x44, 0x13, 0x26, 0x49, 0x86, 0x06, 0x99,
        0x9c, 0x42, 0x50, 0xf4, 0x91, 0xef, 0x98, 0x7a, 0x33, 0x54, 0x0b, 0x43, 0xed, 0xcf, 0xac, 0x62,
        0xe4, 0xb3, 0x1c, 0xa9, 0xc9, 0x08, 0xe8, 0x95, 0x80, 0xdf, 0x94, 0xfa, 0x75, 0x8f, 0x3f, 0xa6,
        0x47, 0x07, 0xa7, 0xfc, 0xf3, 0x73, 0x17, 0xba, 0x83, 0x59, 0x3c, 0x19, 0xe6, 0x85, 0x4f, 0xa8,
        0x68, 0x6b, 0x81, 0xb2, 0x71, 0x64, 0xda, 0x8b, 0xf8, 0xeb, 0x0f, 0x4b, 0x70, 0x56, 0x9d, 0x35,
        0x1e, 0x24, 0x0e, 0x5e, 0x63, 0x58, 0xd1, 0xa2, 0x25, 0x22, 0x7c, 0x3b, 0x01, 0x21, 0x78, 0x87,
        0xd4, 0x00, 0x46, 0x57, 0x9f, 0xd3, 0x27, 0x52, 0x4c, 0x36, 0x02, 0xe7, 0xa0, 0xc4, 0xc8, 0x9e,
        0xea, 0xbf, 0x8a, 0xd2, 0x40, 0xc7, 0x38, 0xb5, 0xa3, 0xf7, 0xf2, 0xce, 0xf9, 0x61, 0x15, 0xa1,
        0xe0, 0xae, 0x5d, 0xa4, 0x9b, 0x34, 0x1a, 0x55, 0xad, 0x93, 0x32, 0x30, 0xf5, 0x8c, 0xb1, 0xe3,
        0x1d, 0xf6, 0xe2, 0x2e, 0x82, 0x66, 0xca, 0x60, 0xc0, 0x29, 0x23, 0xab, 0x0d, 0x53, 0x4e, 0x6f,
        0xd5, 0xdb, 0x37, 0x45, 0xde, 0xfd, 0x8e, 0x2f, 0x03, 0xff, 0x6a, 0x72, 0x6d, 0x6c, 0x5b, 0x51,
        0x8d, 0x1b, 0xaf, 0x92, 0xbb, 0xdd, 0xbc, 0x7f, 0x11, 0xd9, 0x5c, 0x41, 0x1f, 0x10, 0x5a, 0xd8,
        0x0a, 0xc1, 0x31, 0x88, 0xa5, 0xcd, 0x7b, 0xbd, 0x2d, 0x74, 0xd0, 0x12, 0xb8, 0xe5, 0xb4, 0xb0,
        0x89, 0x69, 0x97, 0x4a, 0x0c, 0x96, 0x77, 0x7e, 0x65, 0xb9, 0xf1, 0x09, 0xc5, 0x6e, 0xc6, 0x84,
        0x18, 0xf0, 0x7d, 0xec, 0x3a, 0xdc, 0x4d, 0x20, 0x79, 0xee, 0x5f, 0x3e, 0xd7, 0xcb, 0x39, 0x48
    };
    return box[v];
}

// The round function of SM4E (key = false) and SM4EKEY (key = true) over four round keys.
inline void
sm4Rounds(const uint32_t* input, const uint32_t* keys, uint32_t* out, bool key)
{
    uint32_t s[4];
    std::memcpy(s, input, 16);
    for ( unsigned k = 0; k < 4; ++k ) {
        uint32_t v = s[3] ^ s[2] ^ s[1] ^ keys[k];
        uint32_t sub = 0;
        for ( unsigned b = 0; b < 4; ++b ) {
            sub |= static_cast<uint32_t>(sm4Sbox(static_cast<uint8_t>(v >> (8 * b)))) << (8 * b);
        }
        v = key ? (sub ^ rol32(sub, 13) ^ rol32(sub, 23))
                : (sub ^ rol32(sub, 2) ^ rol32(sub, 10) ^ rol32(sub, 18) ^ rol32(sub, 24));
        v ^= s[0];
        s[0] = s[1];
        s[1] = s[2];
        s[2] = s[3];
        s[3] = v;
    }
    std::memcpy(out, s, 16);
}

// SM4E: sources Vd, Vn. SM4EKEY (c 1): sources Vn, Vm.
inline void
sm4(VanadisArm64Inst& op, VanadisRegisterFile* rf)
{
    uint32_t a[4], b[4], r[4];
    std::memcpy(a, op.Vin(rf, 0), 16);
    std::memcpy(b, op.Vin(rf, 1), 16);
    sm4Rounds(a, b, r, op.c == 1);
    writeLow(op, rf, 0, r, 16);
}

} // namespace A64Crypto
} // namespace Vanadis
} // namespace SST

#endif
