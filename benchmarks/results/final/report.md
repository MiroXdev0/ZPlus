# Z+ vs Python Performance Benchmark

Captured: 2026-11-18T14:20:04.298194+00:00

## Environment

- CPU: Intel(R) Xeon(R) CPU           X5660  @ 2.80GHz
- Logical processors: 12
- RAM: 16.00 GiB total; 5.95 GiB available at capture
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
| arithmetic | 50000 | 2399670 | 289.152 ms | 26.779 ms | 76.281 ms | 27.934 ms | 24.322 ms | 1.043× | 0.908× |
| mixed | 10000 | 155000 | 104.328 ms | 25.971 ms | 70.826 ms | 25.927 ms | 31.250 ms | 0.998× | 1.203× |
| functions | 50000 | 2492250 | 515.184 ms | 32.905 ms | 91.081 ms | 24.916 ms | 28.610 ms | 0.757× | 0.869× |
| branching | 50000 | 3124925000 | 393.580 ms | 33.294 ms | 92.637 ms | 30.828 ms | 28.270 ms | 0.926× | 0.849× |
| combined | 50000 | 3034241 | 671.610 ms | 42.090 ms | 130.428 ms | 57.541 ms | 33.394 ms | 1.367× | 0.793× |
| arrays | 10000 | 29994 | 95.617 ms | 34.105 ms | 77.088 ms | 31.422 ms | 30.066 ms | 0.921× | 0.882× |
| structs | 10000 | 39994 | 104.406 ms | 35.201 ms | 69.350 ms | 31.001 ms | 27.468 ms | 0.881× | 0.780× |
| memory | 10000 | 69995 | 105.680 ms | 37.144 ms | 81.210 ms | 30.133 ms | 31.488 ms | 0.811× | 0.848× |
| tight_loop | 1000000 | 1499999500000 | 4512.911 ms | 25.111 ms | 412.221 ms | 28.509 ms | 29.849 ms | 1.135× | 1.189× |
| recursion | 50000 | 1191560 | 1767.392 ms | 36.384 ms | 123.092 ms | 27.299 ms | 31.604 ms | 0.750× | 0.869× |

## Compilation

| Workload | Native compile wall time | Compile status |
|---|---:|---|
| arithmetic | 109.738 ms | success |
| mixed | 128.112 ms | success |
| functions | 67.035 ms | success |
| branching | 113.791 ms | success |
| combined | 84.873 ms | success |
| arrays | 114.562 ms | success |
| structs | 66.004 ms | success |
| memory | 62.341 ms | success |
| tight_loop | 47.938 ms | success |
| recursion | 97.629 ms | success |

## Run statistics

### arithmetic

- interpreter: min 254.913 ms; max 332.955 ms; mean 288.306 ms; median 289.152 ms; standard deviation 31.530 ms; complete.
- native: min 26.244 ms; max 27.708 ms; mean 26.885 ms; median 26.779 ms; standard deviation 0.534 ms; complete.
- python: min 70.397 ms; max 90.171 ms; mean 77.732 ms; median 76.281 ms; standard deviation 7.697 ms; complete.
- cpp_O2: min 23.413 ms; max 36.862 ms; mean 28.021 ms; median 27.934 ms; standard deviation 5.456 ms; complete.
- cpp_O3: min 23.890 ms; max 27.681 ms; mean 24.977 ms; median 24.322 ms; standard deviation 1.555 ms; complete.

### mixed

- interpreter: min 82.573 ms; max 109.718 ms; mean 99.034 ms; median 104.328 ms; standard deviation 11.096 ms; complete.
- native: min 24.631 ms; max 28.560 ms; mean 26.603 ms; median 25.971 ms; standard deviation 1.684 ms; complete.
- python: min 64.661 ms; max 76.782 ms; mean 70.201 ms; median 70.826 ms; standard deviation 5.278 ms; complete.
- cpp_O2: min 24.411 ms; max 32.468 ms; mean 26.936 ms; median 25.927 ms; standard deviation 3.163 ms; complete.
- cpp_O3: min 29.295 ms; max 34.957 ms; mean 31.857 ms; median 31.250 ms; standard deviation 2.352 ms; complete.

### functions

- interpreter: min 450.913 ms; max 533.364 ms; mean 495.943 ms; median 515.184 ms; standard deviation 39.145 ms; complete.
- native: min 29.483 ms; max 33.466 ms; mean 32.301 ms; median 32.905 ms; standard deviation 1.603 ms; complete.
- python: min 83.467 ms; max 131.625 ms; mean 103.873 ms; median 91.081 ms; standard deviation 23.464 ms; complete.
- cpp_O2: min 24.297 ms; max 27.728 ms; mean 25.723 ms; median 24.916 ms; standard deviation 1.670 ms; complete.
- cpp_O3: min 24.328 ms; max 35.330 ms; mean 29.828 ms; median 28.610 ms; standard deviation 4.846 ms; complete.

### branching

- interpreter: min 375.787 ms; max 434.551 ms; mean 397.605 ms; median 393.580 ms; standard deviation 23.187 ms; complete.
- native: min 28.380 ms; max 38.143 ms; mean 33.719 ms; median 33.294 ms; standard deviation 3.693 ms; complete.
- python: min 81.398 ms; max 111.462 ms; mean 94.213 ms; median 92.637 ms; standard deviation 10.842 ms; complete.
- cpp_O2: min 26.783 ms; max 33.489 ms; mean 30.291 ms; median 30.828 ms; standard deviation 2.574 ms; complete.
- cpp_O3: min 25.079 ms; max 32.911 ms; mean 28.095 ms; median 28.270 ms; standard deviation 3.243 ms; complete.

