// Measures malloc/work/free versus buffer reuse for 64 bytes and 1 MiB.
// Both variants write/read the first and last byte, not the entire allocation.
// Pointer escape barriers keep allocations and memory work observable to clang.
// Untimed warm-up, minimum repeats and checked checksums control DCE and noise.
// Expected: reuse wins; 1 MiB allocation need not imply mmap on macOS.
#include "bench.h"

static inline int64_t work(unsigned char *p, size_t bytes, size_t i) {
    p[0] = (unsigned char)i;
    p[bytes - 1] = (unsigned char)(i >> 8);
    escape(p);
    return (int64_t)p[0] + p[bytes - 1];
}
NOINLINE static int64_t allocating(size_t n, size_t bytes, unsigned char *unused) {
    (void)unused;
    int64_t sum = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char *p = allocate(bytes);
        sum += work(p, bytes, i);
        free(p);
    }
    return sum;
}
NOINLINE static int64_t reusing(size_t n, size_t bytes, unsigned char *p) {
    int64_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum += work(p, bytes, i);
    return sum;
}
typedef int64_t (*work_fn)(size_t, size_t, unsigned char *);
static work_fn volatile functions[] = {allocating, reusing};

int main(int argc, char **argv) {
    int quick = quick_mode(argc, argv), reps = quick ? 2 : 7;
    performance_qos();
    const size_t sizes[] = {64, 1048576};
    const char *names[] = {"malloc_free", "reuse"};
    puts("variant,bytes,iterations,reps,quick,min_ns_per_iteration,checksum");
    for (int s = 0; s < 2; ++s) {
        size_t bytes = sizes[s], n = s ? (quick ? 32 : 1000) : (quick ? 10000 : 1000000);
        unsigned char *p = allocate(bytes);
        int64_t expected = 0;
        for (size_t i = 0; i < n; ++i) expected += (int64_t)(i & 255) + ((i >> 8) & 255);
        double best[] = {1e300, 1e300};
        for (int v = 0; v < 2; ++v) check(functions[v](n, bytes, p), expected);
        for (int r = 0; r < reps; ++r) {
            for (int v = 0; v < 2; ++v) {
                uint64_t start = now_ns();
                int64_t sum = functions[v](n, bytes, p);
                double elapsed = (double)(now_ns() - start);
                check(sum, expected);
                best[v] = minimum(best[v], elapsed / (double)n);
            }
        }
        for (int v = 0; v < 2; ++v)
            printf("%s,%zu,%zu,%d,%d,%.4f,%" PRId64 "\n", names[v], bytes, n, reps, quick, best[v], expected);
        free(p);
    }
}
