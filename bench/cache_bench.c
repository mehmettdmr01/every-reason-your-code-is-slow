// cache_bench.c: real measurements for "Same Code, 10x Slower".
//
// Three experiments, one CSV on stdout:
//   traverse  row-major vs column-major sum of an N x N int32 matrix
//   stride    touch every K-th byte of a large buffer
//   chase     pointer chasing through a random cycle, working set 16 KB .. max
//
// Build: see Makefile (two binaries: default -O2 and -O2 without vectorization).
// Usage: ./cache_bench [--max-mb 128] [--quick]

#define _DARWIN_C_SOURCE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#endif

#ifndef BUILD_TAG
#define BUILD_TAG "unknown"
#endif

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// xorshift64*: deterministic, no libc rand() quality surprises.
static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static inline uint64_t rng_next(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void report(const char *test, const char *variant, uint64_t param,
                   uint64_t bytes, double *samples, int reps) {
    qsort(samples, (size_t)reps, sizeof(double), cmp_double);
    double min = samples[0];
    double med = samples[reps / 2];
    printf("%s,%s,%s,%" PRIu64 ",%" PRIu64 ",%d,%.4f,%.4f\n", BUILD_TAG, test,
           variant, param, bytes, reps, min, med);
    fflush(stdout);
}

// ---------------------------------------------------------------- traverse

__attribute__((noinline)) static int64_t sum_rows(const int32_t *a, size_t n) {
    int64_t s = 0;
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++)
            s += a[i * n + j];
    return s;
}

__attribute__((noinline)) static int64_t sum_cols(const int32_t *a, size_t n) {
    int64_t s = 0;
    for (size_t j = 0; j < n; j++)
        for (size_t i = 0; i < n; i++)
            s += a[i * n + j];
    return s;
}

// Both functions are pure, so the optimizer would happily fold repeated calls
// into one. Calling through volatile pointers keeps every call real.
typedef int64_t (*sum_fn)(const int32_t *, size_t);
static sum_fn volatile call_rows = sum_rows;
static sum_fn volatile call_cols = sum_cols;

static void run_traverse(size_t n, int reps) {
    size_t count = n * n;
    int32_t *a = malloc(count * sizeof(int32_t));
    if (!a) {
        fprintf(stderr, "traverse: malloc failed for n=%zu\n", n);
        return;
    }
    for (size_t k = 0; k < count; k++)
        a[k] = (int32_t)(rng_next() & 0xFF);

    // Small matrices finish in microseconds; repeat until a sample is >= ~20 ms.
    int inner = 1;
    {
        uint64_t t0 = now_ns();
        volatile int64_t sink = call_rows(a, n);
        (void)sink;
        uint64_t dt = now_ns() - t0;
        if (dt < 20000000ull)
            inner = (int)(20000000ull / (dt + 1)) + 1;
    }

    double *rows = malloc(sizeof(double) * (size_t)reps);
    double *cols = malloc(sizeof(double) * (size_t)reps);
    int64_t check_r = 0, check_c = 0;
    for (int r = 0; r < reps; r++) {
        uint64_t t0 = now_ns();
        for (int k = 0; k < inner; k++)
            check_r = call_rows(a, n);
        uint64_t t1 = now_ns();
        for (int k = 0; k < inner; k++)
            check_c = call_cols(a, n);
        uint64_t t2 = now_ns();
        rows[r] = (double)(t1 - t0) / ((double)inner * (double)count);
        cols[r] = (double)(t2 - t1) / ((double)inner * (double)count);
    }
    if (check_r != check_c)
        fprintf(stderr, "traverse: checksum mismatch at n=%zu\n", n);

    report("traverse", "row", n, count * sizeof(int32_t), rows, reps);
    report("traverse", "col", n, count * sizeof(int32_t), cols, reps);
    free(rows);
    free(cols);
    free(a);
}

// ------------------------------------------------------------------ stride

__attribute__((noinline)) static void touch_stride(uint8_t *buf, size_t len,
                                                   size_t step) {
#pragma clang loop vectorize(disable) interleave(disable)
    for (size_t i = 0; i < len; i += step)
        buf[i] = (uint8_t)(buf[i] * 3u + 1u);
}

