# Every Reason Your Code Is Slow — the measurements

Source for every number in the video **["Every Reason Your Code Is Slow, Explained in 14 Minutes"](https://youtu.be/kwTcK_RMnz4)**
(channel: Twelve Reasons). Watch the video: https://youtu.be/kwTcK_RMnz4 Each benchmark is a single C or Python file; no dependencies beyond
clang, pthreads and NumPy (item 11 only).

| # | Reason | File | What it measures | Ratio in the video |
|---|---|---|---|---|
| 1 | Walking memory in the wrong order | `bench/cache_bench.c` | row-major vs column-major sum of an N×N grid | 11× (59× with vectorisation, item 6) |
| 2 | Pointer chasing | `bench/pointer_chase.c` | summing an array vs a linked list, nodes adjacent vs scattered | 18× / 630× |
| 3 | Hash map outgrew the cache | `bench/hashmap_bench.c` | one lookup at table sizes from 32 KB to 128 MB | 21× |
| 4 | False sharing | `bench/false_sharing.c` | two threads incrementing adjacent vs padded counters | 2.8× |
| 5 | Branch misprediction | `bench/branch_pred.c` | conditional sum over random vs sorted data | 10× |
| 6 | SIMD, the wide lane | `bench/cache_bench.c` built with and without vectorisation | same loop, compiler allowed/forbidden to vectorise | 11× → 59× |
| 7 | Allocating in the hot loop | `bench/alloc_loop.c` | malloc/free per iteration vs one reused buffer, 64 B and 1 MiB | 35× / 1,100× |
| 8 | Lock contention | `bench/lock_contention.c` | 4 threads, 10 M increments: mutex vs per-thread totals | 250× |
| 9 | One byte at a time | `bench/small_io.c` | writing 64 MB with 1 B … 1 MiB per write() | 2,500× |
| 10 | Copying | `bench/copy_bench.c` | 64 KB argument by value vs by pointer; string append without reserve | 800× / quadratic |
| 11 | The interpreter tax | `bench/interp_overhead.py` | Python for-loop sum vs NumPy over 10 M floats | 228× |
| 12 | You never measured | — | the three traps: cold cache, dead code elimination, averaging noise | — |

## Run

```sh
cd bench
make
./cache_bench && ./pointer_chase && ./hashmap_bench && ./false_sharing && ./branch_pred \
  && ./alloc_loop && ./lock_contention && ./small_io && ./copy_bench
python3 interp_overhead.py          # needs numpy
clang -O2 -fno-vectorize -fno-slp-vectorize cache_bench.c -o cache_bench_novec && ./cache_bench_novec
```

Every program prints the minimum of several repetitions, which is the number used in the video.
Measured on an Apple M1 (8 GB), macOS, clang 17, October 2026. Your absolute numbers will differ;
the ratios should not, much. If one of yours disagrees by more than 2×, open an issue with your
CPU and compiler: that is interesting.

## License

MIT. Use anything here in your own benchmarks, talks or videos.
