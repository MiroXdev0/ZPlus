# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T10:34:13.520126+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.27 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 133.417 ms | 15.478 ms | 71.107 ms | 4.594× | 0.533× | 8.620× |
| functions | 50000 | 2492250 | 202.315 ms | 16.310 ms | 71.344 ms | 4.374× | 0.353× | 12.404× |
| branching | 50000 | 3124925000 | 180.285 ms | 15.412 ms | 70.819 ms | 4.595× | 0.393× | 11.697× |
| combined | 50000 | 3034241 | 271.018 ms | 17.321 ms | 86.711 ms | 5.006× | 0.320× | 15.646× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 28.841 ms | success |
| functions | 29.788 ms | success |
| branching | 27.987 ms | success |
| combined | 27.706 ms | success |

## Run statistics

### arithmetic

- interpreter: min 131.704 ms; max 136.953 ms; mean 134.072 ms; median 133.417 ms; standard deviation 1.744 ms; complete.
- native: min 15.176 ms; max 15.795 ms; mean 15.510 ms; median 15.478 ms; standard deviation 0.172 ms; complete.
- python: min 68.604 ms; max 75.411 ms; mean 71.611 ms; median 71.107 ms; standard deviation 2.101 ms; complete.

### functions

- interpreter: min 196.902 ms; max 236.609 ms; mean 205.594 ms; median 202.315 ms; standard deviation 11.519 ms; complete.
- native: min 15.795 ms; max 17.077 ms; mean 16.332 ms; median 16.310 ms; standard deviation 0.372 ms; complete.
- python: min 70.167 ms; max 74.567 ms; mean 71.749 ms; median 71.344 ms; standard deviation 1.529 ms; complete.

### branching

- interpreter: min 173.846 ms; max 192.320 ms; mean 181.031 ms; median 180.285 ms; standard deviation 6.130 ms; complete.
- native: min 14.876 ms; max 15.966 ms; mean 15.429 ms; median 15.412 ms; standard deviation 0.388 ms; complete.
- python: min 67.313 ms; max 72.769 ms; mean 70.528 ms; median 70.819 ms; standard deviation 1.519 ms; complete.

### combined

- interpreter: min 268.724 ms; max 280.358 ms; mean 273.133 ms; median 271.018 ms; standard deviation 4.422 ms; complete.
- native: min 16.960 ms; max 17.571 ms; mean 17.308 ms; median 17.321 ms; standard deviation 0.221 ms; complete.
- python: min 82.924 ms; max 90.509 ms; mean 86.692 ms; median 86.711 ms; standard deviation 2.669 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.37× to 5.01×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.320× to 0.533×. The weakest ratio was `combined` (3.13× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
