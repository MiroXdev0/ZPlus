#!/usr/bin/env python3
"""Reproducible process-wall-clock benchmarks for Z+ and Python."""

from __future__ import annotations

import argparse
import csv
import ctypes
import datetime as dt
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = ROOT / "benchmarks" / "programs"
WORKLOADS = (
    "arithmetic",
    "mixed",
    "functions",
    "branching",
    "combined",
    "arrays",
    "structs",
    "memory",
    "tight_loop",
    "recursion",
)
ITERATION_ARGUMENTS = {
    "arithmetic": "arithmetic_iterations",
    "mixed": "mixed_iterations",
    "functions": "function_iterations",
    "branching": "branching_iterations",
    "combined": "combined_iterations",
    "arrays": "arrays_iterations",
    "structs": "structs_iterations",
    "memory": "memory_iterations",
    "tight_loop": "tight_loop_iterations",
    "recursion": "recursion_iterations",
}
CHECKSUM_RE = re.compile(r"^CHECKSUM ([a-z_]+) (-?\d+)\s*$", re.MULTILINE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warmups", type=int, default=3, help="warm-up processes per mode (default: 3)")
    parser.add_argument("--runs", type=int, default=10, help="measured processes per mode (default: 10)")
    parser.add_argument("--iterations", type=int, help="override iterations for every workload")
    parser.add_argument("--arithmetic-iterations", type=int, default=50000)
    parser.add_argument("--mixed-iterations", type=int, default=10000)
    parser.add_argument("--function-iterations", type=int, default=50000)
    parser.add_argument("--branching-iterations", type=int, default=50000)
    parser.add_argument("--combined-iterations", type=int, default=50000)
    parser.add_argument("--arrays-iterations", type=int, default=10000)
    parser.add_argument("--structs-iterations", type=int, default=10000)
    parser.add_argument("--memory-iterations", type=int, default=10000)
    parser.add_argument("--tight-loop-iterations", type=int, default=1000000)
    parser.add_argument("--recursion-iterations", type=int, default=50000)
    parser.add_argument("--workloads", nargs="+", choices=WORKLOADS, default=list(WORKLOADS))
    parser.add_argument("--output-dir", type=Path, default=ROOT / "benchmarks" / "results" / "latest")
    parser.add_argument("--zp", type=Path, help="compiler executable (default: bin/zp.exe or bin/zp)")
    parser.add_argument("--timeout", type=float, default=120.0, help="timeout per process in seconds")
    parser.add_argument("--compiler-config", default="Release", help="compiler build configuration to report")
    parser.add_argument("--cpp-compiler", help="C++ compiler for -O2/-O3 reference binaries")
    args = parser.parse_args()
    if args.warmups < 0 or args.runs < 1:
        parser.error("--warmups must be non-negative and --runs must be at least one")
    for workload in WORKLOADS:
        value = getattr(args, ITERATION_ARGUMENTS[workload])
        if value < 1:
            parser.error(f"--{workload}-iterations must be positive")
    if args.iterations is not None and args.iterations < 1:
        parser.error("--iterations must be positive")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def compiler_path(requested: Path | None) -> Path:
    if requested is not None:
        return requested.expanduser().resolve()
    candidates = (
        ROOT / "bin" / "zp.exe",
        ROOT / "bin" / "zp",
        ROOT / "build" / "Release" / "zp.exe",
        ROOT / "build" / "zp",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    located = shutil.which("zp") or shutil.which("zp.exe")
    if located:
        return Path(located).resolve()
    raise FileNotFoundError("Z+ compiler not found; pass --zp or build bin/zp.exe")


def cpp_compiler_path(requested: str | None) -> Path | None:
    if requested:
        located = shutil.which(requested) or requested
        candidate = Path(located).expanduser()
        if candidate.is_file():
            return candidate.resolve()
        return None
    for name in ("g++", "clang++", "c++"):
        located = shutil.which(name)
        if located:
            return Path(located).resolve()
    return None


def cpu_model() -> str:
    if sys.platform == "win32":
        try:
            import winreg

            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0",
            ) as key:
                return str(winreg.QueryValueEx(key, "ProcessorNameString")[0]).strip()
        except OSError:
            pass
    if platform.processor():
        return platform.processor()
    if sys.platform.startswith("linux"):
        try:
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
                if line.lower().startswith(("model name", "hardware")):
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    return platform.machine() or "unavailable"


def memory_info() -> tuple[int | None, int | None]:
    if sys.platform == "win32":
        class MemoryStatus(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong),
                ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong),
                ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong),
                ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong),
                ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]

        status = MemoryStatus()
        status.dwLength = ctypes.sizeof(status)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return int(status.ullTotalPhys), int(status.ullAvailPhys)
    if hasattr(os, "sysconf"):
        try:
            page_size = os.sysconf("SC_PAGE_SIZE")
            total = int(page_size * os.sysconf("SC_PHYS_PAGES"))
            available = int(page_size * os.sysconf("SC_AVPHYS_PAGES"))
            return total, available
        except (ValueError, OSError):
            pass
    return None, None


