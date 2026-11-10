# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T09:48:38.127607+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.45 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 133.909 ms | 18.685 ms | 75.271 ms | 4.028× | 0.562× | 7.167× |
| functions | 50000 | 2492250 | 488.417 ms | 19.147 ms | 76.605 ms | 4.001× | 0.157× | 25.509× |
| branching | 50000 | 3124925000 | 178.803 ms | 18.606 ms | 75.548 ms | 4.061× | 0.423× | 9.610× |
| combined | 50000 | 3034241 | 570.996 ms | 20.736 ms | 91.239 ms | 4.400× | 0.160× | 27.537× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 28.315 ms | success |
| functions | 29.161 ms | success |
| branching | 28.755 ms | success |
| combined | 29.009 ms | success |

## Run statistics

### arithmetic

- interpreter: min 129.034 ms; max 183.718 ms; mean 139.637 ms; median 133.909 ms; standard deviation 16.247 ms; complete.
- native: min 18.195 ms; max 19.248 ms; mean 18.736 ms; median 18.685 ms; standard deviation 0.342 ms; complete.
- python: min 73.860 ms; max 79.611 ms; mean 75.836 ms; median 75.271 ms; standard deviation 1.859 ms; complete.

### functions

- interpreter: min 484.968 ms; max 498.675 ms; mean 489.419 ms; median 488.417 ms; standard deviation 4.179 ms; complete.
- native: min 18.517 ms; max 19.796 ms; mean 19.163 ms; median 19.147 ms; standard deviation 0.374 ms; complete.
- python: min 75.145 ms; max 82.120 ms; mean 77.134 ms; median 76.605 ms; standard deviation 2.084 ms; complete.

### branching

- interpreter: min 176.605 ms; max 205.384 ms; mean 181.909 ms; median 178.803 ms; standard deviation 8.665 ms; complete.
- native: min 18.062 ms; max 21.883 ms; mean 18.984 ms; median 18.606 ms; standard deviation 1.067 ms; complete.
- python: min 72.691 ms; max 84.233 ms; mean 76.060 ms; median 75.548 ms; standard deviation 3.146 ms; complete.

### combined

- interpreter: min 565.359 ms; max 575.191 ms; mean 570.977 ms; median 570.996 ms; standard deviation 3.792 ms; complete.
- native: min 20.300 ms; max 21.456 ms; mean 20.762 ms; median 20.736 ms; standard deviation 0.345 ms; complete.
- python: min 88.353 ms; max 94.933 ms; mean 91.274 ms; median 91.239 ms; standard deviation 2.267 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.00× to 4.40×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.157× to 0.562×. The weakest ratio was `functions` (6.38× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
