#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

DEFAULT_GCC_FLAGS = ["-march=rv64gc", "-mabi=lp64d", "-static"]
DEFAULT_WSL_DISTRO = "Ubuntu"
PROGRESS_BAR_WIDTH = 28
EVENT_PREFIX = "@@CODEX_EVENT@@"


@dataclass
class RunnerConfig:
    runner: str
    wsl_distro: str | None
    repo_root: Path


@dataclass
class Toolchain:
    compiler: str
    gcc: str
    qemu: str
    runtime_files: list[str]


@dataclass
class CaseResult:
    name: str
    compiled: bool
    linked: bool
    ran: bool
    passed: bool
    skipped: bool
    stage: str
    return_code: int | None
    elapsed_sec: float | None
    stdout: str
    stderr: str
    detail: str


def normalize_text(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def split_expected_lines(text: str) -> list[str]:
    lines = normalize_text(text).splitlines()
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def build_actual_output(stdout: str, return_code: int) -> str:
    stdout = normalize_text(stdout)
    if stdout and not stdout.endswith("\n"):
        stdout += "\n"
    return f"{stdout}{return_code}\n"


def sanitize_wsl_text(text: str) -> str:
    cleaned = text.replace("\x00", "")
    kept: list[str] = []
    for line in normalize_text(cleaned).split("\n"):
        if line.startswith("wsl:"):
            continue
        kept.append(line)
    return "\n".join(kept)


def decode_text(text: str, *, is_wsl: bool) -> str:
    return sanitize_wsl_text(text) if is_wsl else normalize_text(text)


def as_wsl_path(path: Path) -> str:
    resolved = path.resolve()
    posix = resolved.as_posix()
    if posix.startswith("/mnt/"):
        return posix
    drive = resolved.drive.rstrip(":").lower()
    tail = posix.split(":", 1)[1]
    return f"/mnt/{drive}{tail}"


def resolve_runner(repo_root: Path, requested: str, wsl_distro: str) -> RunnerConfig:
    if requested == "host":
        return RunnerConfig("host", None, repo_root)
    if requested == "wsl":
        return RunnerConfig("wsl", wsl_distro, repo_root)
    if shutil.which("wsl.exe"):
        return RunnerConfig("wsl", wsl_distro, repo_root)
    return RunnerConfig("host", None, repo_root)


def resolve_runtime_files(repo_root: Path, explicit: list[str]) -> list[Path]:
    if explicit:
        files = [repo_root / item if not Path(item).is_absolute() else Path(item) for item in explicit]
        missing = [str(path) for path in files if not path.exists()]
        if missing:
            raise FileNotFoundError(f"运行时文件不存在: {', '.join(missing)}")
        return files
    archive = repo_root / "libsysy_riscv.a"
    if archive.exists():
        return [archive]
    source = repo_root / "sylib.c"
    if source.exists():
        return [source]
    raise FileNotFoundError("未找到运行时文件，请通过 --runtime 指定 libsysy_riscv.a 或 sylib.c")


def build_toolchain(args: argparse.Namespace, config: RunnerConfig) -> Toolchain:
    runtime_files = resolve_runtime_files(config.repo_root, args.runtime)
    return Toolchain(
        compiler=args.compiler,
        gcc=args.gcc,
        qemu=args.qemu,
        runtime_files=[str(path) for path in runtime_files],
    )


def collect_case_names(cases_dir: Path) -> list[str]:
    names: set[str] = set()
    for suffix in (".sy", ".in", ".out"):
        names.update(path.stem for path in cases_dir.glob(f"*{suffix}"))
    return sorted(names)


def resolve_source_path(cases_dir: Path, case_name: str) -> Path | None:
    exact = cases_dir / f"{case_name}.sy"
    if exact.exists():
        return exact
    compact_base = re.sub(r"\d+$", "", case_name)
    compact_candidate = cases_dir / f"{compact_base}.sy"
    if compact_base != case_name and compact_candidate.exists():
        return compact_candidate
    split_base = re.sub(r"[-_]\d+$", "", case_name)
    split_candidate = cases_dir / f"{split_base}.sy"
    if split_base != case_name and split_candidate.exists():
        return split_candidate
    return None


def matches_pattern(name: str, pattern: str) -> bool:
    return Path(name).match(pattern) or Path(f"{name}.sy").match(pattern)


def render_progress(done: int, total: int, *, passed: int, failed: int, current_case: str) -> str:
    total = max(total, 1)
    done = min(done, total)
    filled = done * PROGRESS_BAR_WIDTH // total
    bar = "#" * filled + "-" * (PROGRESS_BAR_WIDTH - filled)
    percent = done * 100.0 / total
    suffix = f" | {current_case}" if current_case else ""
    return f"[{bar}] {done}/{total} {percent:6.2f}% | pass {passed} fail {failed}{suffix}"


def show_progress(
    done: int,
    total: int,
    *,
    passed: int,
    failed: int,
    current_case: str,
    final: bool = False,
) -> None:
    line = render_progress(done, total, passed=passed, failed=failed, current_case=current_case)
    print(f"\r{line}", end="\n" if final else "", file=sys.stderr, flush=True)


def driver_source() -> str:
    return r'''
from __future__ import annotations

import concurrent.futures
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

EVENT_PREFIX = "@@CODEX_EVENT@@"
DEFAULT_GCC_FLAGS = ["-march=rv64gc", "-mabi=lp64d", "-static"]


def emit(payload: dict) -> None:
    sys.stdout.write(EVENT_PREFIX + json.dumps(payload, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def normalize_text(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n")


def split_expected_lines(text: str) -> list[str]:
    lines = normalize_text(text).splitlines()
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def build_actual_output(stdout: str, return_code: int) -> str:
    stdout = normalize_text(stdout)
    if stdout and not stdout.endswith("\n"):
        stdout += "\n"
    return f"{stdout}{return_code}\n"


def run_command(parts: list[str], *, cwd: str, timeout: int, stdin_path: str | None = None) -> subprocess.CompletedProcess[str]:
    stdin_handle = open(stdin_path, "r", encoding="utf-8", newline=None) if stdin_path else None
    try:
        return subprocess.run(
            parts,
            cwd=cwd,
            stdin=stdin_handle,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            check=False,
        )
    finally:
        if stdin_handle is not None:
            stdin_handle.close()


def execute_case(case: dict, cfg: dict, temp_root: str) -> dict:
    name = case["name"]
    source_path = case["source_path"]
    input_path = case["input_path"]
    expected_path = case["expected_path"]
    asm_path = os.path.join(temp_root, name + ".s")
    exe_path = os.path.join(temp_root, name)

    if source_path is None:
        return {"type": "result", "result": {"name": name, "compiled": False, "linked": False, "ran": False, "passed": False, "skipped": True, "stage": "prepare", "return_code": None, "elapsed_sec": None, "stdout": "", "stderr": "", "detail": "missing source file"}}
    if not os.path.exists(expected_path):
        return {"type": "result", "result": {"name": name, "compiled": False, "linked": False, "ran": False, "passed": False, "skipped": False, "stage": "prepare", "return_code": None, "elapsed_sec": None, "stdout": "", "stderr": "", "detail": f"missing expected output: {Path(expected_path).name}"}} 

    compile_cmd = [cfg["compiler"], "-S", "-o", asm_path, source_path]
    if cfg["opt"]:
        compile_cmd.append("-O1")
    try:
        compile_proc = run_command(compile_cmd, cwd=cfg["repo_root"], timeout=cfg["timeout"])
    except subprocess.TimeoutExpired:
        return {"type": "result", "result": {"name": name, "compiled": False, "linked": False, "ran": False, "passed": False, "skipped": False, "stage": "compile", "return_code": None, "elapsed_sec": None, "stdout": "", "stderr": "", "detail": "compile timeout"}}
    compile_stderr = normalize_text(compile_proc.stderr).strip()
    if compile_proc.returncode != 0 or not os.path.exists(asm_path) or os.path.getsize(asm_path) == 0:
        detail = compile_stderr or f"exit code {compile_proc.returncode}"
        return {"type": "result", "result": {"name": name, "compiled": False, "linked": False, "ran": False, "passed": False, "skipped": False, "stage": "compile", "return_code": compile_proc.returncode, "elapsed_sec": None, "stdout": "", "stderr": compile_stderr, "detail": detail}}

    link_cmd = [cfg["gcc"], *DEFAULT_GCC_FLAGS, asm_path, *cfg["runtime_files"], "-o", exe_path]
    try:
        link_proc = run_command(link_cmd, cwd=cfg["repo_root"], timeout=cfg["timeout"])
    except subprocess.TimeoutExpired:
        return {"type": "result", "result": {"name": name, "compiled": True, "linked": False, "ran": False, "passed": False, "skipped": False, "stage": "link", "return_code": None, "elapsed_sec": None, "stdout": "", "stderr": "", "detail": "link timeout"}}
    link_stderr = normalize_text(link_proc.stderr).strip()
    if link_proc.returncode != 0 or not os.path.exists(exe_path) or os.path.getsize(exe_path) == 0:
        detail = link_stderr or f"exit code {link_proc.returncode}"
        return {"type": "result", "result": {"name": name, "compiled": True, "linked": False, "ran": False, "passed": False, "skipped": False, "stage": "link", "return_code": link_proc.returncode, "elapsed_sec": None, "stdout": "", "stderr": link_stderr, "detail": detail}}

    expected_text = Path(expected_path).read_text(encoding="utf-8")
    samples: list[float] = []
    last_rc = None
    last_stdout = ""
    last_stderr = ""
    run_failed_detail = ""

    local_exe = exe_path
    local_input = input_path if input_path and os.path.exists(input_path) else None
    if cfg["repo_root"].startswith("/mnt/"):
        local_exe = os.path.join("/tmp", f"codex_perf_{os.getpid()}_{name}.exe")
        shutil.copy2(exe_path, local_exe)
        if local_input:
            tmp_input = os.path.join("/tmp", f"codex_perf_{os.getpid()}_{name}.in")
            shutil.copy2(local_input, tmp_input)
            local_input = tmp_input

    try:
        for _ in range(cfg["repeat"]):
            started = time.perf_counter()
            try:
                run_proc = run_command([cfg["qemu"], local_exe], cwd=cfg["repo_root"], timeout=cfg["timeout"], stdin_path=local_input)
            except subprocess.TimeoutExpired:
                run_failed_detail = "run timeout"
                break
            elapsed = time.perf_counter() - started
            samples.append(elapsed)
            last_rc = run_proc.returncode
            last_stdout = normalize_text(run_proc.stdout).strip()
            last_stderr = normalize_text(run_proc.stderr).strip()
            if last_rc != 0:
                run_failed_detail = f"non-zero return code {last_rc}"
                break
            actual_text = build_actual_output(run_proc.stdout, run_proc.returncode)
            if split_expected_lines(expected_text) != split_expected_lines(actual_text):
                run_failed_detail = "output mismatch"
                break
    finally:
        if local_exe != exe_path and os.path.exists(local_exe):
            os.remove(local_exe)
        if local_input and input_path and local_input != input_path and os.path.exists(local_input):
            os.remove(local_input)

    if run_failed_detail:
        return {"type": "result", "result": {"name": name, "compiled": True, "linked": True, "ran": bool(samples), "passed": False, "skipped": False, "stage": "run", "return_code": last_rc, "elapsed_sec": statistics.median(samples) if samples else None, "stdout": last_stdout, "stderr": last_stderr, "detail": run_failed_detail}}
    return {"type": "result", "result": {"name": name, "compiled": True, "linked": True, "ran": True, "passed": True, "skipped": False, "stage": "ok", "return_code": last_rc, "elapsed_sec": statistics.median(samples) if samples else None, "stdout": last_stdout, "stderr": last_stderr, "detail": f"median of {len(samples)} run(s)"}} 


def main() -> int:
    payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    cfg = payload["config"]
    cases = payload["cases"]

    if cfg["build_cmd"]:
        build_proc = subprocess.run(
            cfg["build_cmd"],
            cwd=cfg["repo_root"],
            shell=True,
            executable="/bin/bash",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=cfg["timeout"] * 20,
            check=False,
        )
        if build_proc.stdout.strip():
            emit({"type": "log", "stream": "stdout", "text": normalize_text(build_proc.stdout)})
        if build_proc.stderr.strip():
            emit({"type": "log", "stream": "stderr", "text": normalize_text(build_proc.stderr)})
        if build_proc.returncode != 0:
            emit({"type": "build_failed", "return_code": build_proc.returncode})
            return build_proc.returncode

    temp_parent = cfg["temp_parent"] or None
    with tempfile.TemporaryDirectory(prefix="perf-run-", dir=temp_parent) as temp_root:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, int(cfg["workers"]))) as executor:
            futures = [executor.submit(execute_case, case, cfg, temp_root) for case in cases]
            for future in concurrent.futures.as_completed(futures):
                emit(future.result())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
'''


def write_driver_files(temp_root: Path, payload: dict) -> tuple[Path, Path]:
    driver_path = temp_root / "wsl_driver.py"
    payload_path = temp_root / "payload.json"
    driver_path.write_text(driver_source(), encoding="utf-8", newline="\n")
    payload_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    return driver_path, payload_path


def invoke_driver(config: RunnerConfig, driver_path: Path, payload_path: Path) -> subprocess.Popen[str]:
    if config.runner == "wsl":
        cmd = [
            "wsl.exe",
            "-d",
            config.wsl_distro or DEFAULT_WSL_DISTRO,
            "bash",
            "-lc",
            f"python3 {shlex.quote(as_wsl_path(driver_path))} {shlex.quote(as_wsl_path(payload_path))}",
        ]
    else:
        python_cmd = shutil.which("python3") or shutil.which("python") or sys.executable
        cmd = [python_cmd, str(driver_path), str(payload_path)]
    return subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="运行 SysY 性能测试")
    parser.add_argument("--cases-dir", default="performance", help="性能用例目录")
    parser.add_argument("--pattern", default="*.sy", help="用例匹配模式")
    parser.add_argument("--compiler", default="./compiler", help="编译器命令，默认 ./compiler")
    parser.add_argument("--build-cmd", default="make clean && make -j4 compiler", help="测试前构建命令")
    parser.add_argument("--gcc", default="riscv64-linux-gnu-gcc", help="RISC-V GCC 命令")
    parser.add_argument("--qemu", default="qemu-riscv64", help="QEMU 命令")
    parser.add_argument("--runtime", nargs="*", default=[], help="运行时文件，默认自动探测")
    parser.add_argument("--runner", choices=["auto", "host", "wsl"], default="auto", help="执行环境")
    parser.add_argument("--wsl-distro", default=DEFAULT_WSL_DISTRO, help="WSL 发行版名称")
    parser.add_argument("--timeout", type=int, default=60, help="单阶段超时秒数")
    parser.add_argument("--workers", type=int, default=10, help="WSL 内部并发数")
    parser.add_argument("--repeat", type=int, default=1, help="每个用例运行次数")
    parser.add_argument("--limit", type=int, help="只跑前 N 个用例")
    parser.add_argument("--no-opt", action="store_true", help="不追加 -O1")
    parser.add_argument("--list-only", action="store_true", help="只列出匹配用例")
    parser.add_argument("--no-progress", action="store_true", help="关闭进度条")
    parser.add_argument("--keep-going", action="store_true", help="兼容旧参数，当前始终继续执行")
    parser.add_argument("--summary-json", help="把摘要写入 JSON 文件")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path.cwd().resolve()
    cases_dir = (repo_root / args.cases_dir).resolve()
    if not cases_dir.exists():
        print(f"用例目录不存在: {cases_dir}", file=sys.stderr)
        return 2

    case_names = [name for name in collect_case_names(cases_dir) if matches_pattern(name, args.pattern)]
    if args.limit is not None:
        case_names = case_names[: args.limit]
    if not case_names:
        print("没有找到匹配的性能用例", file=sys.stderr)
        return 2
    if args.list_only:
        for name in case_names:
            print(name)
        return 0

    config = resolve_runner(repo_root, args.runner, args.wsl_distro)
    try:
        toolchain = build_toolchain(args, config)
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    payload = {
        "config": {
            "repo_root": as_wsl_path(repo_root) if config.runner == "wsl" else str(repo_root),
            "temp_parent": as_wsl_path(repo_root) if config.runner == "wsl" else str(repo_root),
            "compiler": toolchain.compiler,
            "gcc": toolchain.gcc,
            "qemu": toolchain.qemu,
            "runtime_files": [as_wsl_path(Path(path)) if config.runner == "wsl" else path for path in toolchain.runtime_files],
            "build_cmd": args.build_cmd or "",
            "timeout": int(args.timeout),
            "workers": int(args.workers),
            "repeat": int(args.repeat),
            "opt": not args.no_opt,
        },
        "cases": [
            {
                "name": name,
                "source_path": (
                    as_wsl_path(resolve_source_path(cases_dir, name))
                    if config.runner == "wsl" and resolve_source_path(cases_dir, name) is not None
                    else (str(resolve_source_path(cases_dir, name)) if resolve_source_path(cases_dir, name) is not None else None)
                ),
                "input_path": as_wsl_path(cases_dir / f"{name}.in") if config.runner == "wsl" else str(cases_dir / f"{name}.in"),
                "expected_path": as_wsl_path(cases_dir / f"{name}.out") if config.runner == "wsl" else str(cases_dir / f"{name}.out"),
            }
            for name in case_names
        ],
    }

    print(f"building compiler with: {args.build_cmd}")

    results: list[CaseResult] = []
    passed_cases = 0
    failed_cases = 0
    total_cases = len(case_names)

    with tempfile.TemporaryDirectory(prefix="codex-perf-driver-", dir=repo_root) as temp_dir:
        temp_root = Path(temp_dir)
        driver_path, payload_path = write_driver_files(temp_root, payload)
        proc = invoke_driver(config, driver_path, payload_path)

        assert proc.stdout is not None
        for raw_line in proc.stdout:
            line = decode_text(raw_line.rstrip("\n"), is_wsl=config.runner == "wsl")
            if not line:
                continue
            if not line.startswith(EVENT_PREFIX):
                print(line)
                continue
            event = json.loads(line[len(EVENT_PREFIX):])
            if event["type"] == "log":
                text = event["text"].rstrip()
                if text:
                    print(text, file=sys.stderr if event["stream"] == "stderr" else sys.stdout)
                continue
            if event["type"] != "result":
                continue
            result = CaseResult(**event["result"])
            results.append(result)
            if result.passed:
                passed_cases += 1
                elapsed = f"{result.elapsed_sec:.3f}s" if result.elapsed_sec is not None else "n/a"
                extra = f" stderr={result.stderr}" if result.stderr else ""
                print(f"[PASS] {result.name} rc={result.return_code} time={elapsed}{extra}")
            elif result.skipped:
                print(f"[SKIP] {result.name} {result.detail}")
            else:
                failed_cases += 1
                extra = f" stderr={result.stderr}" if result.stderr else ""
                print(f"[FAIL] {result.name} {result.detail}{extra}")
            if not args.no_progress:
                show_progress(
                    len(results),
                    total_cases,
                    passed=passed_cases,
                    failed=failed_cases,
                    current_case=result.name,
                )

        stderr_text = decode_text(proc.stderr.read(), is_wsl=config.runner == "wsl") if proc.stderr else ""
        return_code = proc.wait(timeout=max(10, args.timeout * max(1, total_cases)))
        if stderr_text.strip():
            print(stderr_text.rstrip(), file=sys.stderr)
        if return_code != 0 and not results:
            print(f"测试驱动失败，退出码 {return_code}", file=sys.stderr)
            return return_code

    if not args.no_progress:
        show_progress(
            len(results),
            total_cases,
            passed=passed_cases,
            failed=failed_cases,
            current_case=results[-1].name if results else "",
            final=True,
        )

    print(f"summary: {passed_cases}/{len(results)} cases passed")
    if args.summary_json:
        summary = {
            "opt_enabled": not args.no_opt,
            "repeat": args.repeat,
            "results": [asdict(result) for result in results],
        }
        Path(args.summary_json).write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    return 0 if all(result.passed or result.skipped for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