def run_process(command: list[str], timeout: float) -> dict[str, Any]:
    started = time.perf_counter_ns()
    try:
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
        elapsed = time.perf_counter_ns() - started
        return {
            "returncode": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
            "duration_ns": elapsed,
            "timed_out": False,
        }
    except subprocess.TimeoutExpired as error:
        elapsed = time.perf_counter_ns() - started
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        return {
            "returncode": None,
            "stdout": stdout,
            "stderr": stderr,
            "duration_ns": elapsed,
            "timed_out": True,
        }
    except OSError as error:
        elapsed = time.perf_counter_ns() - started
        return {
            "returncode": None,
            "stdout": "",
            "stderr": str(error),
            "duration_ns": elapsed,
            "timed_out": False,
        }


def expected_checksum(workload: str, iterations: int) -> int:
    safe_integer = 2**53 - 1
    bounds = {
        "arithmetic": 96 * iterations,
        "mixed": 30 * iterations,
        "functions": 100 * iterations,
        "branching": 3 * iterations * iterations + 1,
        "combined": 206 * iterations,
        "arrays": 6 * iterations,
        "structs": 7 * iterations,
        "memory": 12 * iterations,
        "tight_loop": 3 * iterations * iterations,
        "recursion": 66 * iterations,
    }
    if bounds[workload] > safe_integer or 3 * iterations > safe_integer:
        raise ValueError(
            f"{workload}: {iterations} iterations may exceed the exactly "
            "representable integer range shared by Z+ and Python"
        )
    checksum = 0
    for i in range(iterations):
        if workload == "arithmetic":
            checksum += (i * 3 + 7) % 97
        elif workload == "mixed":
            value = i % 10 + 0.5
            checksum += int((value * 1.5 + 0.25) * 2)
        elif workload == "functions":
            checksum += ((i % 1000) * 7 + 3) % 101
        elif workload == "branching":
            checksum += i * 3 + 1 if i % 2 == 0 else i * 2 - 1
        elif workload == "combined":
            value = ((i % 1000) * 7 + 11) % 103
            checksum += value * 2 if i % 3 == 0 else (value + i % 17) % 89
        elif workload == "arrays":
            checksum += i % 7
        elif workload == "structs":
            checksum += i % 7 + 1
        elif workload == "memory":
            checksum += i % 11 + 2
        elif workload == "tight_loop":
            checksum += i * 3 + 1
        elif workload == "recursion":
            value = i % 12
            checksum += value * (value + 1) // 2
        else:
            raise ValueError(f"unknown workload: {workload}")
    return checksum


def prepare_programs(
    workload: str,
    iterations: int,
    expected: int,
    working_dir: Path,
) -> tuple[Path, Path]:
    zp_source = (PROGRAMS / f"{workload}.zp").read_text(encoding="utf-8")
    py_source = (PROGRAMS / f"{workload}.py").read_text(encoding="utf-8")

    zp_source, zp_iterations = re.subn(
        r"(?m)^(\s*let iterations: number = )\d+(\s*// BENCH_ITERATIONS\s*)$",
        rf"\g<1>{iterations}\g<2>",
        zp_source,
    )
    zp_source, zp_expected = re.subn(
        r"(?m)^(\s*let expected: number = )\d+(\s*// BENCH_EXPECTED\s*)$",
        rf"\g<1>{expected}\g<2>",
        zp_source,
    )
    zp_source, zp_checksum = re.subn(
        rf'(say\("CHECKSUM {re.escape(workload)} )\d+("\))',
        rf"\g<1>{expected}\g<2>",
        zp_source,
    )
    py_source, py_iterations = re.subn(
        r"(?m)^(\s*ITERATIONS = )\d+(\s*# BENCH_ITERATIONS\s*)$",
        rf"\g<1>{iterations}\g<2>",
        py_source,
    )
    py_source, py_expected = re.subn(
        r"(?m)^(\s*EXPECTED_CHECKSUM = )\d+(\s*# BENCH_EXPECTED\s*)$",
        rf"\g<1>{expected}\g<2>",
        py_source,
    )
    if (zp_iterations, zp_expected, zp_checksum, py_iterations, py_expected) != (
        1,
        1,
        1,
        1,
        1,
    ):
        raise ValueError(f"{workload}: benchmark template markers are missing or duplicated")

    zp_path = working_dir / f"{workload}.zp"
    py_path = working_dir / f"{workload}.py"
    zp_path.write_text(zp_source, encoding="utf-8")
    py_path.write_text(py_source, encoding="utf-8")
    return zp_path, py_path