### combined

- interpreter: min 637.851 ms; max 688.067 ms; mean 666.412 ms; median 671.610 ms; standard deviation 20.223 ms; complete.
- native: min 41.101 ms; max 46.681 ms; mean 43.089 ms; median 42.090 ms; standard deviation 2.402 ms; complete.
- python: min 119.534 ms; max 145.608 ms; mean 132.368 ms; median 130.428 ms; standard deviation 12.635 ms; complete.
- cpp_O2: min 42.410 ms; max 104.723 ms; mean 64.510 ms; median 57.541 ms; standard deviation 23.850 ms; complete.
- cpp_O3: min 30.253 ms; max 37.169 ms; mean 33.816 ms; median 33.394 ms; standard deviation 2.621 ms; complete.

### arrays

- interpreter: min 84.801 ms; max 107.816 ms; mean 95.691 ms; median 95.617 ms; standard deviation 8.941 ms; complete.
- native: min 31.738 ms; max 45.978 ms; mean 35.748 ms; median 34.105 ms; standard deviation 5.811 ms; complete.
- python: min 70.815 ms; max 87.701 ms; mean 77.321 ms; median 77.088 ms; standard deviation 6.890 ms; complete.
- cpp_O2: min 30.282 ms; max 34.777 ms; mean 32.434 ms; median 31.422 ms; standard deviation 2.056 ms; complete.
- cpp_O3: min 25.084 ms; max 35.677 ms; mean 30.206 ms; median 30.066 ms; standard deviation 4.586 ms; complete.

### structs

- interpreter: min 90.219 ms; max 122.362 ms; mean 103.848 ms; median 104.406 ms; standard deviation 11.930 ms; complete.
- native: min 31.588 ms; max 36.636 ms; mean 34.454 ms; median 35.201 ms; standard deviation 2.305 ms; complete.
- python: min 67.994 ms; max 74.696 ms; mean 71.017 ms; median 69.350 ms; standard deviation 3.133 ms; complete.
- cpp_O2: min 29.440 ms; max 37.146 ms; mean 32.061 ms; median 31.001 ms; standard deviation 3.218 ms; complete.
- cpp_O3: min 26.722 ms; max 30.828 ms; mean 28.586 ms; median 27.468 ms; standard deviation 1.924 ms; complete.

### memory

- interpreter: min 97.850 ms; max 132.031 ms; mean 109.584 ms; median 105.680 ms; standard deviation 13.105 ms; complete.
- native: min 36.067 ms; max 40.100 ms; mean 37.676 ms; median 37.144 ms; standard deviation 1.594 ms; complete.
- python: min 77.629 ms; max 90.909 ms; mean 82.799 ms; median 81.210 ms; standard deviation 5.646 ms; complete.
- cpp_O2: min 28.160 ms; max 34.600 ms; mean 30.566 ms; median 30.133 ms; standard deviation 2.597 ms; complete.
- cpp_O3: min 28.076 ms; max 34.044 ms; mean 31.192 ms; median 31.488 ms; standard deviation 2.132 ms; complete.

### tight_loop

- interpreter: min 4291.747 ms; max 4517.468 ms; mean 4454.033 ms; median 4512.911 ms; standard deviation 97.039 ms; complete.
- native: min 22.764 ms; max 28.355 ms; mean 25.700 ms; median 25.111 ms; standard deviation 2.394 ms; complete.
- python: min 383.168 ms; max 440.326 ms; mean 409.417 ms; median 412.221 ms; standard deviation 21.610 ms; complete.
- cpp_O2: min 25.915 ms; max 28.854 ms; mean 27.576 ms; median 28.509 ms; standard deviation 1.491 ms; complete.
- cpp_O3: min 25.848 ms; max 35.606 ms; mean 31.071 ms; median 29.849 ms; standard deviation 4.162 ms; complete.

### recursion

- interpreter: min 1660.274 ms; max 2001.379 ms; mean 1800.678 ms; median 1767.392 ms; standard deviation 130.436 ms; complete.
- native: min 32.176 ms; max 36.655 ms; mean 35.597 ms; median 36.384 ms; standard deviation 1.919 ms; complete.
- python: min 107.536 ms; max 131.142 ms; mean 122.305 ms; median 123.092 ms; standard deviation 8.905 ms; complete.
- cpp_O2: min 25.192 ms; max 30.651 ms; mean 27.854 ms; median 27.299 ms; standard deviation 2.092 ms; complete.
- cpp_O3: min 27.982 ms; max 35.210 ms; mean 31.819 ms; median 31.604 ms; standard deviation 2.930 ms; complete.

## Analysis

The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.

- Native Z+ median speedup over Python ranged from 1.97× to 16.42× across the checksum-verified comparable workloads; its largest ratio was on `tight_loop`.
- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from 0.070× to 0.806×. The weakest ratio was `recursion` (14.36× slower than Python); `arrays` was its strongest result.
- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.
- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.
- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.
- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.

These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.

## Conclusion

No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.
