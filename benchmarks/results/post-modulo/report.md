# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T14:10:33.936188+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 6.75 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 258.089 ms | 26.250 ms | 76.574 ms | 26.948 ms | 25.810 ms | 1.027× | 0.983× |
| mixed | 10000 | 155000 | 84.701 ms | 25.356 ms | 60.043 ms | 26.030 ms | 23.726 ms | 1.027× | 0.936× |
| functions | 50000 | 2492250 | 380.206 ms | 30.836 ms | 76.544 ms | 25.117 ms | 24.457 ms | 0.815× | 0.793× |
| branching | 50000 | 3124925000 | 325.858 ms | 26.732 ms | 76.481 ms | 28.983 ms | 24.229 ms | 1.084× | 0.906× |
| combined | 50000 | 3034241 | 524.767 ms | 32.418 ms | 96.789 ms | 25.750 ms | 26.761 ms | 0.794× | 0.825× |
| arrays | 10000 | 29994 | 78.890 ms | 26.869 ms | 58.587 ms | 23.879 ms | 23.605 ms | 0.889× | 0.879× |
| structs | 10000 | 39994 | 72.884 ms | 27.077 ms | 57.859 ms | 25.241 ms | 27.349 ms | 0.932× | 1.010× |
| memory | 10000 | 69995 | 96.684 ms | 29.571 ms | 60.386 ms | 25.330 ms | 26.663 ms | 0.857× | 0.902× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 50.715 ms | success |
| mixed | 49.655 ms | success |
| functions | 50.475 ms | success |
| branching | 53.052 ms | success |
| combined | 53.205 ms | success |
| arrays | 58.444 ms | success |
| structs | 53.307 ms | success |
| memory | 59.212 ms | success |

## Run statistics

### arithmetic

- interpreter: min 256.049 ms; max 264.592 ms; mean 258.863 ms; median 258.089 ms; standard deviation 3.329 ms; complete.
- native: min 25.756 ms; max 28.370 ms; mean 26.863 ms; median 26.250 ms; standard deviation 1.191 ms; complete.
- python: min 70.157 ms; max 77.194 ms; mean 74.565 ms; median 76.574 ms; standard deviation 3.308 ms; complete.
- cpp_O2: min 24.857 ms; max 28.388 ms; mean 26.828 ms; median 26.948 ms; standard deviation 1.448 ms; complete.
- cpp_O3: min 24.064 ms; max 27.051 ms; mean 25.598 ms; median 25.810 ms; standard deviation 1.152 ms; complete.

### mixed

- interpreter: min 82.040 ms; max 94.252 ms; mean 86.008 ms; median 84.701 ms; standard deviation 4.754 ms; complete.
- native: min 24.610 ms; max 28.064 ms; mean 25.913 ms; median 25.356 ms; standard deviation 1.401 ms; complete.
- python: min 59.199 ms; max 61.071 ms; mean 59.969 ms; median 60.043 ms; standard deviation 0.755 ms; complete.
- cpp_O2: min 23.433 ms; max 27.545 ms; mean 25.496 ms; median 26.030 ms; standard deviation 1.897 ms; complete.
- cpp_O3: min 23.063 ms; max 24.426 ms; mean 23.732 ms; median 23.726 ms; standard deviation 0.563 ms; complete.

### functions

- interpreter: min 365.775 ms; max 431.090 ms; mean 394.684 ms; median 380.206 ms; standard deviation 28.436 ms; complete.
- native: min 28.946 ms; max 32.793 ms; mean 30.829 ms; median 30.836 ms; standard deviation 1.749 ms; complete.
- python: min 73.738 ms; max 97.453 ms; mean 80.278 ms; median 76.544 ms; standard deviation 9.851 ms; complete.
- cpp_O2: min 23.551 ms; max 29.549 ms; mean 25.948 ms; median 25.117 ms; standard deviation 2.365 ms; complete.
- cpp_O3: min 24.138 ms; max 27.408 ms; mean 24.967 ms; median 24.457 ms; standard deviation 1.381 ms; complete.

