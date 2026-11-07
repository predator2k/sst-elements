/* Memory order under out-of-order load issue: loads and stores of several widths that alias at random, addresses that come
   from loads, and copies through registers. The program prints a checksum, which must equal the output on the hardware. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { WORDS = 512, ROUNDS = 60000 };

static uint64_t mem64[WORDS];
static uint32_t index_table[WORDS];

static inline uint32_t next_random(uint32_t* x)
{
    *x = *x * 1664525u + 1013904223u;
    return *x >> 8;
}

int main(void)
{
    uint32_t x = 20261106;
    for (int i = 0; i < WORDS; ++i) {
        mem64[i] = (uint64_t)next_random(&x) << 32 | next_random(&x);
        index_table[i] = next_random(&x) % WORDS;
    }
    uint64_t sum = 0;
    uint8_t* bytes = (uint8_t*)mem64;
    uint16_t* halves = (uint16_t*)mem64;
    uint32_t* words = (uint32_t*)mem64;
    for (int r = 0; r < ROUNDS; ++r) {
        const uint32_t a = next_random(&x) % WORDS;
        const uint32_t b = index_table[a];
        const uint32_t c = index_table[b];
        const uint32_t d = (uint32_t)(mem64[c] % WORDS);
        switch (r & 7) {
        case 0:
            mem64[a] += mem64[b];
            sum ^= mem64[a];
            break;
        case 1:
            words[2 * a + (r >> 3 & 1)] = words[2 * b] + 1;
            sum += mem64[a];
            break;
        case 2:
            bytes[8 * a + (r & 7)] = (uint8_t)(mem64[d] >> 3);
            sum = sum * 3 + bytes[8 * a + (r >> 3 & 7)];
            break;
        case 3:
            halves[4 * b + 1] = (uint16_t)sum;
            sum += words[2 * b];
            break;
        case 4:
            mem64[d] ^= sum;
            mem64[index_table[d]] += mem64[d];
            break;
        case 5:
        {
            uint64_t pair[2] = { mem64[a], mem64[b] };
            memcpy(&mem64[c], pair, c + 1 < WORDS ? sizeof pair : sizeof pair[0]);
            sum += mem64[c];
            break;
        }
        case 6:
            index_table[a] = (uint32_t)(mem64[b] % WORDS);
            sum ^= (uint64_t)index_table[a] << 7;
            break;
        default:
            memmove(&bytes[8 * a], &bytes[8 * a + 3], 5);
            sum += bytes[8 * a];
            break;
        }
    }
    for (int i = 0; i < WORDS; ++i) sum = sum * 31 + mem64[i] + index_table[i];
    printf("memorder %llu\n", (unsigned long long)sum);
    return 0;
}
