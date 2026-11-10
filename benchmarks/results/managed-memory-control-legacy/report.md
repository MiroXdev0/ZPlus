# Z+ vs Python Performance Benchmark

Captured: 2026-11-08T10:38:00.814225+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 7.24 GiB available at capture
- Operating system: Windows-11-10.0.26300-SP0
- Python: 3.13.15
- Z+ compiler: Z+ Compiler 0.5.0
- Compiler build configuration: Release
- Compiler executable: `C:\Users\miros\OneDrive\Desktop\Projects\Z+ compiler\build-0.5.0-clean\Release\zp.exe`

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
| arithmetic | 50000 | 2399670 | 196.844 ms | 15.692 ms | 69.023 ms | 4.399× | 0.351× | 12.544× |
| functions | 50000 | 2492250 | 595.736 ms | 16.140 ms | 72.634 ms | 4.500× | 0.122× | 36.909× |
| branching | 50000 | 3124925000 | 255.357 ms | 15.111 ms | 69.749 ms | 4.616× | 0.273× | 16.899× |
| combined | 50000 | 3034241 | 715.517 ms | 17.571 ms | 85.046 ms | 4.840× | 0.119× | 40.722× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 27.144 ms | success |
| functions | 24.443 ms | success |
| branching | 24.693 ms | success |
| combined | 26.132 ms | success |

## Run statistics

### arithmetic

- interpreter: min 195.424 ms; max 285.195 ms; mean 207.284 ms; median 196.844 ms; standard deviation 27.663 ms; complete.
- native: min 14.940 ms; max 17.191 ms; mean 15.816 ms; median 15.692 ms; standard deviation 0.815 ms; complete.
- python: min 67.617 ms; max 71.808 ms; mean 69.214 ms; median 69.023 ms; standard deviation 1.285 ms; complete.

### functions

- interpreter: min 591.133 ms; max 613.332 ms; mean 598.064 ms; median 595.736 ms; standard deviation 7.183 ms; complete.
- native: min 15.765 ms; max 16.290 ms; mean 16.102 ms; median 16.140 ms; standard deviation 0.184 ms; complete.
- python: min 70.096 ms; max 77.201 ms; mean 72.706 ms; median 72.634 ms; standard deviation 1.929 ms; complete.

### branching

- interpreter: min 248.309 ms; max 322.858 ms; mean 262.787 ms; median 255.357 ms; standard deviation 21.674 ms; complete.
- native: min 14.916 ms; max 15.427 ms; mean 15.125 ms; median 15.111 ms; standard deviation 0.161 ms; complete.
- python: min 68.555 ms; max 70.904 ms; mean 69.808 ms; median 69.749 ms; standard deviation 0.771 ms; complete.

### combined

- interpreter: min 711.363 ms; max 734.971 ms; mean 717.646 ms; median 715.517 ms; standard deviation 7.437 ms; complete.
- native: min 17.401 ms; max 17.915 ms; mean 17.604 ms; median 17.571 ms; standard deviation 0.186 ms; complete.
- python: min 82.519 ms; max 89.107 ms; mean 85.478 ms; median 85.046 ms; standard deviation 1.871 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ had a measured median speedup over Python in all comparable workloads, ranging from 4.40× to 4.84×; its largest ratio was on `combined`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.119× to 0.351×. The weakest ratio was `combined` (8.41× slower than Python); `arithmetic` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- All four selected workloads compiled and executed natively in this run. Arrays and other unsupported operations were intentionally not benchmarked; native array support remains unsupported.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
