# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T18:49:33.988895+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 6.95 GiB available at capture
- Operating system: Windows-11-10.0.26300-SP0
- Python: 3.13.15
- Z+ compiler: Z+ Compiler 1.3.0
- Compiler build configuration: Release
- Compiler executable: `C:\Users\miros\OneDrive\Desktop\Projects\Z+ compiler\bin\zp.exe`

## Methodology

Each timing is process wall-clock time measured with `time.perf_counter_ns()` and includes process startup, source loading, and (for `zp run`) interpreter/compiler startup. It is not pure function execution time. Native compilation is measured separately and excluded from execution timings. Every timed run is a fresh process.

- Warm-ups per available mode: 2
- Measured runs per mode: 5
- Timeout per process: 120.0 s
- Checksums are checked against a separately computed deterministic reference before any run is accepted.
- Python and Z+ use the same loop counts and arithmetic expressions for each workload. Z+ prints its expected checksum only if its computed numeric checksum matches; otherwise it emits `CHECKSUM_ERROR`.
- Native compile failures are recorded as unsupported with the original diagnostic; there is no interpreter fallback.

Workloads: arithmetic/modulo accumulation; mixed fractional/integer arithmetic; function calls; conditional branches; combined scalar work; indexed array updates; mutable struct-field updates; and repeated managed-array allocation.

## Results

| Workload | Iterations | Checksum | Z+ Interpreter | Z+ Native | Python | C++ O2 | C++ O3 | Native vs C++ O2 | Native vs C++ O3 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| arithmetic | 50000 | 2399670 | 275.438 ms | 28.357 ms | 82.417 ms | 26.279 ms | 26.729 ms | 0.927× | 0.943× |
| mixed | 10000 | 155000 | 90.435 ms | 25.344 ms | 65.433 ms | 27.756 ms | 31.709 ms | 1.095× | 1.251× |
| functions | 50000 | 2492250 | 386.682 ms | 27.700 ms | 82.469 ms | 28.530 ms | 27.860 ms | 1.030× | 1.006× |
| branching | 50000 | 3124925000 | 358.038 ms | 28.975 ms | 76.995 ms | 24.890 ms | 29.972 ms | 0.859× | 1.034× |
| combined | 50000 | 3034241 | 564.962 ms | 35.180 ms | 112.635 ms | 30.834 ms | 32.120 ms | 0.876× | 0.913× |
| arrays | 10000 | 29994 | 80.971 ms | 31.872 ms | 66.657 ms | 28.450 ms | 31.208 ms | 0.893× | 0.979× |
| structs | 10000 | 39994 | 81.883 ms | 28.366 ms | 62.660 ms | 26.673 ms | 24.651 ms | 0.940× | 0.869× |
| memory | 10000 | 69995 | 89.184 ms | 29.740 ms | 68.946 ms | 25.753 ms | 26.200 ms | 0.866× | 0.881× |
| tight_loop | 1000000 | 1499999500000 | 4248.942 ms | 21.537 ms | 341.523 ms | 26.399 ms | 28.558 ms | 1.226× | 1.326× |
| recursion | 50000 | 1191560 | 1498.324 ms | 47.754 ms | 104.141 ms | 26.436 ms | 25.431 ms | 0.554× | 0.533× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 52.549 ms | success |
| mixed | 49.110 ms | success |
| functions | 59.763 ms | success |
| branching | 55.297 ms | success |
| combined | 56.277 ms | success |
| arrays | 60.558 ms | success |
| structs | 58.115 ms | success |
| memory | 54.552 ms | success |
| tight_loop | 42.021 ms | success |
| recursion | 52.762 ms | success |

## Run statistics

### arithmetic

- interpreter: min 267.048 ms; max 288.039 ms; mean 277.781 ms; median 275.438 ms; standard deviation 9.572 ms; complete.
- native: min 26.891 ms; max 31.215 ms; mean 28.816 ms; median 28.357 ms; standard deviation 1.784 ms; complete.
- python: min 76.701 ms; max 93.170 ms; mean 84.075 ms; median 82.417 ms; standard deviation 6.936 ms; complete.
- cpp_O2: min 24.138 ms; max 28.626 ms; mean 26.429 ms; median 26.279 ms; standard deviation 1.913 ms; complete.
- cpp_O3: min 24.464 ms; max 27.416 ms; mean 26.127 ms; median 26.729 ms; standard deviation 1.248 ms; complete.

### mixed

- interpreter: min 89.070 ms; max 104.595 ms; mean 92.800 ms; median 90.435 ms; standard deviation 6.622 ms; complete.
- native: min 25.016 ms; max 28.203 ms; mean 25.854 ms; median 25.344 ms; standard deviation 1.336 ms; complete.
- python: min 62.342 ms; max 72.025 ms; mean 66.983 ms; median 65.433 ms; standard deviation 4.358 ms; complete.
- cpp_O2: min 23.582 ms; max 28.657 ms; mean 26.478 ms; median 27.756 ms; standard deviation 2.532 ms; complete.
- cpp_O3: min 29.733 ms; max 32.129 ms; mean 31.279 ms; median 31.709 ms; standard deviation 1.015 ms; complete.

### functions

- interpreter: min 372.547 ms; max 436.810 ms; mean 399.716 ms; median 386.682 ms; standard deviation 29.762 ms; complete.
- native: min 27.589 ms; max 29.691 ms; mean 28.070 ms; median 27.700 ms; standard deviation 0.909 ms; complete.
- python: min 75.993 ms; max 84.070 ms; mean 80.831 ms; median 82.469 ms; standard deviation 3.306 ms; complete.
- cpp_O2: min 24.806 ms; max 33.555 ms; mean 28.504 ms; median 28.530 ms; standard deviation 3.381 ms; complete.
- cpp_O3: min 24.235 ms; max 28.955 ms; mean 27.430 ms; median 27.860 ms; standard deviation 1.842 ms; complete.