static void run_stride(size_t len, int reps) {
    uint8_t *buf = malloc(len);
    if (!buf) {
        fprintf(stderr, "stride: malloc failed\n");
        return;
    }
    for (size_t k = 0; k < len; k++)
        buf[k] = (uint8_t)k;

    double *samples = malloc(sizeof(double) * (size_t)reps);
    for (size_t step = 1; step <= 4096; step *= 2) {
        size_t touched = (len + step - 1) / step;
        for (int r = 0; r < reps; r++) {
            uint64_t t0 = now_ns();
            touch_stride(buf, len, step);
            uint64_t t1 = now_ns();
            samples[r] = (double)(t1 - t0) / (double)touched;
        }
        report("stride", "bytes", step, len, samples, reps);
    }
    free(samples);
    free(buf);
}

// ------------------------------------------------------------------- chase

// Each node fills one 64-byte line so every hop is a fresh line.
typedef struct {
    uint32_t next;
    uint8_t pad[60];
} node_t;

_Static_assert(sizeof(node_t) == 64, "node must be 64 bytes");

__attribute__((noinline)) static uint32_t chase(const node_t *nodes,
                                                uint32_t start, size_t hops) {
    uint32_t idx = start;
    for (size_t h = 0; h < hops; h++)
        idx = nodes[idx].next;
    return idx;
}

static void run_chase(size_t bytes, int reps) {
    size_t n = bytes / sizeof(node_t);
    if (n < 2 || n > 0xFFFFFFFFull)
        return;
    node_t *nodes = malloc(n * sizeof(node_t));
    uint32_t *perm = malloc(n * sizeof(uint32_t));
    if (!nodes || !perm) {
        fprintf(stderr, "chase: malloc failed for %zu bytes\n", bytes);
        free(nodes);
        free(perm);
        return;
    }
    memset(nodes, 0, n * sizeof(node_t));
    for (size_t k = 0; k < n; k++)
        perm[k] = (uint32_t)k;
    // Sattolo: a single cycle visiting every node exactly once.
    for (size_t k = n - 1; k > 0; k--) {
        size_t j = (size_t)(rng_next() % k);
        uint32_t t = perm[k];
        perm[k] = perm[j];
        perm[j] = t;
    }
    for (size_t k = 0; k < n; k++)
        nodes[k].next = perm[k];
    free(perm);

    size_t hops = n * 4;
    if (hops < (1u << 22))
        hops = 1u << 22;
    if (hops > (1u << 24))
        hops = 1u << 24;

    volatile uint32_t sink = chase(nodes, 0, n); // warm-up: page everything in
    (void)sink;

    double *samples = malloc(sizeof(double) * (size_t)reps);
    for (int r = 0; r < reps; r++) {
        uint64_t t0 = now_ns();
        sink = chase(nodes, (uint32_t)(r % n), hops);
        uint64_t t1 = now_ns();
        samples[r] = (double)(t1 - t0) / (double)hops;
    }
    report("chase", "random", bytes / 1024, bytes, samples, reps);
    free(samples);
    free(nodes);
}

// -------------------------------------------------------------------- main

int main(int argc, char **argv) {
    size_t max_mb = 128;
    int quick = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--max-mb") && i + 1 < argc)
            max_mb = (size_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--quick"))
            quick = 1;
    }

#ifdef __APPLE__
    // Ask for a performance core; macOS has no hard pinning.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

    int reps = quick ? 5 : 15;
    printf("build,test,variant,param,bytes,reps,min_ns_per_op,median_ns_per_op\n");

    // 4000 sits next to 4096 on purpose: power-of-two columns alias in the cache.
    static const size_t sizes[] = {128, 256, 512, 1024, 2048, 4000, 4096, 8192};
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        size_t n = sizes[k];
        if (n * n * sizeof(int32_t) > max_mb * 1024 * 1024 * 2)
            continue;
        run_traverse(n, reps);
    }

    run_stride(max_mb * 1024 * 1024, quick ? 3 : 9);

    for (size_t bytes = 16 * 1024; bytes <= max_mb * 1024 * 1024; bytes *= 2)
        run_chase(bytes, quick ? 3 : 7);

    return 0;
}
