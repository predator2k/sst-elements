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

#ifndef _PHYSMEMMANAGER_H
#define _PHYSMEMMANAGER_H

#include <stddef.h>
#include <vector>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <assert.h>
#include <sstream>

#include "output.h"
#include "vanadisDbgFlags.h"

#define FOUR_KB 4096
#define TWO_MB ( 1024*1024*2)
#define ONE_GB ( 1024*1024*1024)

class PhysMemManager {
  public:
    class BitMap {
      public:
        BitMap( int size ) {
             m_bitMap.resize( size/64, 0 );
        }
        void print() {
            for ( size_t i = 0; i < m_bitMap.size(); i++ ) {
                printf("0x%016" PRIx64 "\n",m_bitMap[i]);
            }
            printf("\n");
        }
        bool getBit( int pos ) { return ( m_bitMap.at( pos/64 ) >> ( pos % 64 ) ) & 1; }
        void setBit( int pos ) { m_bitMap.at( pos/64 ) |= ( 1UL << ( pos % 64 ) ); }
        void clearBit( int pos ) { m_bitMap.at( pos/64 ) &= ~( 1UL << ( pos % 64 ) ); }

        //! Bits of the map, one per 4 KiB page.
        size_t bits() const { return m_bitMap.size() * 64; }

        size_t findFirstEmptyBit( size_t start ) {
            for ( size_t i = start/64; i < m_bitMap.size(); i++ ) {
                if ( m_bitMap[i] != 0xffffffffffff ) {

                    for ( int j = i == start/64 ? start % 64: 0; j < 64; j++ ) {
                        if ( ! getBit( i * 64 + j ) ) {
                            return i * 64 + j;
                        }
                    }
                }
            }
            throw -1;
        }

        bool findEmptyBits( size_t start, int numBits ) {
            assert(0);
        }

        void setBits( int start, int numBits ) {
            assert(0);
        }

        void checkpoint( FILE* fp ) {
            fprintf(fp,"BitMap size: %zu\n",m_bitMap.size());
            for ( auto i = 0; i < m_bitMap.size(); i++ ) {
                if ( m_bitMap[i] ) {
                    fprintf(fp,"%d %#018" PRIx64 "\n",i,m_bitMap[i]);
                }
            }
        }