### branching

- interpreter: min 335.966 ms; max 385.957 ms; mean 357.219 ms; median 358.038 ms; standard deviation 19.313 ms; complete.
- native: min 28.066 ms; max 31.065 ms; mean 29.497 ms; median 28.975 ms; standard deviation 1.430 ms; complete.
- python: min 73.835 ms; max 85.287 ms; mean 79.131 ms; median 76.995 ms; standard deviation 5.136 ms; complete.
- cpp_O2: min 24.623 ms; max 29.944 ms; mean 26.338 ms; median 24.890 ms; standard deviation 2.340 ms; complete.
- cpp_O3: min 24.461 ms; max 32.126 ms; mean 29.652 ms; median 29.972 ms; standard deviation 3.088 ms; complete.

### combined

- interpreter: min 552.161 ms; max 634.657 ms; mean 585.540 ms; median 564.962 ms; standard deviation 37.972 ms; complete.
- native: min 32.024 ms; max 36.149 ms; mean 34.152 ms; median 35.180 ms; standard deviation 1.942 ms; complete.
- python: min 102.336 ms; max 118.105 ms; mean 112.472 ms; median 112.635 ms; standard deviation 6.277 ms; complete.
- cpp_O2: min 29.872 ms; max 35.504 ms; mean 31.863 ms; median 30.834 ms; standard deviation 2.312 ms; complete.
- cpp_O3: min 28.666 ms; max 35.918 ms; mean 32.148 ms; median 32.120 ms; standard deviation 2.665 ms; complete.

### arrays

- interpreter: min 78.482 ms; max 100.262 ms; mean 87.582 ms; median 80.971 ms; standard deviation 10.957 ms; complete.
- native: min 29.336 ms; max 36.317 ms; mean 32.974 ms; median 31.872 ms; standard deviation 3.003 ms; complete.
- python: min 61.273 ms; max 77.153 ms; mean 66.962 ms; median 66.657 ms; standard deviation 6.234 ms; complete.
- cpp_O2: min 26.696 ms; max 35.148 ms; mean 29.989 ms; median 28.450 ms; standard deviation 3.416 ms; complete.
- cpp_O3: min 27.128 ms; max 34.884 ms; mean 31.614 ms; median 31.208 ms; standard deviation 3.029 ms; complete.

### structs

- interpreter: min 73.459 ms; max 122.869 ms; mean 91.671 ms; median 81.883 ms; standard deviation 20.043 ms; complete.
- native: min 27.892 ms; max 30.018 ms; mean 28.888 ms; median 28.366 ms; standard deviation 1.040 ms; complete.
- python: min 60.322 ms; max 66.198 ms; mean 63.193 ms; median 62.660 ms; standard deviation 2.252 ms; complete.
- cpp_O2: min 24.205 ms; max 27.767 ms; mean 26.287 ms; median 26.673 ms; standard deviation 1.392 ms; complete.
- cpp_O3: min 23.698 ms; max 28.207 ms; mean 25.548 ms; median 24.651 ms; standard deviation 1.864 ms; complete.

### memory

- interpreter: min 87.218 ms; max 96.290 ms; mean 90.503 ms; median 89.184 ms; standard deviation 3.608 ms; complete.
- native: min 29.335 ms; max 30.651 ms; mean 29.882 ms; median 29.740 ms; standard deviation 0.520 ms; complete.
- python: min 62.955 ms; max 71.189 ms; mean 67.118 ms; median 68.946 ms; standard deviation 3.859 ms; complete.
- cpp_O2: min 24.088 ms; max 27.488 ms; mean 25.976 ms; median 25.753 ms; standard deviation 1.421 ms; complete.
- cpp_O3: min 24.331 ms; max 34.376 ms; mean 27.423 ms; median 26.200 ms; standard deviation 4.119 ms; complete.

### tight_loop

- interpreter: min 4076.421 ms; max 4281.098 ms; mean 4202.067 ms; median 4248.942 ms; standard deviation 93.754 ms; complete.
- native: min 21.398 ms; max 25.034 ms; mean 22.380 ms; median 21.537 ms; standard deviation 1.541 ms; complete.
- python: min 318.600 ms; max 468.653 ms; mean 381.700 ms; median 341.523 ms; standard deviation 71.489 ms; complete.
- cpp_O2: min 24.781 ms; max 29.256 ms; mean 26.752 ms; median 26.399 ms; standard deviation 1.941 ms; complete.
- cpp_O3: min 25.422 ms; max 29.475 ms; mean 28.032 ms; median 28.558 ms; standard deviation 1.539 ms; complete.

### recursion

- interpreter: min 1462.264 ms; max 1529.287 ms; mean 1500.667 ms; median 1498.324 ms; standard deviation 27.153 ms; complete.
- native: min 34.485 ms; max 66.189 ms; mean 47.193 ms; median 47.754 ms; standard deviation 12.848 ms; complete.
- python: min 99.323 ms; max 138.975 ms; mean 109.911 ms; median 104.141 ms; standard deviation 16.414 ms; complete.
- cpp_O2: min 25.555 ms; max 27.279 ms; mean 26.415 ms; median 26.436 ms; standard deviation 0.647 ms; complete.
- cpp_O3: min 24.027 ms; max 29.279 ms; mean 26.348 ms; median 25.431 ms; standard deviation 2.496 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 2.09× to 15.86× across the checksum-verified comparable workloads; its largest ratio was on `tight_loop`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.070× to 0.823×. The weakest ratio was `recursion` (14.39× slower than Python); `arrays` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
