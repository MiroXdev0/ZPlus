# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T14:17:12.666692+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 6.82 GiB available at capture
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
| functions | 50000 | 2492250 | 371.063 ms | 27.477 ms | 75.699 ms | 27.424 ms | 29.105 ms | 0.998× | 1.059× |
| combined | 50000 | 3034241 | 554.308 ms | 30.843 ms | 89.829 ms | 25.581 ms | 24.823 ms | 0.829× | 0.805× |
| arithmetic | 50000 | 2399670 | 282.452 ms | 26.535 ms | 74.046 ms | 24.532 ms | 24.582 ms | 0.925× | 0.926× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| functions | 51.710 ms | success |
| combined | 54.250 ms | success |
| arithmetic | 52.346 ms | success |

## Run statistics

### functions

- interpreter: min 360.345 ms; max 387.182 ms; mean 371.667 ms; median 371.063 ms; standard deviation 9.761 ms; complete.
- native: min 27.054 ms; max 28.144 ms; mean 27.605 ms; median 27.477 ms; standard deviation 0.415 ms; complete.
- python: min 74.291 ms; max 78.444 ms; mean 75.882 ms; median 75.699 ms; standard deviation 1.319 ms; complete.
- cpp_O2: min 24.554 ms; max 29.898 ms; mean 27.328 ms; median 27.424 ms; standard deviation 2.134 ms; complete.
- cpp_O3: min 27.603 ms; max 30.343 ms; mean 28.974 ms; median 29.105 ms; standard deviation 1.144 ms; complete.

### combined

- interpreter: min 523.019 ms; max 610.348 ms; mean 556.622 ms; median 554.308 ms; standard deviation 33.353 ms; complete.
- native: min 30.094 ms; max 31.530 ms; mean 30.871 ms; median 30.843 ms; standard deviation 0.539 ms; complete.
- python: min 86.489 ms; max 93.314 ms; mean 89.401 ms; median 89.829 ms; standard deviation 2.355 ms; complete.
- cpp_O2: min 25.025 ms; max 28.395 ms; mean 26.354 ms; median 25.581 ms; standard deviation 1.345 ms; complete.
- cpp_O3: min 24.715 ms; max 25.342 ms; mean 24.929 ms; median 24.823 ms; standard deviation 0.236 ms; complete.

### arithmetic

- interpreter: min 265.231 ms; max 339.309 ms; mean 290.248 ms; median 282.452 ms; standard deviation 28.746 ms; complete.
- native: min 25.653 ms; max 28.958 ms; mean 26.789 ms; median 26.535 ms; standard deviation 1.146 ms; complete.
- python: min 72.275 ms; max 87.292 ms; mean 75.584 ms; median 74.046 ms; standard deviation 5.236 ms; complete.
- cpp_O2: min 23.740 ms; max 27.991 ms; mean 24.934 ms; median 24.532 ms; standard deviation 1.564 ms; complete.
- cpp_O3: min 23.994 ms; max 27.776 ms; mean 25.092 ms; median 24.582 ms; standard deviation 1.270 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 2.76× to 2.91× across the checksum-verified comparable workloads; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.162× to 0.262×. The weakest ratio was `combined` (6.17× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
