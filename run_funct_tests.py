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
from dataclasses import dataclass
from pathlib import Path

DEFAULT_GCC_FLAGS = ["-march=rv64gc", "-mabi=lp64d", "-static"]
DEFAULT_WSL_DISTRO = "Ubuntu"
PROGRESS_BAR_WIDTH = 28


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


def normalize_input_file(src: Path, dst: Path) -> None:
    data = src.read_text(encoding="utf-8")
    dst.write_text(normalize_text(data), encoding="utf-8", newline="\n")


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
        return "输出不同，但无法生成差异。"
    return "\n".join(diff[:max_lines])


def as_wsl_path(path: Path) -> str:
    resolved = path.resolve()
    posix = resolved.as_posix()
    if posix.startswith("/mnt/"):
        return posix
    drive = resolved.drive.rstrip(":").lower()
    tail = posix.split(":", 1)[1]
    return f"/mnt/{drive}{tail}"


def shell_join(parts: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


def sanitize_wsl_text(text: str) -> str:
    cleaned = text.replace("\x00", "")
    lines = normalize_text(cleaned).split("\n")
    kept = []
    for line in lines:
        if line.startswith("wsl:"):
            continue
        kept.append(line)
    return "\n".join(kept)


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


def run_runner_command(
    config: RunnerConfig,
    command: str,
    *,
    timeout: int,
) -> subprocess.CompletedProcess[bytes]:
    if config.runner == "wsl":
        cmd = ["wsl.exe", "-d", config.wsl_distro or DEFAULT_WSL_DISTRO, "bash", "-lc", command]
    else:
        cmd = ["bash", "-lc", command]
    return subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )


def decode_stream(data: bytes, *, is_wsl: bool) -> str:
    text = data.decode("utf-8", errors="replace")
    return sanitize_wsl_text(text) if is_wsl else normalize_text(text)


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
    compiler = args.compiler
    gcc = args.gcc or "riscv64-linux-gnu-gcc"
    qemu = args.qemu or "qemu-riscv64"
    return Toolchain(compiler=compiler, gcc=gcc, qemu=qemu, runtime_files=[str(path) for path in runtime_files])


def run_build_if_needed(args: argparse.Namespace, config: RunnerConfig) -> int:
    if not args.build_cmd:
        return 0
    repo = as_wsl_path(config.repo_root) if config.runner == "wsl" else str(config.repo_root)
    command = f"cd {shlex.quote(repo)} && {args.build_cmd}"
    proc = run_runner_command(config, command, timeout=args.timeout * 20)
    stdout = decode_stream(proc.stdout, is_wsl=config.runner == "wsl")
    stderr = decode_stream(proc.stderr, is_wsl=config.runner == "wsl")
    if stdout.strip():
        print(stdout.rstrip())
    if stderr.strip():
        print(stderr.rstrip(), file=sys.stderr)
    return proc.returncode


def compile_case(
    config: RunnerConfig,
    toolchain: Toolchain,
    case: Path,
    asm_path: Path,
    *,
    enable_opt: bool,
    timeout: int,
) -> subprocess.CompletedProcess[bytes]:
    repo = as_wsl_path(config.repo_root) if config.runner == "wsl" else str(config.repo_root)
    case_ref = as_wsl_path(case) if config.runner == "wsl" else str(case)
    asm_ref = as_wsl_path(asm_path) if config.runner == "wsl" else str(asm_path)
    parts = [toolchain.compiler, "-S", "-o", asm_ref, case_ref]
    if enable_opt:
        parts.append("-O1")
    command = f"cd {shlex.quote(repo)} && {shell_join(parts)}"
    return run_runner_command(config, command, timeout=timeout)


def link_case(
    config: RunnerConfig,
    toolchain: Toolchain,
    asm_path: Path,
    exe_path: Path,
    *,
    timeout: int,
) -> subprocess.CompletedProcess[bytes]:
    repo = as_wsl_path(config.repo_root) if config.runner == "wsl" else str(config.repo_root)
    asm_ref = as_wsl_path(asm_path) if config.runner == "wsl" else str(asm_path)
    exe_ref = as_wsl_path(exe_path) if config.runner == "wsl" else str(exe_path)
    runtime_refs = [as_wsl_path(Path(path)) if config.runner == "wsl" else path for path in toolchain.runtime_files]
    parts = [toolchain.gcc, *DEFAULT_GCC_FLAGS, asm_ref, *runtime_refs, "-o", exe_ref]
    command = f"cd {shlex.quote(repo)} && {shell_join(parts)}"
    return run_runner_command(config, command, timeout=timeout)


