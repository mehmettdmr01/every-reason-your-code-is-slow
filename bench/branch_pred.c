// Measures a threshold sum on shuffled/sorted ints and a branchless shuffled sum.
// Identical values and checksums; sorting and allocation are outside timing.
// Warm-up and minimum repeats; volatile call targets prevent call elimination.
// A conditional volatile asm barrier keeps the branch; all variants are scalar.
// Expected: random branches cost more than sorted branches; --quick uses 65536 ints.
#include "bench.h"

NOINLINE static int64_t branched(const int32_t *a, size_t n) {
    int64_t sum = 0;
    for (size_t i = 0; i < n; ++i) {
        int64_t x = a[i];
        if (x >= 128) {
            sum += x;
            __asm__ volatile("" : "+r"(sum) : : "memory");
        }
    }
    return sum;
}
NOINLINE static int64_t branchless(const int32_t *a, size_t n) {
    int64_t sum = 0;
    for (size_t i = 0; i < n; ++i) {
        int64_t x = a[i];
        int64_t masked = x & -(int64_t)(x >= 128);
        __asm__ volatile("" : "+r"(masked));
        sum += masked;
    }
    return sum;
}
static int compare_ints(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}
typedef int64_t (*sum_fn)(const int32_t *, size_t);
static sum_fn volatile functions[] = {branched, branched, branchless};

int main(int argc, char **argv) {
    int quick = quick_mode(argc, argv), reps = quick ? 2 : 7;
    size_t n = quick ? 65536 : 1048576;
    performance_qos();
    int32_t *a = allocate(n * sizeof(*a)), *sorted = allocate(n * sizeof(*sorted));
    uint64_t rng = UINT64_C(0x9E3779B97F4A7C15);
    int64_t expected = 0;
    for (size_t i = 0; i < n; ++i) {
        a[i] = (int32_t)(random_u64(&rng) & 255);
        if (a[i] >= 128) expected += a[i];
    }
    memcpy(sorted, a, n * sizeof(*a));
    qsort(sorted, n, sizeof(*sorted), compare_ints);
    const int32_t *inputs[] = {a, sorted, a};
    const char *names[] = {"unsorted_branch", "sorted_branch", "unsorted_branchless"};
    double best[] = {1e300, 1e300, 1e300};
    for (int v = 0; v < 3; ++v) check(functions[v](inputs[v], n), expected);
    for (int r = 0; r < reps; ++r) {
        for (int v = 0; v < 3; ++v) {
            uint64_t start = now_ns();
            int64_t sum = functions[v](inputs[v], n);
            double elapsed = (double)(now_ns() - start);
            check(sum, expected);
            best[v] = minimum(best[v], elapsed / (double)n);
        }
    }
    puts("variant,n,reps,quick,min_ns_per_element,checksum");
    for (int v = 0; v < 3; ++v)
        printf("%s,%zu,%d,%d,%.4f,%" PRId64 "\n", names[v], n, reps, quick, best[v], expected);
    free(sorted); free(a);
}
