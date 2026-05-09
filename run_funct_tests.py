#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import json
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
class CaseResult:
    name: str
    compiled: bool
    passed: bool
    stage: str
    detail: str = ""


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


def discover_cases(cases_dir: Path, pattern: str, limit: int | None) -> list[Path]:
    cases = sorted(path for path in cases_dir.glob(pattern) if path.suffix == ".sy")
    if limit is not None:
        cases = cases[:limit]
    return cases


def format_diff(expected: str, actual: str, max_lines: int) -> str:
    diff = list(
        difflib.unified_diff(
            split_expected_lines(expected),
            split_expected_lines(actual),
            fromfile="expected",
            tofile="actual",
            lineterm="",
        )
    )
    if not diff:
        return "output differs, but diff generation failed"
    return "\n".join(diff[:max_lines])


def as_wsl_path(path: Path) -> str:
    resolved = path.resolve()
    posix = resolved.as_posix()
    if posix.startswith("/mnt/"):
        return posix
    drive = resolved.drive.rstrip(":").lower()
    tail = posix.split(":", 1)[1]
    return f"/mnt/{drive}{tail}"


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


def render_progress(
    done: int,
    total: int,
    *,
    compile_passes: int,
    full_passes: int,
    mode: str,
    current_case: str,
) -> str:
    total = max(total, 1)
    done = min(done, total)
    filled = done * PROGRESS_BAR_WIDTH // total
    bar = "#" * filled + "-" * (PROGRESS_BAR_WIDTH - filled)
    percent = done * 100.0 / total
    stats = f"compile {compile_passes}/{done}"
    if mode == "full":
        stats += f" | run {full_passes}/{done}"
    suffix = f" | {current_case}" if current_case else ""
    return f"[{bar}] {done}/{total} {percent:6.2f}% | {stats}{suffix}"


def show_progress(
    done: int,
    total: int,
    *,
    compile_passes: int,
    full_passes: int,
    mode: str,
    current_case: str,
    final: bool = False,
) -> None:
    line = render_progress(
        done,
        total,
        compile_passes=compile_passes,
        full_passes=full_passes,
        mode=mode,
        current_case=current_case,
    )
    print(f"\r{line}", end="\n" if final else "", file=sys.stderr, flush=True)


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
    return []


def build_toolchain(args: argparse.Namespace, config: RunnerConfig) -> Toolchain:
    runtime_files = resolve_runtime_files(config.repo_root, args.runtime)
    return Toolchain(
        compiler=args.compiler,
        gcc=args.gcc or "riscv64-linux-gnu-gcc",
        qemu=args.qemu or "qemu-riscv64",
        runtime_files=[str(path) for path in runtime_files],
    )


