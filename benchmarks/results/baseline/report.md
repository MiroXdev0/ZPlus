# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T13:59:25.356035+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 6.59 GiB available at capture
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

| Workload | Iterations | Checksum | Z+ Interpreter median | Z+ Native median | Python median | Native vs Python | Interpreter vs Python | Native vs Interpreter |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| arithmetic | 50000 | 2399670 | 306.223 ms | 29.637 ms | 89.893 ms | 3.033× | 0.294× | 10.332× |
| mixed | 10000 | 155000 | 107.217 ms | 38.400 ms | 79.572 ms | 2.072× | 0.742× | 2.792× |
| functions | 50000 | 2492250 | 525.779 ms | 29.385 ms | 89.996 ms | 3.063× | 0.171× | 17.893× |
| branching | 50000 | 3124925000 | 353.554 ms | 28.478 ms | 84.044 ms | 2.951× | 0.238× | 12.415× |
| combined | 50000 | 3034241 | 588.752 ms | 33.823 ms | 103.453 ms | 3.059× | 0.176× | 17.407× |
| arrays | 10000 | 29994 | 109.106 ms | 33.476 ms | 64.679 ms | 1.932× | 0.593× | 3.259× |
| structs | 10000 | 39994 | 93.496 ms | 29.428 ms | 68.362 ms | 2.323× | 0.731× | 3.177× |
| memory | 10000 | 69995 | 102.843 ms | 36.505 ms | 72.744 ms | 1.993× | 0.707× | 2.817× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 52.774 ms | success |
| mixed | 60.766 ms | success |
| functions | 67.872 ms | success |
| branching | 56.268 ms | success |
| combined | 61.169 ms | success |
| arrays | 90.592 ms | success |
| structs | 72.306 ms | success |
| memory | 53.591 ms | success |

## Run statistics

### arithmetic

- interpreter: min 276.622 ms; max 311.307 ms; mean 296.707 ms; median 306.223 ms; standard deviation 17.739 ms; complete.
- native: min 29.285 ms; max 30.662 ms; mean 29.784 ms; median 29.637 ms; standard deviation 0.528 ms; complete.
- python: min 81.173 ms; max 100.693 ms; mean 89.931 ms; median 89.893 ms; standard deviation 8.873 ms; complete.

### mixed

- interpreter: min 90.334 ms; max 116.091 ms; mean 104.000 ms; median 107.217 ms; standard deviation 11.232 ms; complete.
- native: min 35.932 ms; max 46.925 ms; mean 39.698 ms; median 38.400 ms; standard deviation 4.477 ms; complete.
- python: min 67.042 ms; max 86.375 ms; mean 76.362 ms; median 79.572 ms; standard deviation 8.357 ms; complete.

### functions

- interpreter: min 464.805 ms; max 572.115 ms; mean 520.246 ms; median 525.779 ms; standard deviation 39.064 ms; complete.
- native: min 27.893 ms; max 31.370 ms; mean 29.715 ms; median 29.385 ms; standard deviation 1.416 ms; complete.
- python: min 84.052 ms; max 100.544 ms; mean 91.527 ms; median 89.996 ms; standard deviation 6.136 ms; complete.

### branching

- interpreter: min 332.243 ms; max 366.858 ms; mean 353.720 ms; median 353.554 ms; standard deviation 13.609 ms; complete.
- native: min 27.768 ms; max 31.726 ms; mean 28.992 ms; median 28.478 ms; standard deviation 1.579 ms; complete.
- python: min 83.242 ms; max 103.892 ms; mean 90.430 ms; median 84.044 ms; standard deviation 9.485 ms; complete.

### combined

- interpreter: min 581.916 ms; max 667.645 ms; mean 602.447 ms; median 588.752 ms; standard deviation 36.696 ms; complete.
- native: min 32.436 ms; max 37.031 ms; mean 34.077 ms; median 33.823 ms; standard deviation 1.756 ms; complete.
- python: min 98.613 ms; max 131.318 ms; mean 108.282 ms; median 103.453 ms; standard deviation 13.085 ms; complete.

### arrays

- interpreter: min 102.201 ms; max 115.543 ms; mean 109.130 ms; median 109.106 ms; standard deviation 6.246 ms; complete.
- native: min 32.356 ms; max 35.315 ms; mean 33.432 ms; median 33.476 ms; standard deviation 1.215 ms; complete.
- python: min 62.176 ms; max 70.432 ms; mean 66.391 ms; median 64.679 ms; standard deviation 3.666 ms; complete.

### structs

- interpreter: min 71.301 ms; max 95.884 ms; mean 86.041 ms; median 93.496 ms; standard deviation 11.604 ms; complete.
- native: min 26.325 ms; max 32.044 ms; mean 29.274 ms; median 29.428 ms; standard deviation 2.250 ms; complete.
- python: min 62.835 ms; max 78.543 ms; mean 69.988 ms; median 68.362 ms; standard deviation 7.486 ms; complete.

### memory

- interpreter: min 92.847 ms; max 105.278 ms; mean 100.518 ms; median 102.843 ms; standard deviation 5.431 ms; complete.
- native: min 33.070 ms; max 40.443 ms; mean 36.077 ms; median 36.505 ms; standard deviation 2.957 ms; complete.
- python: min 63.830 ms; max 78.360 ms; mean 71.244 ms; median 72.744 ms; standard deviation 5.443 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 1.93× to 3.06× across the checksum-verified comparable workloads; its largest ratio was on `functions`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.171× to 0.742×. The weakest ratio was `functions` (5.84× slower than Python); `mixed` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
