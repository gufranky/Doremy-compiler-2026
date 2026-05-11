#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import shlex
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path

DEFAULT_GCC_FLAGS = ["-march=rv64gc", "-mabi=lp64d", "-static"]
DEFAULT_WSL_DISTRO = "Ubuntu"


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


def sanitize_wsl_text(text: str) -> str:
    cleaned = text.replace("\x00", "")
    kept: list[str] = []
    for line in normalize_text(cleaned).split("\n"):
        if line.startswith("wsl:"):
            continue
        kept.append(line)
    return "\n".join(kept)


def decode_stream(data: bytes, *, is_wsl: bool) -> str:
    text = data.decode("utf-8", errors="replace")
    return sanitize_wsl_text(text) if is_wsl else normalize_text(text)


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


def build_actual_output(stdout: str, return_code: int) -> str:
    stdout = normalize_text(stdout)
    if stdout and not stdout.endswith("\n"):
        stdout += "\n"
    return f"{stdout}{return_code}\n"


def resolve_runner(repo_root: Path, requested: str, wsl_distro: str) -> RunnerConfig:
    if requested == "host":
        return RunnerConfig("host", None, repo_root)
    if requested == "wsl":
        return RunnerConfig("wsl", wsl_distro, repo_root)
    if shutil.which("wsl.exe"):
        return RunnerConfig("wsl", wsl_distro, repo_root)
    return RunnerConfig("host", None, repo_root)


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
    source_path: Path,
    asm_path: Path,
    *,
    enable_opt: bool,
    timeout: int,
) -> subprocess.CompletedProcess[bytes]:
    repo = as_wsl_path(config.repo_root) if config.runner == "wsl" else str(config.repo_root)
    source_ref = as_wsl_path(source_path) if config.runner == "wsl" else str(source_path)
    asm_ref = as_wsl_path(asm_path) if config.runner == "wsl" else str(asm_path)
    parts = [toolchain.compiler, "-S", "-o", asm_ref, source_ref]
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


