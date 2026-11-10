# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T10:02:32.912286+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.84 GiB available at capture
- Operating system: Windows-11-10.0.26300-SP0
- Python: 3.13.15
- Z+ compiler: Z+ Compiler 0.5.0
- Compiler build configuration: Release
- Compiler executable: `C:\Users\miros\OneDrive\Desktop\Projects\Z+ compiler\bin\zp.exe`

## Methodology

Each timing is process wall-clock time measured with `time.perf_counter_ns()` and includes process startup, source loading, and (for `zp run`) interpreter/compiler startup. It is not pure function execution time. Native compilation is measured separately and excluded from execution timings. Every timed run is a fresh process.

- Warm-ups per available mode: 3
- Measured runs per mode: 10
- Timeout per process: 120.0 s
- Checksums are checked against a separately computed deterministic reference before any run is accepted.
- Python and Z+ use the same loop counts and arithmetic expressions for each workload. Z+ prints its expected checksum only if its computed numeric checksum matches; otherwise it emits `CHECKSUM_ERROR`.
- The native backend supports only its documented integer-oriented subset. A native compile failure is recorded as unsupported with the original diagnostic; there is no interpreter fallback.

Workloads: arithmetic/modulo accumulation; function calls plus arithmetic; conditional branch accumulation; combined function/branch/arithmetic accumulation.

## Results

| Workload | Iterations | Checksum | Z+ Interpreter median | Z+ Native median | Python median | Native vs Python | Interpreter vs Python | Native vs Interpreter |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| arithmetic | 50000 | 2399670 | 132.693 ms | 15.948 ms | 74.833 ms | 4.692× | 0.564× | 8.320× |
| functions | 50000 | 2492250 | 502.916 ms | 16.335 ms | 76.616 ms | 4.690× | 0.152× | 30.787× |
| branching | 50000 | 3124925000 | 179.402 ms | 16.074 ms | 77.674 ms | 4.832× | 0.433× | 11.161× |
| combined | 50000 | 3034241 | 573.251 ms | 18.168 ms | 88.652 ms | 4.879× | 0.155× | 31.552× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 35.625 ms | success |
| functions | 28.756 ms | success |
| branching | 28.484 ms | success |
| combined | 58.801 ms | success |

## Run statistics

### arithmetic

- interpreter: min 130.575 ms; max 160.083 ms; mean 137.315 ms; median 132.693 ms; standard deviation 9.610 ms; complete.
- native: min 15.603 ms; max 16.722 ms; mean 16.137 ms; median 15.948 ms; standard deviation 0.411 ms; complete.
- python: min 72.365 ms; max 77.273 ms; mean 74.865 ms; median 74.833 ms; standard deviation 1.637 ms; complete.

### functions

- interpreter: min 484.601 ms; max 671.048 ms; mean 521.855 ms; median 502.916 ms; standard deviation 55.150 ms; complete.
- native: min 15.955 ms; max 18.809 ms; mean 16.675 ms; median 16.335 ms; standard deviation 0.867 ms; complete.
- python: min 73.708 ms; max 81.395 ms; mean 76.759 ms; median 76.616 ms; standard deviation 2.608 ms; complete.

### branching

- interpreter: min 176.305 ms; max 185.540 ms; mean 179.725 ms; median 179.402 ms; standard deviation 2.638 ms; complete.
- native: min 15.654 ms; max 16.489 ms; mean 16.075 ms; median 16.074 ms; standard deviation 0.231 ms; complete.
- python: min 70.314 ms; max 93.995 ms; mean 78.329 ms; median 77.674 ms; standard deviation 7.337 ms; complete.

### combined

- interpreter: min 567.570 ms; max 592.409 ms; mean 576.218 ms; median 573.251 ms; standard deviation 7.991 ms; complete.
- native: min 17.675 ms; max 18.957 ms; mean 18.217 ms; median 18.168 ms; standard deviation 0.402 ms; complete.
- python: min 85.748 ms; max 131.480 ms; mean 96.075 ms; median 88.652 ms; standard deviation 14.773 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.69× to 4.88×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.152× to 0.564×. The weakest ratio was `functions` (6.56× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