        void checkpointLoad( SST::Output* output, FILE* fp ) {
            int size;
            assert( 1 == fscanf(fp,"BitMap size: %d\n",&size) );
            output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"BitMap size: %d\n",size);
            m_bitMap.resize(size,0);
            int index;
            uint64_t value;
            while ( 2 == fscanf( fp, "%d %" PRIx64 "\n", &index, &value ) ) {
                output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"%d %#018" PRIx64 "\n",index,value);
                m_bitMap[index] = value;
            }
        }

      private:

      private:
        std::vector<uint64_t> m_bitMap;
    };

  public:

    typedef std::vector<uint32_t> PageList;
    enum PageSize { FourKB, TwoMB, OneGB };
    PhysMemManager( size_t memSize ) : m_bitMap( memSize/4096), m_numAllocated(0) { }
    ~PhysMemManager() {
#if 0
        if ( m_numAllocated > 1 ) {
            printf("%s() numAllocated=%" PRIu64 "\n",__func__,m_numAllocated);
        }
#endif
    }

    void allocPages( PageSize pageSize, int numPages, PageList& pagesOut ) {
        while ( numPages ) {
            pagesOut.push_back( findFreePage( pageSize, UINT32_MAX ) );
            --numPages;
        }
    }

    uint32_t allocPage( PageSize pageSize ) {
        return findFreePage( pageSize, UINT32_MAX );
    }

    // A page for virtual page vpn: in the random order, one whose number has the colour of vpn (setRandomOrder).
    uint32_t allocPage( PageSize pageSize, uint32_t vpn ) {
        return findFreePage( pageSize, vpn );
    }

    /*
     * Gives out the following 4 KiB pages in a random order instead of the first free page, as an operating system that
     * hands out pages freed in the order the system freed them does, so the physical colours of a buffer's pages, and with
     * them its sets in a physically indexed cache, are unrelated to its virtual addresses.
     * Handling: each later allocation starts its search for a free page at a position drawn from a xorshift generator
     * with this seed. With colors above 1, a page given out for a known virtual page has the same number modulo colors
     * as that virtual page, as a cache indexed by more bits than the page offset (a virtually indexed L1) sees it.
     * Emits nothing.
     */
    void setRandomOrder( uint64_t seed, uint32_t colors ) {
        m_random_state = seed ^ 0x9E3779B97F4A7C15ull;
        if ( 0 == m_random_state ) m_random_state = 0x9E3779B97F4A7C15ull;
        m_colors = colors > 0 ? colors : 1;
    }


    void freePages( PageSize pageSize, PageList& pagesIn ) {
        for ( size_t i = 0; i < pagesIn.size(); i++ ) {
            freePage( pageSize, pagesIn[i] );
        }
    }

    void freePage( size_t pageSize, size_t pageNum ) {
        if ( FourKB == pageSize ) {
            --m_numAllocated;
            m_bitMap.clearBit( pageNum );
        } else {
            assert(0);
        }
    }

    void checkpoint( SST::Output* output, std::string dir ) {
        std::stringstream filename;
        filename << dir << "/" << "PhysMemManager";
        auto fp = fopen(filename.str().c_str(),"w+");

        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"PhysMemManager %s\n", filename.str().c_str());

        fprintf(fp,"m_numAllocated %" PRIu64 "\n",m_numAllocated);
        m_bitMap.checkpoint(fp);
        if ( 0 != m_random_state ) fprintf(fp,"m_random_state %" PRIu64 " %" PRIu32 "\n",m_random_state,m_colors);
    }
    void checkpointLoad( SST::Output* output , std::string dir ) {
        std::stringstream filename;
        filename << dir << "/" << "PhysMemManager";
        auto fp = fopen(filename.str().c_str(),"r");
        assert(fp);

        assert( 1 == fscanf(fp,"m_numAllocated %" SCNu64 "\n",&m_numAllocated) );
        output->verbose(CALL_INFO, 0, VANADIS_DBG_CHECKPOINT,"m_numAllocated %" PRIu64 "\n",m_numAllocated);
        m_bitMap.checkpointLoad(output,fp);
        uint64_t state = 0;
        uint32_t colors = 1;
        if ( 2 == fscanf(fp,"m_random_state %" SCNu64 " %" SCNu32 "\n",&state,&colors) ) {
            m_random_state = state;
            m_colors = colors;
        }
    }

  private:
    int calcNumNeeded( PageSize pageSize ) {
        switch( pageSize ) {
          case FourKB: return 1;
          case TwoMB: return TWO_MB/FOUR_KB;
          case OneGB: return ONE_GB/FOUR_KB;
          default: assert(0);
        }
    }

    // A free page whose number is vpn modulo the colours, searched from start onwards with wrap-around.
    size_t findFreeColoredPage( size_t start, uint32_t vpn ) {
        const size_t pages = m_bitMap.bits();
        const size_t color = vpn % m_colors;
        size_t page = start - start % m_colors + color;
        for ( size_t tried = 0; tried < pages; tried += m_colors, page += m_colors ) {
            if ( page >= pages ) page = color;
            if ( ! m_bitMap.getBit( page ) ) return page;
        }
        throw -1;
    }

    int findFreePage( PageSize pageSize, uint32_t vpn ) {
        if ( FourKB == pageSize ) {
            size_t start = 0;
            if ( 0 != m_random_state ) {
                m_random_state ^= m_random_state << 13;
                m_random_state ^= m_random_state >> 7;
                m_random_state ^= m_random_state << 17;
                start = m_random_state % m_bitMap.bits();
            }
            size_t page;
            if ( 0 != m_random_state && m_colors > 1 && UINT32_MAX != vpn ) {
                page = findFreeColoredPage( start, vpn );
            } else {
                try {
                    page = m_bitMap.findFirstEmptyBit(start);
                } catch ( int ) {
                    page = m_bitMap.findFirstEmptyBit(0);
                }
            }
            m_bitMap.setBit( page );
            ++m_numAllocated;
            return page;
        }
        assert(0);

        int startPage = 0;
        int numNeeded = calcNumNeeded( pageSize );

        while ( 1 ) {
            startPage = m_bitMap.findFirstEmptyBit( startPage );

            // if the page is aligned
            if ( 0 == startPage % numNeeded ) {
                // check to see if there are enough empty 4K pages to cover this page size
                if ( m_bitMap.findEmptyBits( startPage, numNeeded ) ) {
                    m_bitMap.setBits( startPage, numNeeded );
                    return startPage;
                } else {
                    // move the start page enough 4K pages to keep aligned
                    startPage += numNeeded;
                }
            } else {
                // was not aligned, move the start page by one
                startPage += 1;
            }
        }
    }

    BitMap m_bitMap;
    uint64_t m_numAllocated;
    //! State of the generator of setRandomOrder (0 gives out the first free page) and its colours.
    uint64_t m_random_state = 0;
    uint32_t m_colors = 1;
};

#endif
