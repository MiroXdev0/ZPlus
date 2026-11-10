# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T10:41:49.320506+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.71 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 148.033 ms | 15.224 ms | 68.440 ms | 4.496× | 0.462× | 9.724× |
| functions | 50000 | 2492250 | 224.592 ms | 16.221 ms | 74.437 ms | 4.589× | 0.331× | 13.846× |
| branching | 50000 | 3124925000 | 199.757 ms | 15.082 ms | 72.350 ms | 4.797× | 0.362× | 13.245× |
| combined | 50000 | 3034241 | 305.625 ms | 17.591 ms | 86.596 ms | 4.923× | 0.283× | 17.374× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 38.924 ms | success |
| functions | 27.269 ms | success |
| branching | 28.049 ms | success |
| combined | 28.269 ms | success |

## Run statistics

### arithmetic

- interpreter: min 143.713 ms; max 149.873 ms; mean 147.338 ms; median 148.033 ms; standard deviation 1.952 ms; complete.
- native: min 14.651 ms; max 15.896 ms; mean 15.241 ms; median 15.224 ms; standard deviation 0.408 ms; complete.
- python: min 67.595 ms; max 70.532 ms; mean 68.820 ms; median 68.440 ms; standard deviation 1.022 ms; complete.

### functions

- interpreter: min 223.214 ms; max 228.321 ms; mean 225.188 ms; median 224.592 ms; standard deviation 1.874 ms; complete.
- native: min 15.859 ms; max 16.544 ms; mean 16.192 ms; median 16.221 ms; standard deviation 0.208 ms; complete.
- python: min 73.294 ms; max 78.468 ms; mean 74.933 ms; median 74.437 ms; standard deviation 1.686 ms; complete.

### branching

- interpreter: min 195.798 ms; max 204.707 ms; mean 199.302 ms; median 199.757 ms; standard deviation 2.953 ms; complete.
- native: min 14.650 ms; max 15.654 ms; mean 15.176 ms; median 15.082 ms; standard deviation 0.327 ms; complete.
- python: min 70.848 ms; max 76.403 ms; mean 72.724 ms; median 72.350 ms; standard deviation 1.709 ms; complete.

### combined

- interpreter: min 298.704 ms; max 414.553 ms; mean 315.782 ms; median 305.625 ms; standard deviation 35.013 ms; complete.
- native: min 17.312 ms; max 18.276 ms; mean 17.678 ms; median 17.591 ms; standard deviation 0.288 ms; complete.
- python: min 82.846 ms; max 91.150 ms; mean 86.304 ms; median 86.596 ms; standard deviation 2.659 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.50× to 4.92×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.283× to 0.462×. The weakest ratio was `combined` (3.53× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