def extract_checksum(output: str, workload: str) -> int | None:
    matches = [
        int(value)
        for name, value in CHECKSUM_RE.findall(output)
        if name == workload
    ]
    return matches[-1] if len(matches) == 1 else None


def validate_run(
    result: dict[str, Any],
    workload: str,
    expected: int,
) -> tuple[bool, int | None, str]:
    observed = extract_checksum(result["stdout"], workload)
    if result["timed_out"]:
        return False, observed, "timeout"
    if result["returncode"] != 0:
        return False, observed, f"exit status {result['returncode']}"
    if observed is None:
        return False, None, "missing or malformed checksum"
    if observed != expected:
        return False, observed, f"checksum mismatch: expected {expected}, got {observed}"
    return True, observed, ""


def summarize(values: list[int]) -> dict[str, float | int] | None:
    if not values:
        return None
    return {
        "min_ns": min(values),
        "max_ns": max(values),
        "mean_ns": statistics.mean(values),
        "median_ns": statistics.median(values),
        "stdev_ns": statistics.stdev(values) if len(values) > 1 else 0.0,
    }


def format_ms(value_ns: float | int | None) -> str:
    return "unavailable" if value_ns is None else f"{value_ns / 1_000_000:.3f} ms"


def format_speedup(value: float | None) -> str:
    return "unavailable" if value is None else f"{value:.3f}×"


