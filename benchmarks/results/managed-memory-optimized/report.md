# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T10:37:00.948951+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.26 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 130.466 ms | 14.971 ms | 68.440 ms | 4.571× | 0.525× | 8.714× |
| functions | 50000 | 2492250 | 195.926 ms | 16.091 ms | 77.834 ms | 4.837× | 0.397× | 12.176× |
| branching | 50000 | 3124925000 | 182.292 ms | 14.956 ms | 69.171 ms | 4.625× | 0.379× | 12.189× |
| combined | 50000 | 3034241 | 269.371 ms | 17.717 ms | 85.239 ms | 4.811× | 0.316× | 15.204× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 27.556 ms | success |
| functions | 27.424 ms | success |
| branching | 28.086 ms | success |
| combined | 27.894 ms | success |

## Run statistics

### arithmetic

- interpreter: min 128.811 ms; max 132.339 ms; mean 130.527 ms; median 130.466 ms; standard deviation 1.168 ms; complete.
- native: min 14.306 ms; max 17.468 ms; mean 15.273 ms; median 14.971 ms; standard deviation 1.005 ms; complete.
- python: min 66.754 ms; max 70.761 ms; mean 68.898 ms; median 68.440 ms; standard deviation 1.373 ms; complete.

### functions

- interpreter: min 193.550 ms; max 199.625 ms; mean 196.097 ms; median 195.926 ms; standard deviation 2.097 ms; complete.
- native: min 15.162 ms; max 17.169 ms; mean 16.078 ms; median 16.091 ms; standard deviation 0.516 ms; complete.
- python: min 71.719 ms; max 81.806 ms; mean 77.346 ms; median 77.834 ms; standard deviation 2.825 ms; complete.

### branching

- interpreter: min 176.539 ms; max 185.898 ms; mean 180.984 ms; median 182.292 ms; standard deviation 3.390 ms; complete.
- native: min 14.575 ms; max 15.248 ms; mean 14.949 ms; median 14.956 ms; standard deviation 0.213 ms; complete.
- python: min 68.156 ms; max 71.847 ms; mean 69.369 ms; median 69.171 ms; standard deviation 1.109 ms; complete.

### combined

- interpreter: min 265.069 ms; max 278.519 ms; mean 271.319 ms; median 269.371 ms; standard deviation 4.850 ms; complete.
- native: min 17.174 ms; max 18.592 ms; mean 17.719 ms; median 17.717 ms; standard deviation 0.466 ms; complete.
- python: min 83.301 ms; max 90.100 ms; mean 85.979 ms; median 85.239 ms; standard deviation 2.261 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.57× to 4.84×; its largest ratio was on `functions`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.316× to 0.525×. The weakest ratio was `combined` (3.16× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
