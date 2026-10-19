# Measures float64 totals: Python for/ndarray, sum(ndarray), np.sum and sum(list).
# Array/list construction is outside timing; identical exactly representable data.
# Each variant warms up once; report minimum repeats and verify every result.
# Expected: NumPy avoids per-element interpreter work; list sum avoids NumPy boxing.
# Default 10M elements, --quick 1M; stdout is CSV and no matrix product is measured.
import argparse
import csv
import gc
import sys
import time

try:
    import numpy as np
except ImportError:
    sys.exit("NumPy gerekli; NumPy bulunan Python'u PYTHON=/path/to/python ile secin.")


def python_for(values):
    total = 0.0
    for value in values:
        total += value
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    n, reps = (1_000_000, 2) if args.quick else (10_000_000, 5)
    values = np.ones(n, dtype=np.float64)
    as_list = values.tolist()
    variants = (
        ("python_for_ndarray", python_for, values),
        ("sum_ndarray", sum, values),
        ("numpy_sum", np.sum, values),
        ("sum_list", sum, as_list),
    )
    expected = float(n)
    best = {name: float("inf") for name, _, _ in variants}
    gc_enabled = gc.isenabled()
    gc.disable()
    try:
        for name, function, data in variants:
            if float(function(data)) != expected:
                raise RuntimeError(f"{name}: warm-up checksum mismatch")
        for _ in range(reps):
            for name, function, data in variants:
                start = time.perf_counter_ns()
                result = function(data)
                elapsed = time.perf_counter_ns() - start
                if float(result) != expected:
                    raise RuntimeError(f"{name}: checksum mismatch")
                best[name] = min(best[name], elapsed / n)
    finally:
        if gc_enabled:
            gc.enable()
    writer = csv.writer(sys.stdout)
    writer.writerow(("variant", "n", "reps", "quick", "python_version", "numpy_version", "min_ns_per_element", "checksum"))
    for name, _, _ in variants:
        writer.writerow((name, n, reps, int(args.quick), sys.version.split()[0], np.__version__, f"{best[name]:.4f}", f"{expected:.1f}"))


if __name__ == "__main__":
    main()