def environment_info(zp: Path, compiler_config: str, timeout: float) -> dict[str, Any]:
    version_result = run_process([str(zp), "version"], timeout)
    version = (version_result["stdout"] + version_result["stderr"]).strip()
    memory_bytes, available_memory_bytes = memory_info()
    return {
        "captured_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "operating_system": platform.platform(),
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "cpu_model": cpu_model(),
        "logical_processors": os.cpu_count(),
        "total_memory_bytes": memory_bytes,
        "total_memory_gib": (
            memory_bytes / (1024**3) if memory_bytes is not None else None
        ),
        "available_memory_bytes": available_memory_bytes,
        "available_memory_gib": (
            available_memory_bytes / (1024**3)
            if available_memory_bytes is not None
            else None
        ),
        "python_version": sys.version,
        "python_executable": sys.executable,
        "zplus_version": version or None,
        "zplus_version_returncode": version_result["returncode"],
        "zplus_compiler_path": str(zp),
        "compiler_build_configuration": compiler_config,
    }


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, Any]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def render_report(
    path: Path,
    environment: dict[str, Any],
    configuration: dict[str, Any],
    results: list[dict[str, Any]],
) -> None:
    ram = environment["total_memory_gib"]
    available_ram = environment["available_memory_gib"]
    comparable = [
        item
        for item in results
        if all(value is not None for value in item.get("speedups", {}).values())
    ]
    lines = [
        "# Z+ vs Python Performance Benchmark",
        "",
        f"Captured: {environment['captured_at_utc']}",
        "",
        "## Environment",
        "",
        f"- CPU: {environment['cpu_model']}",
        f"- Logical processors: {environment['logical_processors'] or 'unavailable'}",
        (
            f"- RAM: {ram:.2f} GiB total; {available_ram:.2f} GiB available at capture"
            if ram is not None and available_ram is not None
            else (f"- RAM: {ram:.2f} GiB total" if ram is not None else "- RAM: unavailable")
        ),
        f"- Operating system: {environment['operating_system']}",
        f"- Python: {sys.version.split()[0]}",
        f"- Z+ compiler: {environment['zplus_version'] or 'unavailable'}",
        f"- Compiler build configuration: {environment['compiler_build_configuration']}",
        f"- Compiler executable: `{environment['zplus_compiler_path']}`",
        "",
        "## Methodology",
        "",
        "Each timing is process wall-clock time measured with `time.perf_counter_ns()` and includes process startup, source loading, and (for `zp run`) interpreter/compiler startup. It is not pure function execution time. Native compilation is measured separately and excluded from execution timings. Every timed run is a fresh process.",
        "",
        f"- Warm-ups per available mode: {configuration['warmups']}",
        f"- Measured runs per mode: {configuration['runs']}",
        f"- Timeout per process: {configuration['timeout_seconds']} s",
        "- Checksums are checked against a separately computed deterministic reference before any run is accepted.",
        "- Python and Z+ use the same loop counts and arithmetic expressions for each workload. Z+ prints its expected checksum only if its computed numeric checksum matches; otherwise it emits `CHECKSUM_ERROR`.",
        "- Native compile failures are recorded as unsupported with the original diagnostic; there is no interpreter fallback.",
        "",
        "Workloads: arithmetic/modulo accumulation; mixed fractional/integer arithmetic; function calls; conditional branches; combined scalar work; indexed array updates; mutable struct-field updates; and repeated managed-array allocation.",
        "",
        "## Results",
        "",
        "| Workload | Iterations | Checksum | Z+ Interpreter | Z+ Native | Python | C++ O2 | C++ O3 | Native vs C++ O2 | Native vs C++ O3 |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for item in results:
        summary = item["summary"]
        display_median = lambda mode: (
            summary[mode].get("median_ns")
            if item["mode_status"].get(mode) == "complete" and summary[mode]
            else None
        )
        lines.append(
            "| {name} | {iterations} | {checksum} | {interp} | {native} | {python} | {cpp2} | {cpp3} | {native_cpp2} | {native_cpp3} |".format(
                name=item["workload"],
                iterations=item["iterations"],
                checksum=item["expected_checksum"],
                interp=format_ms(display_median("interpreter")),
                native=format_ms(display_median("native")),
                python=format_ms(display_median("python")),
                cpp2=format_ms(display_median("cpp_O2")),
                cpp3=format_ms(display_median("cpp_O3")),
                native_cpp2=format_speedup(item.get("speedups", {}).get("native_vs_cpp_O2")),
                native_cpp3=format_speedup(item.get("speedups", {}).get("native_vs_cpp_O3")),
            )
        )
    lines.extend(["", "## Compilation", "", "| Workload | Native compile wall time | Compile status |", "|---|---:|---|"])
    for item in results:
        compilation = item["compilation"]
        lines.append(
            f"| {item['workload']} | {format_ms(compilation.get('duration_ns'))} | {compilation.get('status', 'unavailable')} |"
        )
    lines.extend(["", "## Run statistics", ""])
    for item in results:
        lines.extend([f"### {item['workload']}", ""])
        if item["native_status"] == "unsupported":
            lines.extend(
                [
                    "**Native backend: unsupported for this workload.**",
                    "",
                    "```text",
                    (item.get("native_diagnostic") or "No compiler diagnostic captured.").strip(),
                    "```",
                    "",
                ]
            )
        benchmark_modes = ("interpreter", "native", "python", "cpp_O2", "cpp_O3")
        for mode in benchmark_modes:
            stats = item["summary"][mode]
            if stats:
                lines.append(
                    f"- {mode}: min {format_ms(stats['min_ns'])}; max {format_ms(stats['max_ns'])}; mean {format_ms(stats['mean_ns'])}; median {format_ms(stats['median_ns'])}; standard deviation {format_ms(stats['stdev_ns'])}; {item['mode_status'].get(mode, 'unknown')}."
                )
            else:
                lines.append(f"- {mode}: unavailable ({item['mode_status'].get(mode, 'no successful measurements')}).")
        lines.append("")
    if comparable:
        native_ratios = [
            item["speedups"]["native_vs_python"] for item in comparable
        ]
        interpreter_ratios = [
            item["speedups"]["interpreter_vs_python"] for item in comparable
        ]
        best_native = max(comparable, key=lambda item: item["speedups"]["native_vs_python"])
        best_interpreter = max(
            comparable, key=lambda item: item["speedups"]["interpreter_vs_python"]
        )
        worst_interpreter = min(
            comparable, key=lambda item: item["speedups"]["interpreter_vs_python"]
        )
        analysis = [
            f"- Native Z+ median speedup over Python ranged from {min(native_ratios):.2f}× to {max(native_ratios):.2f}× across the checksum-verified comparable workloads; its largest ratio was on `{best_native['workload']}`.",
            f"- The Z+ interpreter was slower than Python in all comparable workloads: its Python-relative ratios ranged from {min(interpreter_ratios):.3f}× to {max(interpreter_ratios):.3f}×. The weakest ratio was `{worst_interpreter['workload']}` ({1 / worst_interpreter['speedups']['interpreter_vs_python']:.2f}× slower than Python); `{best_interpreter['workload']}` was its strongest result.",
            "- The function-call and combined workloads have higher interpreter medians than arithmetic-only. This is consistent with the interpreter paying additional call/dispatch and branch work, but the workloads do more total work and do not isolate a per-call cost.",
            "- Native executable medians are process-level measurements and include startup. Their apparent advantage over Python therefore reflects both startup and workload execution; this harness does not isolate pure loop throughput.",
            "- Native compilation was measured separately. These compilation durations do not establish a compiler performance bottleneck, and are not part of any speedup ratio.",
            "- Native compilation and execution support is reported per workload; no unsupported operation is routed through interpreter execution.",
        ]
    else:
        analysis = [
            "There were no workloads with complete, checksum-verified measurements in all three modes, so this run supports no cross-mode performance conclusion.",
            "Process startup is included in execution measurements. Native compilation is measured separately. Unavailable or failed results are not estimated.",
        ]
    lines.extend(
        [
            "## Analysis",
            "",
            "The speedup column is Python median divided by the corresponding Z+ median; above 1.0× means Z+ was faster for this process-level measurement. The native/interpreter ratio is computed the same way. All comparisons are withheld unless all requested warm-ups and measured runs for all three modes pass checksum validation.",
            "",
            *analysis,
            "",
            "These deterministic synthetic loops do not establish general application performance. Per-workload summary statistics, statuses, diagnostics, and every raw process measurement are in `results.json`, `results.csv`, and `summary.csv`.",
            "",
            "## Conclusion",
            "",
            "No universal performance claim is made. Conclusions are limited to the successfully measured workloads and this machine/configuration. Native support limitations and all failed or timed-out runs are recorded explicitly; unavailable results are not estimated.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    zp = compiler_path(args.zp)
    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    prepared_dir = output_dir / "programs"
    prepared_dir.mkdir(parents=True, exist_ok=True)
    native_dir = output_dir / "native"
    native_dir.mkdir(parents=True, exist_ok=True)
    cpp_dir = output_dir / "cpp"
    cpp_dir.mkdir(parents=True, exist_ok=True)
    cpp_compiler = cpp_compiler_path(args.cpp_compiler)

    iterations = {
        workload: (
            args.iterations
            if args.iterations is not None
            else getattr(args, ITERATION_ARGUMENTS[workload])
        )
        for workload in args.workloads
    }
    environment = environment_info(zp, args.compiler_config, args.timeout)
    configuration = {
        "warmups": args.warmups,
        "runs": args.runs,
        "timeout_seconds": args.timeout,
        "compiler_config": args.compiler_config,
        "cpp_compiler": str(cpp_compiler) if cpp_compiler else None,
        "iterations": iterations,
        "workloads": args.workloads,
        "timing": "process wall-clock using time.perf_counter_ns",
    }
    raw_rows: list[dict[str, Any]] = []
    workload_results: list[dict[str, Any]] = []

    cpp_executables: dict[str, Path] = {}
    if cpp_compiler:
        for optimization in ("O2", "O3"):
            suffix = ".exe" if os.name == "nt" else ""
            executable = cpp_dir / f"cpp_reference_{optimization}{suffix}"
            compile_result = run_process(
                [
                    str(cpp_compiler),
                    "-O" + optimization[1],
                    "-DNDEBUG",
                    str(PROGRAMS / "cpp_reference.cpp"),
                    "-o",
                    str(executable),
                ],
                args.timeout,
            )
            if compile_result["returncode"] == 0 and not compile_result["timed_out"]:
                cpp_executables[optimization] = executable
            else:
                print(
                    f"C++ {optimization} compilation failed: "
                    f"{compile_result['stderr'] or compile_result['stdout']}"
                )

    for workload in args.workloads:
        count = iterations[workload]
        expected = expected_checksum(workload, count)
        zp_source, py_source = prepare_programs(workload, count, expected, prepared_dir)
        item: dict[str, Any] = {
            "workload": workload,
            "iterations": count,
            "expected_checksum": expected,
            "mode_status": {},
            "native_status": "pending",
            "compilation": {"status": "not attempted", "duration_ns": None},
            "raw": {
                "interpreter": [],
                "native": [],
                "python": [],
                "cpp_O2": [],
                "cpp_O3": [],
            },
            "summary": {
                "interpreter": None,
                "native": None,
                "python": None,
                "cpp_O2": None,
                "cpp_O3": None,
            },
            "speedups": {},
        }

        check = run_process([str(zp), "check", str(zp_source)], args.timeout)
        item["check"] = check
        if check["returncode"] != 0 or check["timed_out"]:
            item["check_status"] = "failed"
            item["check_diagnostic"] = check["stderr"] or check["stdout"]
        else:
            item["check_status"] = "passed"

        native_executable = native_dir / (
            f"{workload}.exe" if os.name == "nt" else workload
        )
        compile_result = run_process(
            [str(zp), "build", str(zp_source), "-o", str(native_executable)],
            args.timeout,
        )
        item["compilation"] = {
            **compile_result,
            "status": (
                "success"
                if compile_result["returncode"] == 0
                and not compile_result["timed_out"]
                and native_executable.is_file()
                else "unsupported"
            ),
            "executable": str(native_executable),
        }
        item["native_status"] = (
            "supported"
            if item["compilation"]["status"] == "success"
            else "unsupported"
        )
        if item["native_status"] == "unsupported":
            item["native_diagnostic"] = compile_result["stderr"] or compile_result["stdout"]
        compile_record = {
            "workload": workload,
            "mode": "native",
            "run_type": "compile",
            "run_index": 1,
            "duration_ns": compile_result["duration_ns"],
            "status": item["compilation"]["status"],
            "checksum": None,
            "expected_checksum": expected,
            "detail": item.get("native_diagnostic", ""),
            "stdout": compile_result["stdout"],
            "stderr": compile_result["stderr"],
        }
        raw_rows.append(compile_record)

        command_by_mode = {
            "interpreter": [str(zp), "run", str(zp_source)],
            "python": [sys.executable, str(py_source)],
        }
        for optimization, executable in cpp_executables.items():
            command_by_mode[f"cpp_{optimization}"] = [
                str(executable),
                workload,
                str(count),
                "1",
            ]
        if item["native_status"] == "supported":
            command_by_mode["native"] = [str(native_executable)]

        # A preflight run verifies that each implementation computes the same
        # checksum before any measured timing is considered.
        eligible_modes: set[str] = set()
        benchmark_modes = ("interpreter", "native", "python", "cpp_O2", "cpp_O3")
        for mode in benchmark_modes:
            if mode not in command_by_mode:
                item["mode_status"][mode] = "unsupported"
                continue
            if item["check_status"] != "passed" and mode != "python":
                item["mode_status"][mode] = "not run: Z+ check failed"
                continue
            preflight = run_process(command_by_mode[mode], args.timeout)
            valid, observed, reason = validate_run(preflight, workload, expected)
            preflight_record = {
                "workload": workload,
                "mode": mode,
                "run_type": "preflight",
                "run_index": 0,
                "duration_ns": preflight["duration_ns"],
                "status": "passed" if valid else "failed",
                "checksum": observed,
                "expected_checksum": expected,
                "detail": reason,
                "stdout": preflight["stdout"],
                "stderr": preflight["stderr"],
            }
            raw_rows.append(preflight_record)
            item["raw"][mode].append(preflight_record)
            if valid:
                eligible_modes.add(mode)
                item["mode_status"][mode] = "preflight passed"
            else:
                item["mode_status"][mode] = f"preflight failed: {reason}"

        for mode in benchmark_modes:
            if mode not in eligible_modes:
                item["summary"][mode] = None
                if mode == "native" and item["native_status"] == "unsupported":
                    continue
                continue
            failed = False
            for run_index in range(1, args.warmups + args.runs + 1):
                run_type = "warmup" if run_index <= args.warmups else "measurement"
                sample_index = run_index if run_type == "warmup" else run_index - args.warmups
                run_result = run_process(command_by_mode[mode], args.timeout)
                valid, observed, reason = validate_run(run_result, workload, expected)
                record = {
                    "workload": workload,
                    "mode": mode,
                    "run_type": run_type,
                    "run_index": sample_index,
                    "duration_ns": run_result["duration_ns"],
                    "status": "passed" if valid else "failed",
                    "checksum": observed,
                    "expected_checksum": expected,
                    "detail": reason,
                    "stdout": run_result["stdout"],
                    "stderr": run_result["stderr"],
                }
                raw_rows.append(record)
                item["raw"][mode].append(record)
                if not valid:
                    failed = True
            measurements = [
                record["duration_ns"]
                for record in item["raw"][mode]
                if record["run_type"] == "measurement" and record["status"] == "passed"
            ]
            item["summary"][mode] = summarize(measurements)
            if failed or len(measurements) != args.runs:
                item["mode_status"][mode] = (
                    f"incomplete: {len(measurements)}/{args.runs} valid measured runs"
                )
            else:
                item["mode_status"][mode] = "complete"

        comparable = all(
            item["mode_status"].get(mode) == "complete"
            for mode in ("interpreter", "native", "python")
        )
        if comparable:
            py_median = item["summary"]["python"]["median_ns"]
            interpreter_median = item["summary"]["interpreter"]["median_ns"]
            native_median = item["summary"]["native"]["median_ns"]
            speedups = {
                "native_vs_python": py_median / native_median,
                "interpreter_vs_python": py_median / interpreter_median,
                "native_vs_interpreter": interpreter_median / native_median,
            }
            for cpp_mode in ("cpp_O2", "cpp_O3"):
                cpp_median = (
                    item["summary"][cpp_mode]["median_ns"]
                    if item["summary"][cpp_mode]
                    else None
                )
                if cpp_median is not None:
                    speedups[f"native_vs_{cpp_mode}"] = cpp_median / native_median
                    speedups[f"{cpp_mode}_vs_python"] = py_median / cpp_median
            item["speedups"] = speedups
        else:
            item["speedups"] = {
                "native_vs_python": None,
                "interpreter_vs_python": None,
                "native_vs_interpreter": None,
            }
        workload_results.append(item)
        print(
            f"{workload}: checksum={expected}; interpreter={item['mode_status'].get('interpreter')}; "
            f"native={item['mode_status'].get('native')}; python={item['mode_status'].get('python')}"
        )

    result_document = {
        "schema_version": 1,
        "environment": environment,
        "configuration": configuration,
        "workloads": workload_results,
        "raw_measurements": raw_rows,
    }
    (output_dir / "environment.json").write_text(
        json.dumps(environment, indent=2), encoding="utf-8"
    )
    (output_dir / "results.json").write_text(
        json.dumps(result_document, indent=2), encoding="utf-8"
    )
    raw_fields = [
        "workload",
        "mode",
        "run_type",
        "run_index",
        "duration_ns",
        "status",
        "checksum",
        "expected_checksum",
        "detail",
        "stdout",
        "stderr",
    ]
    write_csv(output_dir / "results.csv", raw_fields, raw_rows)

    summary_rows: list[dict[str, Any]] = []
    for item in workload_results:
        row: dict[str, Any] = {
            "workload": item["workload"],
            "iterations": item["iterations"],
            "expected_checksum": item["expected_checksum"],
            "check_status": item["check_status"],
            "native_status": item["native_status"],
            "compile_status": item["compilation"]["status"],
            "compile_duration_ns": item["compilation"].get("duration_ns"),
        }
        for mode in ("interpreter", "native", "python", "cpp_O2", "cpp_O3"):
            stats = item["summary"][mode] or {}
            row[f"{mode}_status"] = item["mode_status"].get(mode)
            for stat in ("min_ns", "max_ns", "mean_ns", "median_ns", "stdev_ns"):
                row[f"{mode}_{stat}"] = stats.get(stat)
        for speedup, ratio in item["speedups"].items():
            row[speedup] = ratio
        summary_rows.append(row)
    summary_fields = list(summary_rows[0].keys()) if summary_rows else []
    write_csv(output_dir / "summary.csv", summary_fields, summary_rows)
    render_report(output_dir / "report.md", environment, configuration, workload_results)

    valid_comparisons = all(
        all(item["mode_status"].get(mode) == "complete" for mode in ("interpreter", "native", "python"))
        for item in workload_results
    )
    print(f"Reports: {output_dir}")
    return 0 if valid_comparisons else 2


if __name__ == "__main__":
    raise SystemExit(main())
