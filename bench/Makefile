CC = clang
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wpedantic
LDLIBS = -pthread
PROGRAMS = cache_bench pointer_chase hashmap_bench false_sharing branch_pred alloc_loop lock_contention small_io copy_bench

.PHONY: all clean
all: $(PROGRAMS)

# The branch test must not be vectorised away; the SIMD gap in item 6 is cache_bench built twice:
#   make cache_bench                       (vectorised, -O2)
#   clang -O2 -fno-vectorize -fno-slp-vectorize cache_bench.c -o cache_bench_novec
branch_pred: CFLAGS += -fno-vectorize -fno-slp-vectorize

$(PROGRAMS): %: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f $(PROGRAMS) cache_bench_novec
