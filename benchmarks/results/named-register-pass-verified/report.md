# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T14:34:46.335688+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.21 GiB available at capture
- Operating system: Windows-11-10.0.26300-SP0
- Python: 3.13.15
- Z+ compiler: Z+ Compiler 1.3.0
- Compiler build configuration: Release
- Compiler executable: `C:\Users\miros\OneDrive\Desktop\Projects\Z+ compiler\bin\zp.exe`

## Methodology

Each timing is process wall-clock time measured with `time.perf_counter_ns()` and includes process startup, source loading, and (for `zp run`) interpreter/compiler startup. It is not pure function execution time. Native compilation is measured separately and excluded from execution timings. Every timed run is a fresh process.

- Warm-ups per available mode: 3
- Measured runs per mode: 7
- Timeout per process: 120.0 s
- Checksums are checked against a separately computed deterministic reference before any run is accepted.
- Python and Z+ use the same loop counts and arithmetic expressions for each workload. Z+ prints its expected checksum only if its computed numeric checksum matches; otherwise it emits `CHECKSUM_ERROR`.
- Native compile failures are recorded as unsupported with the original diagnostic; there is no interpreter fallback.

Workloads: arithmetic/modulo accumulation; mixed fractional/integer arithmetic; function calls; conditional branches; combined scalar work; indexed array updates; mutable struct-field updates; and repeated managed-array allocation.

## Results

| Workload | Iterations | Checksum | Z+ Interpreter | Z+ Native | Python | C++ O2 | C++ O3 | Native vs C++ O2 | Native vs C++ O3 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| tight_loop | 1000000 | 1499999500000 | 3801.709 ms | 21.236 ms | 311.293 ms | 27.029 ms | 27.134 ms | 1.273× | 1.278× |
| arithmetic | 50000 | 2399670 | 288.438 ms | 26.375 ms | 74.753 ms | 23.688 ms | 23.872 ms | 0.898× | 0.905× |
| mixed | 10000 | 155000 | 92.679 ms | 30.408 ms | 64.087 ms | 25.081 ms | 25.660 ms | 0.825× | 0.844× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| tight_loop | 42.301 ms | success |
| arithmetic | 52.957 ms | success |
| mixed | 50.906 ms | success |

## Run statistics

### tight_loop

- interpreter: min 3754.168 ms; max 4096.526 ms; mean 3881.554 ms; median 3801.709 ms; standard deviation 138.685 ms; complete.
- native: min 20.814 ms; max 22.180 ms; mean 21.412 ms; median 21.236 ms; standard deviation 0.494 ms; complete.
- python: min 287.140 ms; max 334.955 ms; mean 308.322 ms; median 311.293 ms; standard deviation 15.141 ms; complete.
- cpp_O2: min 24.868 ms; max 30.350 ms; mean 27.293 ms; median 27.029 ms; standard deviation 2.043 ms; complete.
- cpp_O3: min 25.250 ms; max 28.843 ms; mean 26.914 ms; median 27.134 ms; standard deviation 1.485 ms; complete.

### arithmetic

- interpreter: min 272.529 ms; max 358.848 ms; mean 295.483 ms; median 288.438 ms; standard deviation 30.977 ms; complete.
- native: min 25.429 ms; max 28.419 ms; mean 26.603 ms; median 26.375 ms; standard deviation 0.939 ms; complete.
- python: min 72.055 ms; max 90.060 ms; mean 77.229 ms; median 74.753 ms; standard deviation 6.343 ms; complete.
- cpp_O2: min 23.044 ms; max 26.852 ms; mean 24.523 ms; median 23.688 ms; standard deviation 1.474 ms; complete.
- cpp_O3: min 23.592 ms; max 26.570 ms; mean 24.467 ms; median 23.872 ms; standard deviation 1.062 ms; complete.

### mixed

- interpreter: min 87.800 ms; max 108.929 ms; mean 94.672 ms; median 92.679 ms; standard deviation 7.125 ms; complete.
- native: min 27.900 ms; max 33.143 ms; mean 30.549 ms; median 30.408 ms; standard deviation 1.907 ms; complete.
- python: min 60.960 ms; max 66.653 ms; mean 63.962 ms; median 64.087 ms; standard deviation 2.083 ms; complete.
- cpp_O2: min 22.980 ms; max 27.101 ms; mean 25.275 ms; median 25.081 ms; standard deviation 1.461 ms; complete.
- cpp_O3: min 23.797 ms; max 27.966 ms; mean 25.706 ms; median 25.660 ms; standard deviation 1.756 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 2.11× to 14.66× across the checksum-verified comparable workloads; its largest ratio was on `tight_loop`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.082× to 0.691×. The weakest ratio was `tight_loop` (12.21× slower than Python); `mixed` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