def run_case_once(
    config: RunnerConfig,
    toolchain: Toolchain,
    exe_path: Path,
    input_path: Path | None,
    *,
    timeout: int,
) -> tuple[bool, int | None, float | None, str, str]:
    repo = as_wsl_path(config.repo_root) if config.runner == "wsl" else str(config.repo_root)
    exe_ref = as_wsl_path(exe_path) if config.runner == "wsl" else str(exe_path)
    if input_path is not None and input_path.exists():
        input_ref = as_wsl_path(input_path) if config.runner == "wsl" else str(input_path)
        command = f"cd {shlex.quote(repo)} && {toolchain.qemu} {shlex.quote(exe_ref)} < {shlex.quote(input_ref)}"
    else:
        command = f"cd {shlex.quote(repo)} && {toolchain.qemu} {shlex.quote(exe_ref)}"

    started = time.perf_counter()
    try:
        proc = run_runner_command(config, command, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, None, None, "", "timeout"
    elapsed = time.perf_counter() - started
    stdout = decode_stream(proc.stdout, is_wsl=config.runner == "wsl")
    stderr = decode_stream(proc.stderr, is_wsl=config.runner == "wsl")
    return True, proc.returncode, elapsed, stdout, stderr


def collect_case_names(cases_dir: Path) -> list[str]:
    names: set[str] = set()
    for suffix in (".sy", ".in", ".out"):
        names.update(path.stem for path in cases_dir.glob(f"*{suffix}"))
    return sorted(names)


def resolve_source_path(cases_dir: Path, case_name: str) -> Path | None:
    exact = cases_dir / f"{case_name}.sy"
    if exact.exists():
        return exact

    # Support layouts like prime_search.sy + prime_search1.in/.out.
    compact_base = re.sub(r"\d+$", "", case_name)
    compact_candidate = cases_dir / f"{compact_base}.sy"
    if compact_base != case_name and compact_candidate.exists():
        return compact_candidate

    split_base = re.sub(r"[-_]\d+$", "", case_name)
    split_candidate = cases_dir / f"{split_base}.sy"
    if split_base != case_name and split_candidate.exists():
        return split_candidate

    return None


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent
    parser = argparse.ArgumentParser(description="运行 performance 目录中的 SysY 性能样例。")
    parser.add_argument("--cases-dir", default=str(script_dir), help="样例目录，默认当前 performance 目录")
    parser.add_argument("--compiler", default="./compiler", help="编译器命令，默认 ./compiler")
    parser.add_argument("--build-cmd", default="make -j4 compiler", help="测试前执行的构建命令，传空字符串可跳过")
    parser.add_argument("--gcc", default="riscv64-linux-gnu-gcc", help="RISC-V GCC 命令")
    parser.add_argument("--qemu", default="qemu-riscv64", help="QEMU 命令")
    parser.add_argument("--runtime", nargs="*", default=[], help="运行时文件路径，默认自动探测")
    parser.add_argument("--runner", choices=["auto", "host", "wsl"], default="auto", help="命令执行环境")
    parser.add_argument("--wsl-distro", default=DEFAULT_WSL_DISTRO, help="WSL 发行版名称")
    parser.add_argument("--timeout", type=int, default=300, help="单个阶段超时时间，单位秒")
    parser.add_argument("--repeat", type=int, default=1, help="每个用例运行次数，取中位数")
    parser.add_argument("--limit", type=int, help="仅运行前 N 个用例")
    parser.add_argument("--pattern", default="*", help="按基础名过滤，例如 'fft*' 或 '*sort*'")
    parser.add_argument("--no-opt", action="store_true", help="编译时不追加 -O1")
    parser.add_argument("--list-only", action="store_true", help="仅列出将处理的用例")
    parser.add_argument("--keep-going", action="store_true", help="遇到失败继续执行后续用例")
    parser.add_argument("--summary-json", help="将汇总结果写入 JSON 文件")
    parser.add_argument("--repo-root", default=str(repo_root), help="仓库根目录，默认脚本上一级目录")
    return parser.parse_args()


def matches_pattern(name: str, pattern: str) -> bool:
    return Path(name).match(pattern)


def main() -> int:
    args = parse_args()
    repo_root = Path(args.repo_root).resolve()
    cases_dir = Path(args.cases_dir).resolve()

    if not cases_dir.exists():
        print(f"样例目录不存在: {cases_dir}", file=sys.stderr)
        return 2

    case_names = [name for name in collect_case_names(cases_dir) if matches_pattern(name, args.pattern)]
    if args.limit is not None:
        case_names = case_names[: args.limit]

    if not case_names:
        print("没有找到匹配的 performance 用例", file=sys.stderr)
        return 2

    if args.list_only:
        print(f"共发现 {len(case_names)} 个用例")
        for name in case_names:
            print(name)
        return 0

    config = resolve_runner(repo_root, args.runner, args.wsl_distro)
    try:
        toolchain = build_toolchain(args, config)
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if args.build_cmd:
        print(f"构建编译器: {args.build_cmd}")
        build_rc = run_build_if_needed(args, config)
        if build_rc != 0:
            print(f"构建失败，退出码 {build_rc}", file=sys.stderr)
            return build_rc

    results: list[CaseResult] = []
    opt_enabled = not args.no_opt

    with tempfile.TemporaryDirectory(prefix="perf-run-", dir=repo_root) as temp_dir:
        temp_root = Path(temp_dir)
        for name in case_names:
            source_path = resolve_source_path(cases_dir, name)
            input_path = cases_dir / f"{name}.in"
            expected_path = cases_dir / f"{name}.out"
            asm_path = temp_root / f"{name}.s"
            exe_path = temp_root / name

            if source_path is None:
                result = CaseResult(
                    name=name,
                    compiled=False,
                    linked=False,
                    ran=False,
                    passed=False,
                    skipped=True,
                    stage="prepare",
                    return_code=None,
                    elapsed_sec=None,
                    stdout="",
                    stderr="",
                    detail="缺少源文件: 未找到同名 .sy，也未找到可回退的基础名 .sy",
                )
                results.append(result)
                print(f"[SKIP] {name} 缺少对应源文件")
                continue

            if not expected_path.exists():
                result = CaseResult(
                    name=name,
                    compiled=False,
                    linked=False,
                    ran=False,
                    passed=False,
                    skipped=True,
                    stage="prepare",
                    return_code=None,
                    elapsed_sec=None,
                    stdout="",
                    stderr="",
                    detail=f"缺少输出文件: {expected_path.name}",
                )
                results.append(result)
                print(f"[SKIP] {name} 缺少 {expected_path.name}")
                continue

            compile_proc = compile_case(
                config,
                toolchain,
                source_path,
                asm_path,
                enable_opt=opt_enabled,
                timeout=args.timeout,
            )
            compile_stderr = decode_stream(compile_proc.stderr, is_wsl=config.runner == "wsl").strip()
            if compile_proc.returncode != 0 or not asm_path.exists():
                result = CaseResult(
                    name=name,
                    compiled=False,
                    linked=False,
                    ran=False,
                    passed=False,
                    skipped=False,
                    stage="compile",
                    return_code=None,
                    elapsed_sec=None,
                    stdout="",
                    stderr=compile_stderr,
                    detail=f"编译失败: {compile_stderr or f'exit code {compile_proc.returncode}'}",
                )
                results.append(result)
                print(f"[FAIL] {name} compile {result.detail}")
                if not args.keep_going:
                    break
                continue

            link_proc = link_case(config, toolchain, asm_path, exe_path, timeout=args.timeout)
            link_stderr = decode_stream(link_proc.stderr, is_wsl=config.runner == "wsl").strip()
            if link_proc.returncode != 0 or not exe_path.exists():
                result = CaseResult(
                    name=name,
                    compiled=True,
                    linked=False,
                    ran=False,
                    passed=False,
                    skipped=False,
                    stage="link",
                    return_code=None,
                    elapsed_sec=None,
                    stdout="",
                    stderr=link_stderr,
                    detail=f"链接失败: {link_stderr or f'exit code {link_proc.returncode}'}",
                )
                results.append(result)
                print(f"[FAIL] {name} link {result.detail}")
                if not args.keep_going:
                    break
                continue

            samples: list[float] = []
            last_stdout = ""
            last_stderr = ""
            last_rc: int | None = None
            run_ok = True

            for _ in range(args.repeat):
                ok, return_code, elapsed, stdout, stderr = run_case_once(
                    config,
                    toolchain,
                    exe_path,
                    input_path if input_path.exists() else None,
                    timeout=args.timeout,
                )
                if not ok or elapsed is None or return_code is None:
                    run_ok = False
                    last_stderr = stderr
                    break
                samples.append(elapsed)
                last_stdout = stdout
                last_stderr = stderr
                last_rc = return_code

            if not run_ok:
                result = CaseResult(
                    name=name,
                    compiled=True,
                    linked=True,
                    ran=False,
                    passed=False,
                    skipped=False,
                    stage="run",
                    return_code=last_rc,
                    elapsed_sec=None,
                    stdout=last_stdout,
                    stderr=last_stderr,
                    detail=f"运行失败: {last_stderr or 'timeout'}",
                )
                results.append(result)
                print(f"[FAIL] {name} run {result.detail}")
                if not args.keep_going:
                    break
                continue

            expected = expected_path.read_text(encoding="utf-8")
            actual = build_actual_output(last_stdout, last_rc)
            passed = normalize_text(expected) == normalize_text(actual)
            elapsed_sec = statistics.median(samples)

            result = CaseResult(
                name=name,
                compiled=True,
                linked=True,
                ran=True,
                passed=passed,
                skipped=False,
                stage="done" if passed else "check",
                return_code=last_rc,
                elapsed_sec=elapsed_sec,
                stdout=last_stdout,
                stderr=last_stderr,
                detail="ok" if passed else "输出与预期不一致",
            )
            results.append(result)

            status = "PASS" if passed else "FAIL"
            suffix = f" stderr={last_stderr.strip()}" if last_stderr.strip() else ""
            print(f"[{status}] {name} rc={last_rc} time={elapsed_sec:.3f}s{suffix}")
            if not passed and not args.keep_going:
                break

    compiled_count = sum(1 for item in results if item.compiled)
    passed_count = sum(1 for item in results if item.passed)
    skipped_count = sum(1 for item in results if item.skipped)
    total = len(results)
    print(
        f"summary: total={total} compiled={compiled_count} passed={passed_count} skipped={skipped_count}"
    )

    if args.summary_json:
        payload = {
            "repo_root": str(repo_root),
            "cases_dir": str(cases_dir),
            "opt_enabled": opt_enabled,
            "repeat": args.repeat,
            "results": [asdict(item) for item in results],
        }
        Path(args.summary_json).write_text(
            json.dumps(payload, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        print(f"已写入汇总: {args.summary_json}")

    return 0 if all(item.passed or item.skipped for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
