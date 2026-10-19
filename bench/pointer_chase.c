// Measures summing identical int64 values in an array and a shuffled linked list.
// Allocation, deterministic shuffling and initialization are outside timing.
// One full warm-up, minimum of repeats, volatile call targets and checked sums.
// Expected: dependent random loads slow down sharply beyond cache; ratio varies.
// Build with -O2; --quick keeps all four sizes but reduces passes and repeats.
#include "bench.h"

typedef struct node { int64_t value; struct node *next; } node;
NOINLINE static int64_t array_sum(const int64_t *a, const node *head, size_t n) {
    (void)head;
    int64_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum += a[i];
    return sum;
}
NOINLINE static int64_t list_sum(const int64_t *a, const node *head, size_t n) {
    (void)a;
    int64_t sum = 0;
    for (size_t i = 0; i < n; ++i) { sum += head->value; head = head->next; }
    return sum;
}
typedef int64_t (*sum_fn)(const int64_t *, const node *, size_t);
static sum_fn volatile functions[] = { array_sum, list_sum };

int main(int argc, char **argv) {
    int quick = quick_mode(argc, argv), reps = quick ? 2 : 7;
    performance_qos();
    const size_t sizes[] = {1024, 16384, 262144, 4194304};
    const char *names[] = {"contiguous", "random_list"};
    puts("variant,n,working_set_bytes,passes,reps,quick,min_ns_per_element,checksum");
    for (size_t k = 0; k < 4; ++k) {
        size_t n = sizes[k];
        int64_t *a = allocate(n * sizeof(*a));
        node *nodes = allocate(n * sizeof(*nodes));
        uint32_t *order = allocate(n * sizeof(*order));
        int64_t expected = 0;
        for (size_t i = 0; i < n; ++i) {
            a[i] = nodes[i].value = (int64_t)(i & 255);
            expected += a[i]; order[i] = (uint32_t)i;
        }
        uint64_t rng = UINT64_C(0x9E3779B97F4A7C15);
        for (size_t i = n - 1; i > 0; --i) {
            size_t j = random_u64(&rng) % (i + 1);
            uint32_t t = order[i]; order[i] = order[j]; order[j] = t;
        }
        for (size_t i = 0; i < n; ++i)
            nodes[order[i]].next = &nodes[order[(i + 1) % n]];
        node *head = &nodes[order[0]];
        free(order);
        size_t target = quick ? 16384 : 1048576;
        size_t passes = n < target ? target / n : 1;
        double best[] = {1e300, 1e300};
        for (int v = 0; v < 2; ++v) check(functions[v](a, head, n), expected);
        for (int r = 0; r < reps; ++r) {
            for (int v = 0; v < 2; ++v) {
                int64_t result = 0;
                uint64_t start = now_ns();
                for (size_t p = 0; p < passes; ++p) result += functions[v](a, head, n);
                double elapsed = (double)(now_ns() - start);
                check(result, expected * (int64_t)passes);
                best[v] = minimum(best[v], elapsed / (double)(n * passes));
            }
        }
        for (int v = 0; v < 2; ++v)
            printf("%s,%zu,%zu,%zu,%d,%d,%.4f,%" PRId64 "\n", names[v], n,
                   n * (v ? sizeof(node) : sizeof(int64_t)), passes, reps, quick, best[v], expected);
        free(nodes); free(a);
    }
}
