# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T10:07:43.861949+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.39 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 115.639 ms | 15.534 ms | 72.070 ms | 4.640× | 0.623× | 7.444× |
| functions | 50000 | 2492250 | 176.194 ms | 16.985 ms | 77.213 ms | 4.546× | 0.438× | 10.374× |
| branching | 50000 | 3124925000 | 154.975 ms | 15.985 ms | 73.030 ms | 4.569× | 0.471× | 9.695× |
| combined | 50000 | 3034241 | 241.363 ms | 17.983 ms | 87.107 ms | 4.844× | 0.361× | 13.422× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 39.310 ms | success |
| functions | 46.593 ms | success |
| branching | 52.312 ms | success |
| combined | 30.343 ms | success |

## Run statistics

### arithmetic

- interpreter: min 112.492 ms; max 140.564 ms; mean 118.231 ms; median 115.639 ms; standard deviation 8.248 ms; complete.
- native: min 14.936 ms; max 16.636 ms; mean 15.607 ms; median 15.534 ms; standard deviation 0.475 ms; complete.
- python: min 70.635 ms; max 76.251 ms; mean 72.399 ms; median 72.070 ms; standard deviation 1.727 ms; complete.

### functions

- interpreter: min 170.288 ms; max 186.939 ms; mean 177.051 ms; median 176.194 ms; standard deviation 5.718 ms; complete.
- native: min 16.283 ms; max 17.303 ms; mean 16.880 ms; median 16.985 ms; standard deviation 0.328 ms; complete.
- python: min 73.445 ms; max 115.467 ms; mean 82.755 ms; median 77.213 ms; standard deviation 13.264 ms; complete.

### branching

- interpreter: min 151.249 ms; max 167.581 ms; mean 156.844 ms; median 154.975 ms; standard deviation 5.119 ms; complete.
- native: min 15.226 ms; max 16.466 ms; mean 15.906 ms; median 15.985 ms; standard deviation 0.488 ms; complete.
- python: min 70.365 ms; max 78.139 ms; mean 73.533 ms; median 73.030 ms; standard deviation 2.330 ms; complete.

### combined

- interpreter: min 235.454 ms; max 248.107 ms; mean 241.004 ms; median 241.363 ms; standard deviation 4.118 ms; complete.
- native: min 17.209 ms; max 18.739 ms; mean 18.060 ms; median 17.983 ms; standard deviation 0.471 ms; complete.
- python: min 85.379 ms; max 91.035 ms; mean 87.346 ms; median 87.107 ms; standard deviation 1.628 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.55× to 4.84×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.361× to 0.623×. The weakest ratio was `combined` (2.77× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
