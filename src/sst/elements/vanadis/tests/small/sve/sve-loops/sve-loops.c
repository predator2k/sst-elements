/* Loops the compiler vectorizes with SVE predicates, gathers and first-fault loads, and a first-fault load that
   reaches an unmapped page: the first element is valid, so FFR must report where the mapping stopped. */
#include <arm_sve.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#define N 4001
static double x[N], y[N];
static int idx[N];
static float f[N];

static size_t sve_strlen(const char* s)
{
    size_t i = 0;
    svbool_t all = svptrue_b8();
    for (;;) {
        svsetffr();
        svuint8_t v = svldff1_u8(all, (const uint8_t*)s + i);
        svbool_t ok = svrdffr();
        svbool_t zero = svcmpeq_n_u8(ok, v, 0);
        if (svptest_any(ok, zero)) return i + svcntp_b8(ok, svbrkb_z(ok, zero));
        i += svcntp_b8(all, ok);
    }
}

int main(void)
{
    for (int i = 0; i < N; ++i) {
        x[i] = i * 0.5;
        y[i] = 1.0;
        idx[i] = (i * 7) % N;
        f[i] = (float)i;
    }
    double dot = 0;
    for (int i = 0; i < N; ++i) dot += x[i] * y[i];
    double gat = 0;
    for (int i = 0; i < N; ++i) gat += x[idx[i]];
    float m = 0;
    for (int i = 0; i < N; ++i) {
        if (f[i] > m) m = f[i];
    }
    for (int i = 0; i < N; ++i) y[idx[i]] = x[i] + 1.0;
    double sc = 0;
    for (int i = 0; i < N; ++i) sc += y[i];

    const size_t page = 4096;
    char* p = mmap(0, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    munmap(p + page, page);
    memset(p, 'q', page);
    size_t total = 0;
    for (size_t len = 1; len < 200; len += 13) {
        char* s = p + page - len;
        s[len - 1] = 0;
        total += sve_strlen(s);
        s[len - 1] = 'q';
    }
    printf("sve %.3f %.3f %.1f %.3f %zu\n", dot, gat, m, sc, total);
    return 0;
}
