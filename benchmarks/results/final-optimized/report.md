# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T14:36:20.400636+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.17 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 285.750 ms | 29.565 ms | 84.193 ms | 29.517 ms | 30.490 ms | 0.998× | 1.031× |
| mixed | 10000 | 155000 | 99.109 ms | 29.922 ms | 63.815 ms | 27.511 ms | 27.611 ms | 0.919× | 0.923× |
| functions | 50000 | 2492250 | 401.092 ms | 29.986 ms | 95.059 ms | 30.259 ms | 29.509 ms | 1.009× | 0.984× |
| branching | 50000 | 3124925000 | 363.013 ms | 26.857 ms | 89.507 ms | 31.335 ms | 27.345 ms | 1.167× | 1.018× |
| combined | 50000 | 3034241 | 599.361 ms | 31.274 ms | 94.171 ms | 28.510 ms | 29.388 ms | 0.912× | 0.940× |
| arrays | 10000 | 29994 | 83.436 ms | 26.539 ms | 63.218 ms | 28.801 ms | 27.133 ms | 1.085× | 1.022× |
| structs | 10000 | 39994 | 77.993 ms | 29.043 ms | 59.383 ms | 26.320 ms | 30.222 ms | 0.906× | 1.041× |
| memory | 10000 | 69995 | 98.954 ms | 35.965 ms | 63.603 ms | 27.155 ms | 27.010 ms | 0.755× | 0.751× |
| tight_loop | 1000000 | 1499999500000 | 4295.510 ms | 21.461 ms | 324.729 ms | 25.710 ms | 26.461 ms | 1.198× | 1.233× |
| recursion | 50000 | 1191560 | 1525.416 ms | 35.627 ms | 134.026 ms | 30.999 ms | 31.006 ms | 0.870× | 0.870× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 53.322 ms | success |
| mixed | 62.400 ms | success |
| functions | 80.905 ms | success |
| branching | 65.122 ms | success |
| combined | 63.496 ms | success |
| arrays | 66.460 ms | success |
| structs | 54.072 ms | success |
| memory | 282.335 ms | success |
| tight_loop | 219.761 ms | success |
| recursion | 51.047 ms | success |

## Run statistics

### arithmetic

- interpreter: min 276.645 ms; max 329.616 ms; mean 298.952 ms; median 285.750 ms; standard deviation 23.763 ms; complete.
- native: min 27.932 ms; max 31.697 ms; mean 29.910 ms; median 29.565 ms; standard deviation 1.537 ms; complete.
- python: min 81.709 ms; max 90.068 ms; mean 84.968 ms; median 84.193 ms; standard deviation 3.186 ms; complete.
- cpp_O2: min 26.908 ms; max 30.849 ms; mean 29.175 ms; median 29.517 ms; standard deviation 1.726 ms; complete.
- cpp_O3: min 26.835 ms; max 33.301 ms; mean 30.129 ms; median 30.490 ms; standard deviation 2.354 ms; complete.

### mixed

- interpreter: min 81.535 ms; max 108.520 ms; mean 99.004 ms; median 99.109 ms; standard deviation 10.991 ms; complete.
- native: min 26.362 ms; max 36.520 ms; mean 31.167 ms; median 29.922 ms; standard deviation 4.661 ms; complete.
- python: min 61.336 ms; max 66.250 ms; mean 63.773 ms; median 63.815 ms; standard deviation 2.056 ms; complete.
- cpp_O2: min 23.939 ms; max 28.500 ms; mean 26.421 ms; median 27.511 ms; standard deviation 2.264 ms; complete.
- cpp_O3: min 24.426 ms; max 28.754 ms; mean 26.855 ms; median 27.611 ms; standard deviation 1.879 ms; complete.

### functions

- interpreter: min 364.754 ms; max 432.140 ms; mean 402.749 ms; median 401.092 ms; standard deviation 25.108 ms; complete.
- native: min 27.911 ms; max 31.041 ms; mean 29.678 ms; median 29.986 ms; standard deviation 1.269 ms; complete.
- python: min 90.122 ms; max 109.699 ms; mean 96.841 ms; median 95.059 ms; standard deviation 7.951 ms; complete.
- cpp_O2: min 23.843 ms; max 35.353 ms; mean 30.355 ms; median 30.259 ms; standard deviation 4.224 ms; complete.
- cpp_O3: min 28.698 ms; max 31.361 ms; mean 29.747 ms; median 29.509 ms; standard deviation 0.985 ms; complete.

### branching

- interpreter: min 342.746 ms; max 384.273 ms; mean 363.642 ms; median 363.013 ms; standard deviation 14.798 ms; complete.
- native: min 26.319 ms; max 28.616 ms; mean 27.149 ms; median 26.857 ms; standard deviation 0.892 ms; complete.
- python: min 78.080 ms; max 95.916 ms; mean 87.920 ms; median 89.507 ms; standard deviation 6.803 ms; complete.
- cpp_O2: min 26.590 ms; max 35.416 ms; mean 30.599 ms; median 31.335 ms; standard deviation 3.480 ms; complete.
- cpp_O3: min 26.232 ms; max 28.438 ms; mean 27.299 ms; median 27.345 ms; standard deviation 0.813 ms; complete.

