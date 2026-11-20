# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T14:15:16.281267+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 6.42 GiB available at capture
- Operating system: Windows-11-10.0.26300-SP0
- Python: 3.13.15
- Z+ compiler: Z+ Compiler 1.3.0
- Compiler build configuration: Release
- Compiler executable: `C:\Users\miros\OneDrive\Desktop\Projects\Z+ compiler\bin\zp.exe`

## Methodology

Each timing is process wall-clock time measured with `time.perf_counter_ns()` and includes process startup, source loading, and (for `zp run`) interpreter/compiler startup. It is not pure function execution time. Native compilation is measured separately and excluded from execution timings. Every timed run is a fresh process.

- Warm-ups per available mode: 2
- Measured runs per mode: 5
- Timeout per process: 120.0 s
- Checksums are checked against a separately computed deterministic reference before any run is accepted.
- Python and Z+ use the same loop counts and arithmetic expressions for each workload. Z+ prints its expected checksum only if its computed numeric checksum matches; otherwise it emits `CHECKSUM_ERROR`.
- Native compile failures are recorded as unsupported with the original diagnostic; there is no interpreter fallback.

Workloads: arithmetic/modulo accumulation; mixed fractional/integer arithmetic; function calls; conditional branches; combined scalar work; indexed array updates; mutable struct-field updates; and repeated managed-array allocation.

## Results

| Workload | Iterations | Checksum | Z+ Interpreter | Z+ Native | Python | C++ O2 | C++ O3 | Native vs C++ O2 | Native vs C++ O3 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| functions | 50000 | 2492250 | 395.531 ms | 35.227 ms | 87.456 ms | 25.276 ms | 27.648 ms | 0.718× | 0.785× |
| combined | 50000 | 3034241 | 543.056 ms | 30.472 ms | 92.435 ms | 25.754 ms | 25.418 ms | 0.845× | 0.834× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| functions | 52.360 ms | success |
| combined | 55.029 ms | success |

## Run statistics

### functions

- interpreter: min 379.686 ms; max 529.658 ms; mean 418.079 ms; median 395.531 ms; standard deviation 63.092 ms; complete.
- native: min 30.430 ms; max 39.325 ms; mean 34.900 ms; median 35.227 ms; standard deviation 3.181 ms; complete.
- python: min 74.959 ms; max 103.297 ms; mean 86.241 ms; median 87.456 ms; standard deviation 11.288 ms; complete.
- cpp_O2: min 24.834 ms; max 26.706 ms; mean 25.692 ms; median 25.276 ms; standard deviation 0.864 ms; complete.
- cpp_O3: min 24.270 ms; max 28.284 ms; mean 27.037 ms; median 27.648 ms; standard deviation 1.640 ms; complete.

### combined

- interpreter: min 527.105 ms; max 582.501 ms; mean 549.353 ms; median 543.056 ms; standard deviation 21.560 ms; complete.
- native: min 30.369 ms; max 31.469 ms; mean 30.738 ms; median 30.472 ms; standard deviation 0.469 ms; complete.
- python: min 87.578 ms; max 93.285 ms; mean 91.096 ms; median 92.435 ms; standard deviation 2.449 ms; complete.
- cpp_O2: min 25.203 ms; max 28.250 ms; mean 26.565 ms; median 25.754 ms; standard deviation 1.547 ms; complete.
- cpp_O3: min 25.121 ms; max 27.805 ms; mean 25.936 ms; median 25.418 ms; standard deviation 1.112 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 2.48× to 3.03× across the checksum-verified comparable workloads; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.170× to 0.221×. The weakest ratio was `combined` (5.87× slower than Python); `functions` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
