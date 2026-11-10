# Z+ vs Python benchmark

This benchmark compares equivalent deterministic scalar workloads in the Z+
interpreter, Z+ native backend, and Python. It uses only syntax supported by
the current interpreter and the documented integer-oriented native backend;
the programs intentionally avoid arrays and aggregate values.

Run from the repository root:

```powershell
python benchmarks/runner/benchmark.py
```

Defaults are three warm-up processes and ten measured processes for each of
four workloads, with 50,000 iterations per workload. The runner compiles each
native executable before taking execution timings. It records that one-time
compile duration separately; compilation is not included in native execution
timings.

Options:

```powershell
python benchmarks/runner/benchmark.py --warmups 2 --runs 5
python benchmarks/runner/benchmark.py --iterations 100000
python benchmarks/runner/benchmark.py --arithmetic-iterations 200000 --function-iterations 100000
python benchmarks/runner/benchmark.py --workloads arithmetic combined
python benchmarks/runner/benchmark.py --output-dir benchmarks/results/local-run
python benchmarks/runner/benchmark.py --zp build/Release/zp.exe
```

`--iterations` overrides the per-workload values. Otherwise the individual
`--arithmetic-iterations`, `--function-iterations`, `--branching-iterations`,
and `--combined-iterations` settings apply. Use `--timeout` to change the
per-process timeout and `--compiler-config` to label the compiler build
configuration.

The runner computes a reference checksum for each selected iteration count.
The Z+ programs print the checksum label only if their computed checksum
matches the generated expected value; Python prints its computed checksum.
The runner checks each preflight, warm-up, and measured process output against
that reference. Timings are accepted only for successful checksum-verified
runs. Any failed run or checksum mismatch is recorded and makes that
mode/workload comparison incomplete. Native compilation failures are
preserved with the compiler diagnostic and marked unsupported; there is no
interpreter fallback.

All execution measurements are process wall-clock durations from
`time.perf_counter_ns()`. They include process startup and language/compiler
startup; they do not represent pure function execution time. Native compilation
time is a separate process wall-clock measurement.

The runner creates its output directory and writes:

- `report.md`: environment, methodology, actual results, compile durations,
  statuses, limitations, and per-mode timing statistics.
- `results.json`: machine-readable environment, configuration, workload
  details, diagnostics, raw stdout/stderr, and individual measurements.
- `results.csv`: one row for every preflight, warm-up, and measured execution.
- `summary.csv`: per-workload statistics and speedup ratios.
- `environment.json`: machine, Python, compiler, and configuration data.

`results.csv` also includes one separately timed native compilation row per
workload.

The default output location is `benchmarks/results/latest`. These are
machine-specific measurements, not universal performance claims.

The managed-heap stress workload is separate because native arrays are
explicitly unsupported:

```powershell
.\bin\zp.exe run .\benchmarks\programs\managed_memory.zp
```

It performs 30,000 array allocations with the default automatic threshold,
prints the checksum, estimated heap bytes/object count and collection count,
then explicitly collects and prints the reclaimed-heap statistics. The
checksum is deterministic; byte and object totals depend on runtime layout.
Use a process timer for repeatable wall-clock measurements. This workload
does not expose individual collection-pause timings.
