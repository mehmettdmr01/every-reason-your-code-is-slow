#ifndef BENCH_H
#define BENCH_H
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
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

#define NOINLINE __attribute__((noinline))
static volatile int64_t bench_sink;

static inline void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}
static inline void *allocate(size_t bytes) {
    void *p = malloc(bytes);
    if (!p) fail("malloc failed");
    return p;
}
static inline void performance_qos(void) {
#ifdef __APPLE__
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}
static inline int quick_mode(int argc, char **argv) {
    if (argc == 1) return 0;
    if (argc == 2 && !strcmp(argv[1], "--quick")) return 1;
    fprintf(stderr, "Usage: %s [--quick]\n", argv[0]);
    exit(EXIT_FAILURE);
}
static inline uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) fail("clock_gettime failed");
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static inline void escape(const void *p) {
    __asm__ volatile("" : : "r"(p) : "memory");
}
static inline void check(int64_t got, int64_t expected) {
    bench_sink = got;
    if (got != expected) fail("checksum mismatch");
}
static inline uint64_t random_u64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *state = x;
    return x * UINT64_C(0x2545F4914F6CDD1D);
}
static inline double minimum(double a, double b) { return a < b ? a : b; }
#endif