def driver_source() -> str:
    return r'''
from __future__ import annotations

import concurrent.futures
import difflib
import json
import os
import shutil
import subprocess
import sys
import tempfile
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


def format_diff(expected: str, actual: str, max_lines: int) -> str:
    diff = list(
        difflib.unified_diff(
            split_expected_lines(expected),
            split_expected_lines(actual),
            fromfile="expected",
            tofile="actual",
            lineterm="",
        )
    )
    if not diff:
        return "output differs, but diff generation failed"
    return "\n".join(diff[:max_lines])


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


def normalize_input(src: str, dst: str) -> None:
    text = Path(src).read_text(encoding="utf-8")
    Path(dst).write_text(normalize_text(text), encoding="utf-8", newline="\n")


def execute_case(case: dict, cfg: dict, temp_root: str) -> dict:
    case_name = case["name"]
    case_path = case["case_path"]
    expected_path = case["expected_path"]
    input_path = case["input_path"]
    asm_path = os.path.join(temp_root, case_name + ".s")
    exe_path = os.path.join(temp_root, case_name)
    normalized_input = os.path.join(temp_root, case_name + ".in")

    if not os.path.exists(expected_path):
        return {"type": "result", "result": {"name": case_name, "compiled": False, "passed": False, "stage": "prepare", "detail": f"missing expected output: {Path(expected_path).name}"}, "compiled_ok": False, "full_ok": False}

    compile_cmd = [cfg["compiler"], "-S", "-o", asm_path, case_path]
    if cfg["opt"]:
        compile_cmd.append("-O1")
    try:
        compile_proc = run_command(compile_cmd, cwd=cfg["repo_root"], timeout=cfg["timeout"])
    except subprocess.TimeoutExpired:
        return {"type": "result", "result": {"name": case_name, "compiled": False, "passed": False, "stage": "compile", "detail": "compile timeout"}, "compiled_ok": False, "full_ok": False}

    if compile_proc.returncode != 0 or not os.path.exists(asm_path) or os.path.getsize(asm_path) == 0:
        detail = normalize_text(compile_proc.stderr).strip() or f"exit code {compile_proc.returncode}"
        return {"type": "result", "result": {"name": case_name, "compiled": False, "passed": False, "stage": "compile", "detail": detail}, "compiled_ok": False, "full_ok": False}

    run_input = None
    if input_path and os.path.exists(input_path):
        normalize_input(input_path, normalized_input)
        run_input = normalized_input

    if cfg["mode"] == "compile":
        return {"type": "result", "result": {"name": case_name, "compiled": True, "passed": True, "stage": "ok", "detail": ""}, "compiled_ok": True, "full_ok": False}

    link_cmd = [cfg["gcc"], *DEFAULT_GCC_FLAGS, asm_path, *cfg["runtime_files"], "-o", exe_path]
    try:
        link_proc = run_command(link_cmd, cwd=cfg["repo_root"], timeout=cfg["timeout"])
    except subprocess.TimeoutExpired:
        return {"type": "result", "result": {"name": case_name, "compiled": True, "passed": False, "stage": "link", "detail": "link timeout"}, "compiled_ok": True, "full_ok": False}

    if link_proc.returncode != 0 or not os.path.exists(exe_path) or os.path.getsize(exe_path) == 0:
        detail = normalize_text(link_proc.stderr).strip() or f"exit code {link_proc.returncode}"
        return {"type": "result", "result": {"name": case_name, "compiled": True, "passed": False, "stage": "link", "detail": detail}, "compiled_ok": True, "full_ok": False}

    local_exe = exe_path
    local_input = run_input
    if cfg["repo_root"].startswith("/mnt/"):
        local_exe = os.path.join("/tmp", f"codex_funct_{os.getpid()}_{case_name}.exe")
        shutil.copy2(exe_path, local_exe)
        if run_input:
            local_input = os.path.join("/tmp", f"codex_funct_{os.getpid()}_{case_name}.in")
            shutil.copy2(run_input, local_input)

    try:
        run_proc = run_command([cfg["qemu"], local_exe], cwd=cfg["repo_root"], timeout=cfg["timeout"], stdin_path=local_input)
    except subprocess.TimeoutExpired:
        return {"type": "result", "result": {"name": case_name, "compiled": True, "passed": False, "stage": "run", "detail": "run timeout"}, "compiled_ok": True, "full_ok": False}
    finally:
        if local_exe != exe_path and os.path.exists(local_exe):
            os.remove(local_exe)
        if local_input and run_input and local_input != run_input and os.path.exists(local_input):
            os.remove(local_input)

    actual_text = build_actual_output(run_proc.stdout, run_proc.returncode)
    expected_text = Path(expected_path).read_text(encoding="utf-8")
    if split_expected_lines(expected_text) == split_expected_lines(actual_text):
        return {"type": "result", "result": {"name": case_name, "compiled": True, "passed": True, "stage": "ok", "detail": ""}, "compiled_ok": True, "full_ok": True}

    diff = format_diff(expected_text, actual_text, cfg["diff_lines"])
    return {"type": "result", "result": {"name": case_name, "compiled": True, "passed": False, "stage": "mismatch", "detail": diff}, "compiled_ok": True, "full_ok": False}


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
    with tempfile.TemporaryDirectory(prefix="funct-run-", dir=temp_parent) as temp_root:
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
    parser = argparse.ArgumentParser(description="批量运行 SysY 功能测试")
    parser.add_argument("--mode", choices=["compile", "full"], default="full", help="compile 只检查编译，full 检查编译和运行结果")
    parser.add_argument("--cases-dir", default="funct", help="测试目录，默认 funct")
    parser.add_argument("--pattern", default="*.sy", help="用例匹配模式")
    parser.add_argument("--compiler", default="./compiler", help="编译器命令，默认 ./compiler")
    parser.add_argument("--build-cmd", help="测试前构建命令，例如 make clean && make -j4 compiler")
    parser.add_argument("--gcc", help="RISC-V GCC 命令，默认 riscv64-linux-gnu-gcc")
    parser.add_argument("--qemu", help="QEMU 命令，默认 qemu-riscv64")
    parser.add_argument("--runtime", nargs="*", default=[], help="运行时文件，默认自动探测 libsysy_riscv.a 或 sylib.c")
    parser.add_argument("--runner", choices=["auto", "host", "wsl"], default="auto", help="执行环境")
    parser.add_argument("--wsl-distro", default=DEFAULT_WSL_DISTRO, help="WSL 发行版名称")
    parser.add_argument("--opt", action="store_true", help="编译时追加 -O1")
    parser.add_argument("--timeout", type=int, default=90, help="单阶段超时秒数")
    parser.add_argument("--workers", type=int, default=10, help="WSL 内部并发数")
    parser.add_argument("--limit", type=int, help="只跑前 N 个用例")
    parser.add_argument("--diff-lines", type=int, default=12, help="输出错误时最多显示多少行 diff")
    parser.add_argument("--list-only", action="store_true", help="只列出匹配到的用例")
    parser.add_argument("--quiet", action="store_true", help="只显示进度和汇总")
    parser.add_argument("--no-progress", action="store_true", help="关闭进度条")
    parser.add_argument("--summary-json", help="把摘要写入 JSON 文件")
    parser.set_defaults(opt=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path.cwd().resolve()
    cases_dir = (repo_root / args.cases_dir).resolve()
    if not cases_dir.exists():
        print(f"用例目录不存在: {cases_dir}", file=sys.stderr)
        return 2

    cases = discover_cases(cases_dir, args.pattern, args.limit)
    if not cases:
        print(f"未找到任何用例: {cases_dir / args.pattern}", file=sys.stderr)
        return 2

    if args.list_only:
        print(f"共发现 {len(cases)} 个用例")
        for case in cases:
            print(case.as_posix())
        return 0

    config = resolve_runner(repo_root, args.runner, args.wsl_distro)
    try:
        toolchain = build_toolchain(args, config)
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if args.mode == "full" and not toolchain.runtime_files:
        print("full 模式需要运行时库，请通过 --runtime 指定 libsysy_riscv.a 或 sylib.c", file=sys.stderr)
        return 2

    payload = {
        "config": {
            "repo_root": as_wsl_path(repo_root) if config.runner == "wsl" else str(repo_root),
            "temp_parent": as_wsl_path(repo_root) if config.runner == "wsl" else str(repo_root),
            "mode": args.mode,
            "compiler": toolchain.compiler,
            "gcc": toolchain.gcc,
            "qemu": toolchain.qemu,
            "runtime_files": [as_wsl_path(Path(path)) if config.runner == "wsl" else path for path in toolchain.runtime_files],
            "build_cmd": args.build_cmd or "",
            "opt": bool(args.opt),
            "timeout": int(args.timeout),
            "workers": int(args.workers),
            "diff_lines": int(args.diff_lines),
        },
        "cases": [
            {
                "name": case.stem,
                "case_path": as_wsl_path(case) if config.runner == "wsl" else str(case),
                "expected_path": as_wsl_path(case.with_suffix(".out")) if config.runner == "wsl" else str(case.with_suffix(".out")),
                "input_path": as_wsl_path(case.with_suffix(".in")) if config.runner == "wsl" else str(case.with_suffix(".in")),
            }
            for case in cases
        ],
    }

    compile_passes = 0
    full_passes = 0
    results: list[CaseResult] = []
    total_cases = len(cases)

    with tempfile.TemporaryDirectory(prefix="codex-funct-driver-", dir=repo_root) as temp_dir:
        temp_root = Path(temp_dir)
        driver_path, payload_path = write_driver_files(temp_root, payload)
        proc = invoke_driver(config, driver_path, payload_path)

        assert proc.stdout is not None
        for raw_line in proc.stdout:
            line = decode_text(raw_line.rstrip("\n"), is_wsl=config.runner == "wsl")
            if not line:
                continue
            if not line.startswith(EVENT_PREFIX):
                if not args.quiet:
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
            if event["compiled_ok"]:
                compile_passes += 1
            if event["full_ok"]:
                full_passes += 1
            if not args.quiet:
                if result.passed:
                    print(f"[PASS] {result.name}")
                elif result.stage == "mismatch":
                    print(f"[FAIL] {result.name} mismatch")
                    print(result.detail)
                else:
                    print(f"[FAIL] {result.name} {result.stage} {result.detail}")
            if not args.no_progress:
                show_progress(
                    len(results),
                    total_cases,
                    compile_passes=compile_passes,
                    full_passes=full_passes,
                    mode=args.mode,
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
            compile_passes=compile_passes,
            full_passes=full_passes,
            mode=args.mode,
            current_case=results[-1].name if results else "",
            final=True,
        )

    total = len(results)
    compile_rate = (compile_passes / total * 100.0) if total else 0.0
    full_rate = (full_passes / total * 100.0) if total else 0.0

    print("=" * 60)
    print(f"执行环境: {config.runner}{'/' + config.wsl_distro if config.runner == 'wsl' else ''}")
    print(f"用例数: {total}")
    print(f"编译通过: {compile_passes}")
    print(f"编译通过率: {compile_rate:.2f}%")
    if args.mode == "full":
        print(f"运行通过: {full_passes}")
        print(f"运行通过率: {full_rate:.2f}%")
    else:
        print(f"运行通过: 跳过 (mode={args.mode})")
    stage_summary: dict[str, int] = {}
    for result in results:
        if result.passed:
            continue
        stage_summary[result.stage] = stage_summary.get(result.stage, 0) + 1
    if stage_summary:
        print("失败分布:")
        for stage, count in sorted(stage_summary.items()):
            print(f"  {stage}: {count}")
    print("=" * 60)

    exit_code = 0 if compile_passes == total else 1
    if args.mode != "compile":
        exit_code = 0 if full_passes == total else 1

    if args.summary_json:
        summary_path = Path(args.summary_json)
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        summary = {
            "mode": args.mode,
            "total": total,
            "compile_passes": compile_passes,
            "compile_rate": round(compile_rate, 2),
            "full_passes": full_passes,
            "full_rate": round(full_rate, 2),
            "stage_summary": stage_summary,
            "exit_code": exit_code,
            "results": [asdict(result) for result in results],
        }
        summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