def run_case(
    config: RunnerConfig,
    toolchain: Toolchain,
    exe_path: Path,
    input_path: Path | None,
    *,
    timeout: int,
) -> subprocess.CompletedProcess[bytes]:
    repo = as_wsl_path(config.repo_root) if config.runner == "wsl" else str(config.repo_root)
    exe_ref = as_wsl_path(exe_path) if config.runner == "wsl" else str(exe_path)
    if input_path is not None and input_path.exists():
        input_ref = as_wsl_path(input_path) if config.runner == "wsl" else str(input_path)
        command = f"cd {shlex.quote(repo)} && {toolchain.qemu} {shlex.quote(exe_ref)} < {shlex.quote(input_ref)}"
    else:
        command = f"cd {shlex.quote(repo)} && {toolchain.qemu} {shlex.quote(exe_ref)}"
    return run_runner_command(config, command, timeout=timeout)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="批量运行 funct 用例并统计编译/执行通过率")
    parser.add_argument("--mode", choices=["compile", "full"], default="full", help="compile 只统计编译通过率，full 统计真实执行通过率")
    parser.add_argument("--cases-dir", default="funct", help="用例目录，默认 funct")
    parser.add_argument("--pattern", default="*.sy", help="用例匹配模式，默认 *.sy")
    parser.add_argument("--compiler", default="./compiler", help="编译器命令，默认 ./compiler")
    parser.add_argument("--build-cmd", help="测试前先执行的构建命令，例如 make compiler")
    parser.add_argument("--gcc", help="RISC-V GCC 命令，默认 riscv64-linux-gnu-gcc")
    parser.add_argument("--qemu", help="QEMU 命令，默认 qemu-riscv64")
    parser.add_argument("--runtime", nargs="*", default=[], help="运行时文件列表；默认自动检测 libsysy_riscv.a 或 sylib.c")
    parser.add_argument("--runner", choices=["auto", "host", "wsl"], default="auto", help="命令执行环境，默认 auto")
    parser.add_argument("--wsl-distro", default=DEFAULT_WSL_DISTRO, help="WSL 发行版名称，默认 Ubuntu")
    parser.add_argument("--opt", action="store_true", help="调用编译器时追加 -O1")
    parser.add_argument("--timeout", type=int, default=120, help="单个阶段超时秒数，默认 120")
    parser.add_argument("--limit", type=int, help="仅运行前 N 个用例")
    parser.add_argument("--diff-lines", type=int, default=12, help="失败时最多显示多少行 diff")
    parser.add_argument("--list-only", action="store_true", help="仅列出用例，不实际执行")
    parser.add_argument("--quiet", action="store_true", help="只输出失败用例和最终汇总")
    parser.add_argument("--no-progress", action="store_true", help="关闭进度条显示")
    parser.add_argument("--summary-json", help="将汇总结果写入 JSON 文件")
    parser.set_defaults(opt=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path.cwd()
    cases_dir = repo_root / args.cases_dir
    if not cases_dir.exists():
        print(f"用例目录不存在: {cases_dir}", file=sys.stderr)
        return 2

    cases = discover_cases(cases_dir, args.pattern, args.limit)
    if not cases:
        print(f"未找到任何用例: {cases_dir / args.pattern}", file=sys.stderr)
        return 2

    if args.list_only:
        print(f"共发现 {len(cases)} 个用例:")
        for case in cases:
            print(case.as_posix())
        return 0

    config = resolve_runner(repo_root, args.runner, args.wsl_distro)
    try:
        toolchain = build_toolchain(args, config)
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    build_rc = run_build_if_needed(args, config)
    if build_rc != 0:
        print(f"构建失败，退出码 {build_rc}", file=sys.stderr)
        return build_rc

    if args.mode == "full" and not toolchain.runtime_files:
        print("full 模式缺少运行时文件，请提供 --runtime 或将 libsysy_riscv.a / sylib.c 放在仓库根目录。", file=sys.stderr)
        return 2

    compile_passes = 0
    full_passes = 0
    results: list[CaseResult] = []
    total_cases = len(cases)

    def update_progress(current_case: str, *, final: bool = False) -> None:
        if args.no_progress:
            return
        show_progress(
            len(results),
            total_cases,
            compile_passes=compile_passes,
            full_passes=full_passes,
            mode=args.mode,
            current_case=current_case,
            final=final,
        )

    with tempfile.TemporaryDirectory(prefix="funct-run-", dir=repo_root) as temp_dir:
        temp_root = Path(temp_dir)

        for case in cases:
            case_name = case.stem
            expected_path = case.with_suffix(".out")
            input_path = case.with_suffix(".in")
            normalized_input_path = temp_root / f"{case_name}.in"
            asm_path = temp_root / f"{case_name}.s"
            exe_path = temp_root / case_name

            if not expected_path.exists():
                results.append(CaseResult(case_name, False, False, "prepare", f"缺少期望输出文件: {expected_path.name}"))
                if not args.quiet:
                    print(f"[FAIL] {case_name} prepare 缺少 {expected_path.name}")
                update_progress(case_name)
                continue

            try:
                compile_proc = compile_case(config, toolchain, case, asm_path, enable_opt=args.opt, timeout=args.timeout)
            except subprocess.TimeoutExpired:
                results.append(CaseResult(case_name, False, False, "compile", "编译超时"))
                if not args.quiet:
                    print(f"[FAIL] {case_name} compile 编译超时")
                update_progress(case_name)
                continue

            compile_stderr = decode_stream(compile_proc.stderr, is_wsl=config.runner == "wsl").strip()
            if compile_proc.returncode != 0 or not asm_path.exists() or asm_path.stat().st_size == 0:
                detail = compile_stderr or f"退出码 {compile_proc.returncode}"
                results.append(CaseResult(case_name, False, False, "compile", detail))
                if not args.quiet:
                    print(f"[FAIL] {case_name} compile {detail}")
                update_progress(case_name)
                continue

            compile_passes += 1
            if input_path.exists():
                normalize_input_file(input_path, normalized_input_path)
            if args.mode == "compile":
                results.append(CaseResult(case_name, True, True, "ok"))
                if not args.quiet:
                    print(f"[PASS] {case_name}")
                update_progress(case_name)
                continue

            try:
                link_proc = link_case(config, toolchain, asm_path, exe_path, timeout=args.timeout)
            except subprocess.TimeoutExpired:
                results.append(CaseResult(case_name, True, False, "link", "链接超时"))
                if not args.quiet:
                    print(f"[FAIL] {case_name} link 链接超时")
                update_progress(case_name)
                continue

            link_stderr = decode_stream(link_proc.stderr, is_wsl=config.runner == "wsl").strip()
            if link_proc.returncode != 0 or not exe_path.exists() or exe_path.stat().st_size == 0:
                detail = link_stderr or f"退出码 {link_proc.returncode}"
                results.append(CaseResult(case_name, True, False, "link", detail))
                if not args.quiet:
                    print(f"[FAIL] {case_name} link {detail}")
                update_progress(case_name)
                continue

            try:
                run_input_path = normalized_input_path if input_path.exists() else None
                run_proc = run_case(config, toolchain, exe_path, run_input_path, timeout=args.timeout)
            except subprocess.TimeoutExpired:
                results.append(CaseResult(case_name, True, False, "run", "运行超时"))
                if not args.quiet:
                    print(f"[FAIL] {case_name} run 运行超时")
                update_progress(case_name)
                continue

            actual_stdout = decode_stream(run_proc.stdout, is_wsl=config.runner == "wsl")
            actual_text = build_actual_output(actual_stdout, run_proc.returncode)
            expected_text = expected_path.read_text(encoding="utf-8")
            if split_expected_lines(expected_text) == split_expected_lines(actual_text):
                full_passes += 1
                results.append(CaseResult(case_name, True, True, "ok"))
                if not args.quiet:
                    print(f"[PASS] {case_name}")
                update_progress(case_name)
                continue

            diff = format_diff(expected_text, actual_text, args.diff_lines)
            results.append(CaseResult(case_name, True, False, "mismatch", diff))
            print(f"[FAIL] {case_name} mismatch")
            print(diff)
            update_progress(case_name)

    update_progress(results[-1].name if results else "", final=True)

    total = len(results)
    compile_rate = (compile_passes / total * 100.0) if total else 0.0
    full_rate = (full_passes / total * 100.0) if total else 0.0

    print("=" * 60)
    print(f"执行环境: {config.runner}{'/' + config.wsl_distro if config.runner == 'wsl' else ''}")
    print(f"总用例: {total}")
    print(f"编译通过: {compile_passes}")
    print(f"编译通过率: {compile_rate:.2f}%")
    if args.mode == "full":
        print(f"执行通过: {full_passes}")
        print(f"执行通过率: {full_rate:.2f}%")
    else:
        print(f"执行通过: 未运行 (mode={args.mode})")

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
        }
        summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
