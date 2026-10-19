// false_sharing.c: two (or more) threads each increment their own counter.
// "packed": the counters sit next to each other, in one cache line.
// "padded": each counter gets its own line (PAD bytes apart).
// The work is identical; only the addresses differ.
//
// Build: cc -O2 -std=c11 -pthread -o false_sharing false_sharing.c
// Usage: ./false_sharing [--threads 2] [--iters 100000000] [--reps 5]
// Output CSV: layout,threads,iters,reps,min_ms,median_ms
#define _DARWIN_C_SOURCE
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef PAD
#define PAD 128 /* one line on Apple M1; two lines on x86, still separate */
#endif
#define MAX_THREADS 8

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

typedef struct {
    volatile uint64_t *counter; /* volatile: the increment must go to memory every time */
    uint64_t iters;
} job_t;

static void *work(void *arg) {
    job_t *job = arg;
    volatile uint64_t *c = job->counter;
    for (uint64_t i = 0; i < job->iters; i++)
        (*c)++;
    return NULL;
}

/* All counters live in one buffer; `stride` bytes between them. */
static double run(int threads, uint64_t iters, size_t stride, uint8_t *buf) {
    pthread_t tid[MAX_THREADS];
    job_t jobs[MAX_THREADS];
    memset(buf, 0, stride * MAX_THREADS + 256);
    uint64_t t0 = now_ns();
    for (int t = 0; t < threads; t++) {
        jobs[t].counter = (volatile uint64_t *)(buf + (size_t)t * stride);
        jobs[t].iters = iters;
        pthread_create(&tid[t], NULL, work, &jobs[t]);
    }
    for (int t = 0; t < threads; t++)
        pthread_join(tid[t], NULL);
    double ms = (double)(now_ns() - t0) / 1e6;
    for (int t = 0; t < threads; t++)
        if (*(volatile uint64_t *)(buf + (size_t)t * stride) != iters)
            fprintf(stderr, "counter %d wrong\n", t);
    return ms;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    int threads = 2, reps = 5;
    uint64_t iters = 100000000ull;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--iters") && i + 1 < argc) iters = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
    }
    if (threads < 1 || threads > MAX_THREADS) return 1;
    /* Line-aligned buffer, so "packed" really shares one line and "padded" really does not. */
    uint8_t *buf = NULL;
    if (posix_memalign((void **)&buf, 4096, PAD * MAX_THREADS + 4096)) return 1;

    printf("layout,threads,iters,reps,min_ms,median_ms\n");
    struct { const char *name; size_t stride; } layouts[] = { {"packed", sizeof(uint64_t)}, {"padded", PAD} };
    for (int l = 0; l < 2; l++) {
        double *ms = malloc(sizeof(double) * (size_t)reps);
        for (int r = 0; r < reps; r++)
            ms[r] = run(threads, iters, layouts[l].stride, buf);
        qsort(ms, (size_t)reps, sizeof(double), cmp);
        printf("%s,%d,%" PRIu64 ",%d,%.1f,%.1f\n", layouts[l].name, threads, iters, reps, ms[0], ms[reps / 2]);
        fflush(stdout);
        free(ms);
    }
    free(buf);
    return 0;
}
