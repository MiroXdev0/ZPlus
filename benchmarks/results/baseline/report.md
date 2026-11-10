# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T09:39:59.462670+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.44 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 190.972 ms | 15.956 ms | 71.300 ms | 4.469× | 0.373× | 11.969× |
| functions | 50000 | 2492250 | 575.747 ms | 16.835 ms | 75.223 ms | 4.468× | 0.131× | 34.199× |
| branching | 50000 | 3124925000 | 231.711 ms | 15.656 ms | 72.892 ms | 4.656× | 0.315× | 14.800× |
| combined | 50000 | 3034241 | 709.256 ms | 18.147 ms | 89.702 ms | 4.943× | 0.126× | 39.084× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 41.450 ms | success |
| functions | 28.884 ms | success |
| branching | 27.843 ms | success |
| combined | 28.828 ms | success |

## Run statistics

### arithmetic

- interpreter: min 181.172 ms; max 215.637 ms; mean 193.852 ms; median 190.972 ms; standard deviation 12.692 ms; complete.
- native: min 15.566 ms; max 16.448 ms; mean 15.966 ms; median 15.956 ms; standard deviation 0.313 ms; complete.
- python: min 69.567 ms; max 73.832 ms; mean 71.570 ms; median 71.300 ms; standard deviation 1.376 ms; complete.

### functions

- interpreter: min 567.382 ms; max 720.498 ms; mean 599.040 ms; median 575.747 ms; standard deviation 54.267 ms; complete.
- native: min 16.086 ms; max 17.285 ms; mean 16.844 ms; median 16.835 ms; standard deviation 0.381 ms; complete.
- python: min 73.280 ms; max 78.359 ms; mean 75.446 ms; median 75.223 ms; standard deviation 1.763 ms; complete.

### branching

- interpreter: min 227.611 ms; max 246.018 ms; mean 233.514 ms; median 231.711 ms; standard deviation 5.802 ms; complete.
- native: min 15.480 ms; max 16.433 ms; mean 15.815 ms; median 15.656 ms; standard deviation 0.330 ms; complete.
- python: min 71.382 ms; max 74.821 ms; mean 72.937 ms; median 72.892 ms; standard deviation 1.075 ms; complete.

### combined

- interpreter: min 705.021 ms; max 712.133 ms; mean 709.173 ms; median 709.256 ms; standard deviation 2.367 ms; complete.
- native: min 17.697 ms; max 18.759 ms; mean 18.204 ms; median 18.147 ms; standard deviation 0.371 ms; complete.
- python: min 86.525 ms; max 95.299 ms; mean 89.781 ms; median 89.702 ms; standard deviation 2.621 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.47× to 4.94×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.126× to 0.373×. The weakest ratio was `combined` (7.91× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
