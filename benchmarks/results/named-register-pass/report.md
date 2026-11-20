# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T14:27:38.024330+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 6.74 GiB available at capture
- Operating system: Windows-11-10.0.26300-SP0
- Python: 3.13.15
- Z+ compiler: Z+ Compiler 1.3.0
- Compiler build configuration: Release
- Compiler executable: `C:\Users\miros\OneDrive\Desktop\Projects\Z+ compiler\bin\zp.exe`

## Methodology

Each timing is process wall-clock time measured with `time.perf_counter_ns()` and includes process startup, source loading, and (for `zp run`) interpreter/compiler startup. It is not pure function execution time. Native compilation is measured separately and excluded from execution timings. Every timed run is a fresh process.

- Warm-ups per available mode: 3
- Measured runs per mode: 7
- Timeout per process: 120.0 s
- Checksums are checked against a separately computed deterministic reference before any run is accepted.
- Python and Z+ use the same loop counts and arithmetic expressions for each workload. Z+ prints its expected checksum only if its computed numeric checksum matches; otherwise it emits `CHECKSUM_ERROR`.
- Native compile failures are recorded as unsupported with the original diagnostic; there is no interpreter fallback.

Workloads: arithmetic/modulo accumulation; mixed fractional/integer arithmetic; function calls; conditional branches; combined scalar work; indexed array updates; mutable struct-field updates; and repeated managed-array allocation.

## Results

| Workload | Iterations | Checksum | Z+ Interpreter | Z+ Native | Python | C++ O2 | C++ O3 | Native vs C++ O2 | Native vs C++ O3 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| tight_loop | 1000000 | 1499999500000 | 3875.404 ms | 21.649 ms | 299.069 ms | 29.027 ms | 26.231 ms | 1.341× | 1.212× |
| arithmetic | 50000 | 2399670 | 265.466 ms | 27.481 ms | 73.546 ms | 24.448 ms | 23.886 ms | 0.890× | 0.869× |
| mixed | 10000 | 155000 | 85.023 ms | 26.631 ms | 75.888 ms | 27.156 ms | 28.680 ms | 1.020× | 1.077× |
| functions | 50000 | 2492250 | 443.380 ms | 28.538 ms | 79.236 ms | 25.014 ms | 27.585 ms | 0.877× | 0.967× |
| combined | 50000 | 3034241 | 584.467 ms | 33.304 ms | 101.566 ms | 27.598 ms | 27.873 ms | 0.829× | 0.837× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| tight_loop | 44.951 ms | success |
| arithmetic | 78.915 ms | success |
| mixed | 71.935 ms | success |
| functions | 52.955 ms | success |
| combined | 70.809 ms | success |

## Run statistics

### tight_loop

- interpreter: min 3727.325 ms; max 4151.035 ms; mean 3893.485 ms; median 3875.404 ms; standard deviation 140.698 ms; complete.
- native: min 21.313 ms; max 22.977 ms; mean 21.889 ms; median 21.649 ms; standard deviation 0.606 ms; complete.
- python: min 291.669 ms; max 314.836 ms; mean 301.852 ms; median 299.069 ms; standard deviation 9.053 ms; complete.
- cpp_O2: min 24.755 ms; max 30.847 ms; mean 28.015 ms; median 29.027 ms; standard deviation 2.310 ms; complete.
- cpp_O3: min 24.506 ms; max 28.948 ms; mean 26.816 ms; median 26.231 ms; standard deviation 1.873 ms; complete.

### arithmetic

- interpreter: min 258.378 ms; max 352.619 ms; mean 287.880 ms; median 265.466 ms; standard deviation 35.985 ms; complete.
- native: min 26.852 ms; max 29.663 ms; mean 27.762 ms; median 27.481 ms; standard deviation 0.948 ms; complete.
- python: min 71.263 ms; max 78.781 ms; mean 74.698 ms; median 73.546 ms; standard deviation 2.668 ms; complete.
- cpp_O2: min 23.849 ms; max 27.388 ms; mean 24.720 ms; median 24.448 ms; standard deviation 1.211 ms; complete.
- cpp_O3: min 23.585 ms; max 27.330 ms; mean 24.402 ms; median 23.886 ms; standard deviation 1.315 ms; complete.

### mixed

- interpreter: min 83.358 ms; max 98.367 ms; mean 87.021 ms; median 85.023 ms; standard deviation 5.192 ms; complete.
- native: min 24.372 ms; max 30.726 ms; mean 27.327 ms; median 26.631 ms; standard deviation 2.351 ms; complete.
- python: min 64.705 ms; max 81.421 ms; mean 72.754 ms; median 75.888 ms; standard deviation 6.869 ms; complete.
- cpp_O2: min 26.028 ms; max 31.083 ms; mean 27.939 ms; median 27.156 ms; standard deviation 1.922 ms; complete.
- cpp_O3: min 23.434 ms; max 31.458 ms; mean 27.771 ms; median 28.680 ms; standard deviation 3.157 ms; complete.

### functions

- interpreter: min 398.794 ms; max 479.875 ms; mean 440.555 ms; median 443.380 ms; standard deviation 30.317 ms; complete.
- native: min 28.158 ms; max 29.245 ms; mean 28.598 ms; median 28.538 ms; standard deviation 0.475 ms; complete.
- python: min 78.685 ms; max 108.325 ms; mean 84.200 ms; median 79.236 ms; standard deviation 10.950 ms; complete.
- cpp_O2: min 23.884 ms; max 28.484 ms; mean 25.831 ms; median 25.014 ms; standard deviation 1.914 ms; complete.
- cpp_O3: min 24.567 ms; max 28.708 ms; mean 26.719 ms; median 27.585 ms; standard deviation 1.829 ms; complete.

### combined

- interpreter: min 551.113 ms; max 659.622 ms; mean 592.826 ms; median 584.467 ms; standard deviation 35.781 ms; complete.
- native: min 30.818 ms; max 36.592 ms; mean 33.487 ms; median 33.304 ms; standard deviation 1.883 ms; complete.
- python: min 98.163 ms; max 118.098 ms; mean 103.277 ms; median 101.566 ms; standard deviation 6.946 ms; complete.
- cpp_O2: min 25.476 ms; max 33.980 ms; mean 28.127 ms; median 27.598 ms; standard deviation 2.832 ms; complete.
- cpp_O3: min 26.372 ms; max 29.964 ms; mean 28.141 ms; median 27.873 ms; standard deviation 1.389 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 2.68× to 13.81× across the checksum-verified comparable workloads; its largest ratio was on `tight_loop`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.077× to 0.893×. The weakest ratio was `tight_loop` (12.96× slower than Python); `mixed` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
