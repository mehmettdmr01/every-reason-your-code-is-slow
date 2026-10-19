// Measures four threads doing 10M total increments: mutex, atomic, local+merge.
// Threads wait at a shared start gate; creation and joins are outside timing.
// Locals stay in registers; an asm operand prevents replacing the loop by n.
// Untimed warm-up, minimum repeats and exact final counters check correctness.
// Expected: one local merge wins; --quick uses 40000 total increments.
#include "bench.h"
#include <pthread.h>
#include <stdatomic.h>

enum { THREADS = 4 };
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int ready, started, done;
_Alignas(128) static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
_Alignas(128) static uint64_t shared_counter;
_Alignas(128) static _Atomic uint64_t atomic_counter;
typedef struct { uint64_t iterations, result; int variant; } job;

static void pt(int rc) { if (rc) { fprintf(stderr, "pthread: %s\n", strerror(rc)); exit(1); } }
static void *worker(void *arg) {
    job *j = arg;
    performance_qos();
    uint64_t n = j->iterations, local = 0;
    int variant = j->variant;
    pt(pthread_mutex_lock(&gate));
    ++ready;
    pt(pthread_cond_broadcast(&condition));
    while (!started) pt(pthread_cond_wait(&condition, &gate));
    pt(pthread_mutex_unlock(&gate));
    if (variant == 0) {
        for (uint64_t i = 0; i < n; ++i) {
            pt(pthread_mutex_lock(&counter_mutex));
            ++shared_counter;
            pt(pthread_mutex_unlock(&counter_mutex));
        }
    } else if (variant == 1) {
        for (uint64_t i = 0; i < n; ++i)
            atomic_fetch_add_explicit(&atomic_counter, 1, memory_order_relaxed);
    } else {
        for (uint64_t i = 0; i < n; ++i) {
            ++local;
            __asm__ volatile("" : "+r"(local));
        }
    }
    pt(pthread_mutex_lock(&gate));
    j->result = local;
    ++done;
    pt(pthread_cond_broadcast(&condition));
    pt(pthread_mutex_unlock(&gate));
    return NULL;
}
static double run(int variant, uint64_t total) {
    pthread_t threads[THREADS];
    job jobs[THREADS];
    ready = started = done = 0;
    shared_counter = 0;
    atomic_store_explicit(&atomic_counter, 0, memory_order_relaxed);
    for (int i = 0; i < THREADS; ++i) {
        jobs[i] = (job){total / THREADS, 0, variant};
        pt(pthread_create(&threads[i], NULL, worker, &jobs[i]));
    }
    pt(pthread_mutex_lock(&gate));
    while (ready != THREADS) pt(pthread_cond_wait(&condition, &gate));
    uint64_t start = now_ns();
    started = 1;
    pt(pthread_cond_broadcast(&condition));
    while (done != THREADS) pt(pthread_cond_wait(&condition, &gate));
    uint64_t result = 0;
    if (variant == 2) for (int i = 0; i < THREADS; ++i) result += jobs[i].result;
    else if (variant == 1) result = atomic_load_explicit(&atomic_counter, memory_order_relaxed);
    else result = shared_counter;
    double ms = (double)(now_ns() - start) / 1e6;
    pt(pthread_mutex_unlock(&gate));
    for (int i = 0; i < THREADS; ++i) pt(pthread_join(threads[i], NULL));
    check((int64_t)result, (int64_t)total);
    return ms;
}
int main(int argc, char **argv) {
    int quick = quick_mode(argc, argv), reps = quick ? 2 : 5;
    uint64_t total = quick ? 40000 : 10000000;
    performance_qos();
    const char *names[] = {"mutex", "atomic_relaxed", "local_merge"};
    double best[] = {1e300, 1e300, 1e300};
    for (int v = 0; v < 3; ++v) (void)run(v, 40000);
    for (int r = 0; r < reps; ++r)
        for (int v = 0; v < 3; ++v) best[v] = minimum(best[v], run(v, total));
    puts("variant,threads,total_increments,increments_per_thread,reps,quick,min_ms,checksum");
    for (int v = 0; v < 3; ++v)
        printf("%s,%d,%" PRIu64 ",%" PRIu64 ",%d,%d,%.4f,%" PRIu64 "\n",
               names[v], THREADS, total, total / THREADS, reps, quick, best[v], total);
}
