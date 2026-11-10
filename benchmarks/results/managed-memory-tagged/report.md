# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T10:48:38.584292+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.18 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 150.433 ms | 15.673 ms | 75.292 ms | 4.804× | 0.500× | 9.598× |
| functions | 50000 | 2492250 | 229.620 ms | 17.317 ms | 79.579 ms | 4.595× | 0.347× | 13.260× |
| branching | 50000 | 3124925000 | 200.783 ms | 16.125 ms | 75.296 ms | 4.670× | 0.375× | 12.452× |
| combined | 50000 | 3034241 | 311.513 ms | 18.004 ms | 87.116 ms | 4.839× | 0.280× | 17.303× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 28.888 ms | success |
| functions | 30.021 ms | success |
| branching | 28.585 ms | success |
| combined | 30.174 ms | success |

## Run statistics

### arithmetic

- interpreter: min 147.482 ms; max 156.556 ms; mean 151.170 ms; median 150.433 ms; standard deviation 2.800 ms; complete.
- native: min 15.007 ms; max 16.359 ms; mean 15.656 ms; median 15.673 ms; standard deviation 0.464 ms; complete.
- python: min 73.282 ms; max 81.876 ms; mean 76.698 ms; median 75.292 ms; standard deviation 2.982 ms; complete.

### functions

- interpreter: min 224.796 ms; max 248.960 ms; mean 232.999 ms; median 229.620 ms; standard deviation 8.086 ms; complete.
- native: min 16.585 ms; max 20.071 ms; mean 17.578 ms; median 17.317 ms; standard deviation 1.017 ms; complete.
- python: min 76.381 ms; max 85.568 ms; mean 79.999 ms; median 79.579 ms; standard deviation 3.166 ms; complete.

### branching

- interpreter: min 197.143 ms; max 210.375 ms; mean 202.697 ms; median 200.783 ms; standard deviation 4.985 ms; complete.
- native: min 15.282 ms; max 18.052 ms; mean 16.335 ms; median 16.125 ms; standard deviation 0.796 ms; complete.
- python: min 73.585 ms; max 79.573 ms; mean 75.683 ms; median 75.296 ms; standard deviation 2.092 ms; complete.

### combined

- interpreter: min 304.549 ms; max 392.857 ms; mean 327.583 ms; median 311.513 ms; standard deviation 34.053 ms; complete.
- native: min 17.532 ms; max 18.458 ms; mean 17.975 ms; median 18.004 ms; standard deviation 0.320 ms; complete.
- python: min 85.477 ms; max 92.741 ms; mean 87.664 ms; median 87.116 ms; standard deviation 2.355 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.60× to 4.84×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.280× to 0.500×. The weakest ratio was `combined` (3.58× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
