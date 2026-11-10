# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T09:47:53.178997+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.16 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 132.687 ms | 18.639 ms | 74.756 ms | 4.011× | 0.563× | 7.119× |
| functions | 50000 | 2492250 | 495.692 ms | 19.489 ms | 79.149 ms | 4.061× | 0.160× | 25.435× |
| branching | 50000 | 3124925000 | 178.568 ms | 18.739 ms | 74.211 ms | 3.960× | 0.416× | 9.529× |
| combined | 50000 | 3034241 | 570.382 ms | 20.858 ms | 90.011 ms | 4.315× | 0.158× | 27.346× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 32.368 ms | success |
| functions | 30.952 ms | success |
| branching | 37.494 ms | success |
| combined | 31.283 ms | success |

## Run statistics

### arithmetic

- interpreter: min 131.632 ms; max 143.249 ms; mean 134.062 ms; median 132.687 ms; standard deviation 3.522 ms; complete.
- native: min 17.531 ms; max 20.241 ms; mean 18.791 ms; median 18.639 ms; standard deviation 0.790 ms; complete.
- python: min 72.718 ms; max 75.100 ms; mean 74.343 ms; median 74.756 ms; standard deviation 0.767 ms; complete.

### functions

- interpreter: min 486.989 ms; max 594.004 ms; mean 512.969 ms; median 495.692 ms; standard deviation 40.882 ms; complete.
- native: min 19.251 ms; max 24.138 ms; mean 20.095 ms; median 19.489 ms; standard deviation 1.507 ms; complete.
- python: min 78.013 ms; max 82.309 ms; mean 79.362 ms; median 79.149 ms; standard deviation 1.362 ms; complete.

### branching

- interpreter: min 174.637 ms; max 219.278 ms; mean 183.880 ms; median 178.568 ms; standard deviation 13.507 ms; complete.
- native: min 18.318 ms; max 19.163 ms; mean 18.773 ms; median 18.739 ms; standard deviation 0.244 ms; complete.
- python: min 72.072 ms; max 76.007 ms; mean 74.330 ms; median 74.211 ms; standard deviation 1.246 ms; complete.

### combined

- interpreter: min 565.184 ms; max 631.288 ms; mean 575.769 ms; median 570.382 ms; standard deviation 19.590 ms; complete.
- native: min 20.555 ms; max 22.015 ms; mean 20.933 ms; median 20.858 ms; standard deviation 0.426 ms; complete.
- python: min 87.465 ms; max 94.324 ms; mean 90.403 ms; median 90.011 ms; standard deviation 2.350 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 3.96× to 4.32×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.158× to 0.563×. The weakest ratio was `combined` (6.34× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