### combined

- interpreter: min 559.650 ms; max 644.135 ms; mean 596.298 ms; median 599.361 ms; standard deviation 33.301 ms; complete.
- native: min 30.070 ms; max 31.974 ms; mean 31.129 ms; median 31.274 ms; standard deviation 0.697 ms; complete.
- python: min 90.038 ms; max 98.326 ms; mean 93.490 ms; median 94.171 ms; standard deviation 3.310 ms; complete.
- cpp_O2: min 27.006 ms; max 30.228 ms; mean 28.537 ms; median 28.510 ms; standard deviation 1.152 ms; complete.
- cpp_O3: min 28.195 ms; max 31.878 ms; mean 29.719 ms; median 29.388 ms; standard deviation 1.604 ms; complete.

### arrays

- interpreter: min 79.097 ms; max 87.872 ms; mean 83.475 ms; median 83.436 ms; standard deviation 3.495 ms; complete.
- native: min 26.306 ms; max 27.379 ms; mean 26.669 ms; median 26.539 ms; standard deviation 0.412 ms; complete.
- python: min 61.506 ms; max 70.269 ms; mean 64.224 ms; median 63.218 ms; standard deviation 3.527 ms; complete.
- cpp_O2: min 25.705 ms; max 31.258 ms; mean 28.432 ms; median 28.801 ms; standard deviation 2.049 ms; complete.
- cpp_O3: min 25.384 ms; max 30.858 ms; mean 27.626 ms; median 27.133 ms; standard deviation 2.377 ms; complete.

### structs

- interpreter: min 74.502 ms; max 85.289 ms; mean 78.762 ms; median 77.993 ms; standard deviation 3.960 ms; complete.
- native: min 28.154 ms; max 32.632 ms; mean 30.093 ms; median 29.043 ms; standard deviation 2.074 ms; complete.
- python: min 58.667 ms; max 67.215 ms; mean 61.137 ms; median 59.383 ms; standard deviation 3.523 ms; complete.
- cpp_O2: min 24.687 ms; max 27.221 ms; mean 26.163 ms; median 26.320 ms; standard deviation 1.035 ms; complete.
- cpp_O3: min 25.402 ms; max 30.576 ms; mean 28.675 ms; median 30.222 ms; standard deviation 2.396 ms; complete.

### memory

- interpreter: min 95.393 ms; max 107.089 ms; mean 99.855 ms; median 98.954 ms; standard deviation 4.329 ms; complete.
- native: min 32.996 ms; max 44.752 ms; mean 37.663 ms; median 35.965 ms; standard deviation 4.439 ms; complete.
- python: min 62.139 ms; max 71.405 ms; mean 65.727 ms; median 63.603 ms; standard deviation 3.967 ms; complete.
- cpp_O2: min 25.287 ms; max 28.705 ms; mean 27.211 ms; median 27.155 ms; standard deviation 1.386 ms; complete.
- cpp_O3: min 26.073 ms; max 30.126 ms; mean 27.764 ms; median 27.010 ms; standard deviation 1.711 ms; complete.

### tight_loop

- interpreter: min 4065.556 ms; max 4499.706 ms; mean 4273.464 ms; median 4295.510 ms; standard deviation 188.250 ms; complete.
- native: min 20.726 ms; max 23.123 ms; mean 21.613 ms; median 21.461 ms; standard deviation 0.900 ms; complete.
- python: min 314.034 ms; max 327.923 ms; mean 322.717 ms; median 324.729 ms; standard deviation 5.774 ms; complete.
- cpp_O2: min 24.729 ms; max 26.196 ms; mean 25.619 ms; median 25.710 ms; standard deviation 0.547 ms; complete.
- cpp_O3: min 24.650 ms; max 28.350 ms; mean 26.492 ms; median 26.461 ms; standard deviation 1.547 ms; complete.

### recursion

- interpreter: min 1433.073 ms; max 1589.313 ms; mean 1516.276 ms; median 1525.416 ms; standard deviation 67.910 ms; complete.
- native: min 33.713 ms; max 39.931 ms; mean 36.373 ms; median 35.627 ms; standard deviation 2.766 ms; complete.
- python: min 120.341 ms; max 141.982 ms; mean 131.243 ms; median 134.026 ms; standard deviation 9.656 ms; complete.
- cpp_O2: min 26.247 ms; max 32.823 ms; mean 30.410 ms; median 30.999 ms; standard deviation 2.494 ms; complete.
- cpp_O3: min 29.087 ms; max 33.595 ms; mean 31.146 ms; median 31.006 ms; standard deviation 1.624 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 1.77× to 15.13× across the checksum-verified comparable workloads; its largest ratio was on `tight_loop`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.076× to 0.761×. The weakest ratio was `tight_loop` (13.23× slower than Python); `structs` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
