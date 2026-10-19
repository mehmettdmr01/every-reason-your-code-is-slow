// hashmap_bench.c: the same hash map, the same load factor, growing table sizes.
// Lookup cost per hit as the table outgrows L1, L2 and finally RAM.
//
// Open addressing, linear probing, 64-bit keys, load factor fixed at 0.5 for every size, so
// the number of probes per lookup stays the same and only the memory behaviour changes.
// Keys looked up in random order (a dependent chain: each key indexes the next, so the
// lookups cannot overlap; this measures latency, not throughput).
//
// Build: cc -O2 -std=c11 -o hashmap_bench hashmap_bench.c
// Usage: ./hashmap_bench [--max-mb 512] [--reps 5]
// CSV: entries,table_bytes,load,reps,min_ns_per_lookup,median_ns_per_lookup
#define _DARWIN_C_SOURCE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static inline uint64_t rng_next(void) {
    uint64_t x = rng_state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static inline uint64_t hash64(uint64_t k) {  /* splitmix64 finaliser */
    k += 0x9E3779B97F4A7C15ull;
    k = (k ^ (k >> 30)) * 0xBF58476D1CE4E5B9ull;
    k = (k ^ (k >> 27)) * 0x94D049BB133111EBull;
    return k ^ (k >> 31);
}

typedef struct { uint64_t key; uint64_t value; } slot_t;  /* key 0 = empty */

static void insert(slot_t *table, uint64_t mask, uint64_t key, uint64_t value) {
    uint64_t i = hash64(key) & mask;
    while (table[i].key != 0 && table[i].key != key) i = (i + 1) & mask;
    table[i].key = key; table[i].value = value;
}

__attribute__((noinline)) static uint64_t lookup(const slot_t *table, uint64_t mask, uint64_t key) {
    uint64_t i = hash64(key) & mask;
    while (table[i].key != key) {
        if (table[i].key == 0) return 0;
        i = (i + 1) & mask;
    }
    return table[i].value;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    size_t max_mb = 512; int reps = 5;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--max-mb") && i + 1 < argc) max_mb = (size_t)strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
    }
    printf("entries,table_bytes,load,reps,min_ns_per_lookup,median_ns_per_lookup\n");
    for (uint64_t entries = 1u << 10; ; entries <<= 1) {
        uint64_t capacity = entries * 2;                       /* load factor 0.5 */
        size_t bytes = capacity * sizeof(slot_t);
        if (bytes > max_mb * 1024 * 1024) break;
        slot_t *table = calloc(capacity, sizeof(slot_t));
        uint64_t *keys = malloc(entries * sizeof(uint64_t));
        if (!table || !keys) { fprintf(stderr, "out of memory at %" PRIu64 " entries\n", entries); break; }
        /* Keys form one random cycle over the entries: value of key k is the next key to look up. */
        for (uint64_t k = 0; k < entries; k++) keys[k] = k + 1;   /* never 0 */
        for (uint64_t k = entries - 1; k > 0; k--) {               /* Sattolo shuffle: single cycle */
            uint64_t j = rng_next() % k;
            uint64_t t = keys[k]; keys[k] = keys[j]; keys[j] = t;
        }
        for (uint64_t k = 0; k < entries; k++) insert(table, capacity - 1, keys[k], keys[(k + 1) % entries]);
        uint64_t hops = entries * 4;
        if (hops < (1u << 22)) hops = 1u << 22;
        if (hops > (1u << 24)) hops = 1u << 24;
        double *samples = malloc(sizeof(double) * (size_t)reps);
        uint64_t key = keys[0];
        for (uint64_t h = 0; h < entries; h++) key = lookup(table, capacity - 1, key);  /* warm-up */
        for (int r = 0; r < reps; r++) {
            uint64_t t0 = now_ns();
            for (uint64_t h = 0; h < hops; h++) key = lookup(table, capacity - 1, key);
            samples[r] = (double)(now_ns() - t0) / (double)hops;
        }
        if (key == 0) fprintf(stderr, "lookup chain broke\n");
        qsort(samples, (size_t)reps, sizeof(double), cmp);
        printf("%" PRIu64 ",%zu,0.5,%d,%.2f,%.2f\n", entries, bytes, reps, samples[0], samples[reps / 2]);
        fflush(stdout);
        free(samples); free(keys); free(table);
    }
    return 0;
}
