# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T14:18:46.823205+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 6.37 GiB available at capture
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
| tight_loop | 1000000 | 1499999500000 | 3682.700 ms | 22.650 ms | 320.588 ms | 26.836 ms | 26.860 ms | 1.185× | 1.186× |
| recursion | 50000 | 1191560 | 1413.070 ms | 33.321 ms | 108.995 ms | 26.966 ms | 27.823 ms | 0.809× | 0.835× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| tight_loop | 42.859 ms | success |
| recursion | 56.667 ms | success |

## Run statistics

### tight_loop

- interpreter: min 3674.588 ms; max 3935.661 ms; mean 3735.031 ms; median 3682.700 ms; standard deviation 112.774 ms; complete.
- native: min 21.660 ms; max 24.996 ms; mean 22.777 ms; median 22.650 ms; standard deviation 1.358 ms; complete.
- python: min 302.178 ms; max 341.812 ms; mean 317.996 ms; median 320.588 ms; standard deviation 16.451 ms; complete.
- cpp_O2: min 26.060 ms; max 29.493 ms; mean 27.503 ms; median 26.836 ms; standard deviation 1.599 ms; complete.
- cpp_O3: min 26.166 ms; max 29.750 ms; mean 27.543 ms; median 26.860 ms; standard deviation 1.422 ms; complete.

### recursion

- interpreter: min 1372.998 ms; max 1501.349 ms; mean 1429.078 ms; median 1413.070 ms; standard deviation 48.524 ms; complete.
- native: min 30.799 ms; max 38.731 ms; mean 33.738 ms; median 33.321 ms; standard deviation 3.255 ms; complete.
- python: min 99.760 ms; max 116.365 ms; mean 107.422 ms; median 108.995 ms; standard deviation 6.684 ms; complete.
- cpp_O2: min 24.832 ms; max 36.772 ms; mean 28.475 ms; median 26.966 ms; standard deviation 4.749 ms; complete.
- cpp_O3: min 25.232 ms; max 30.617 ms; mean 27.822 ms; median 27.823 ms; standard deviation 1.966 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 3.27× to 14.15× across the checksum-verified comparable workloads; its largest ratio was on `tight_loop`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.077× to 0.087×. The weakest ratio was `recursion` (12.96× slower than Python); `tight_loop` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