### branching

- interpreter: min 322.344 ms; max 339.254 ms; mean 328.593 ms; median 325.858 ms; standard deviation 6.914 ms; complete.
- native: min 26.034 ms; max 29.543 ms; mean 27.192 ms; median 26.732 ms; standard deviation 1.445 ms; complete.
- python: min 74.129 ms; max 81.710 ms; mean 77.105 ms; median 76.481 ms; standard deviation 2.927 ms; complete.
- cpp_O2: min 25.350 ms; max 31.383 ms; mean 28.622 ms; median 28.983 ms; standard deviation 2.216 ms; complete.
- cpp_O3: min 23.918 ms; max 26.672 ms; mean 24.702 ms; median 24.229 ms; standard deviation 1.122 ms; complete.

### combined

- interpreter: min 521.186 ms; max 531.750 ms; mean 526.184 ms; median 524.767 ms; standard deviation 4.598 ms; complete.
- native: min 31.092 ms; max 36.003 ms; mean 33.310 ms; median 32.418 ms; standard deviation 2.381 ms; complete.
- python: min 86.927 ms; max 132.242 ms; mean 100.840 ms; median 96.789 ms; standard deviation 18.060 ms; complete.
- cpp_O2: min 24.600 ms; max 29.020 ms; mean 26.430 ms; median 25.750 ms; standard deviation 1.786 ms; complete.
- cpp_O3: min 25.748 ms; max 27.884 ms; mean 26.868 ms; median 26.761 ms; standard deviation 0.940 ms; complete.

### arrays

- interpreter: min 77.787 ms; max 80.938 ms; mean 79.261 ms; median 78.890 ms; standard deviation 1.329 ms; complete.
- native: min 25.892 ms; max 28.890 ms; mean 27.066 ms; median 26.869 ms; standard deviation 1.101 ms; complete.
- python: min 57.545 ms; max 60.990 ms; mean 59.019 ms; median 58.587 ms; standard deviation 1.500 ms; complete.
- cpp_O2: min 23.241 ms; max 28.607 ms; mean 25.236 ms; median 23.879 ms; standard deviation 2.296 ms; complete.
- cpp_O3: min 23.288 ms; max 24.454 ms; mean 23.687 ms; median 23.605 ms; standard deviation 0.452 ms; complete.

### structs

- interpreter: min 70.878 ms; max 78.288 ms; mean 73.860 ms; median 72.884 ms; standard deviation 2.788 ms; complete.
- native: min 25.187 ms; max 30.256 ms; mean 27.236 ms; median 27.077 ms; standard deviation 2.085 ms; complete.
- python: min 57.528 ms; max 58.714 ms; mean 58.068 ms; median 57.859 ms; standard deviation 0.542 ms; complete.
- cpp_O2: min 23.374 ms; max 29.987 ms; mean 26.444 ms; median 25.241 ms; standard deviation 3.028 ms; complete.
- cpp_O3: min 23.698 ms; max 28.828 ms; mean 26.806 ms; median 27.349 ms; standard deviation 1.928 ms; complete.

### memory

- interpreter: min 91.007 ms; max 102.656 ms; mean 95.996 ms; median 96.684 ms; standard deviation 4.690 ms; complete.
- native: min 29.367 ms; max 30.768 ms; mean 29.763 ms; median 29.571 ms; standard deviation 0.572 ms; complete.
- python: min 59.497 ms; max 60.836 ms; mean 60.299 ms; median 60.386 ms; standard deviation 0.489 ms; complete.
- cpp_O2: min 23.727 ms; max 28.082 ms; mean 25.942 ms; median 25.330 ms; standard deviation 1.941 ms; complete.
- cpp_O3: min 24.270 ms; max 28.111 ms; mean 26.112 ms; median 26.663 ms; standard deviation 1.679 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 2.04× to 2.99× across the checksum-verified comparable workloads; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.184× to 0.794×. The weakest ratio was `combined` (5.42× slower than Python); `structs` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
