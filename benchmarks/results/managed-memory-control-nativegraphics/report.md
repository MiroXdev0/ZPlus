# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T10:38:53.280517+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.25 GiB available at capture
- Operating system: Windows-11-10.0.26300-SP0
- Python: 3.13.15
- Z+ compiler: Z+ Compiler 0.5.0
- Compiler build configuration: Release
- Compiler executable: `C:\Users\miros\OneDrive\Desktop\Projects\Z+ compiler\build-native-graphics\Release\zp.exe`

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
| arithmetic | 50000 | 2399670 | 169.248 ms | 15.683 ms | 69.590 ms | 4.437× | 0.411× | 10.792× |
| functions | 50000 | 2492250 | 549.160 ms | 16.066 ms | 71.796 ms | 4.469× | 0.131× | 34.181× |
| branching | 50000 | 3124925000 | 216.097 ms | 15.586 ms | 69.094 ms | 4.433× | 0.320× | 13.865× |
| combined | 50000 | 3034241 | 668.616 ms | 17.633 ms | 85.710 ms | 4.861× | 0.128× | 37.918× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 29.036 ms | success |
| functions | 27.379 ms | success |
| branching | 27.931 ms | success |
| combined | 30.099 ms | success |

## Run statistics

### arithmetic

- interpreter: min 163.527 ms; max 179.990 ms; mean 169.105 ms; median 169.248 ms; standard deviation 4.432 ms; complete.
- native: min 15.005 ms; max 16.189 ms; mean 15.682 ms; median 15.683 ms; standard deviation 0.361 ms; complete.
- python: min 67.855 ms; max 74.726 ms; mean 70.206 ms; median 69.590 ms; standard deviation 2.106 ms; complete.

### functions

- interpreter: min 544.100 ms; max 593.709 ms; mean 557.021 ms; median 549.160 ms; standard deviation 19.538 ms; complete.
- native: min 15.681 ms; max 16.768 ms; mean 16.152 ms; median 16.066 ms; standard deviation 0.348 ms; complete.
- python: min 70.703 ms; max 79.664 ms; mean 72.501 ms; median 71.796 ms; standard deviation 2.591 ms; complete.

### branching

- interpreter: min 210.336 ms; max 220.663 ms; mean 215.853 ms; median 216.097 ms; standard deviation 3.733 ms; complete.
- native: min 15.121 ms; max 15.895 ms; mean 15.603 ms; median 15.586 ms; standard deviation 0.255 ms; complete.
- python: min 67.583 ms; max 73.900 ms; mean 69.725 ms; median 69.094 ms; standard deviation 1.976 ms; complete.

### combined

- interpreter: min 665.254 ms; max 671.707 ms; mean 668.634 ms; median 668.616 ms; standard deviation 2.184 ms; complete.
- native: min 17.170 ms; max 18.579 ms; mean 17.770 ms; median 17.633 ms; standard deviation 0.476 ms; complete.
- python: min 83.212 ms; max 88.742 ms; mean 85.709 ms; median 85.710 ms; standard deviation 1.771 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.43× to 4.86×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.128× to 0.411×. The weakest ratio was `combined` (7.80× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
