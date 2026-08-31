#!/usr/bin/env python3
"""
Build Examples — compile all CMake projects under code/

Discovers and builds CMake projects, separating host and STM32 targets.

Usage:
    python3 scripts/build_examples.py --host
    python3 scripts/build_examples.py --stm32
    python3 scripts/build_examples.py --all
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

# --msvc 开关(由 main() 设置):configure 时显式指定 cl 编译器
FORCE_MSVC = False


@dataclass
class BuildResult:
    path: Path
    success: bool
    duration: float
    output: str


# MSVC(Windows)线上已知不兼容、显式跳过的工程,key 为相对 code/ 的 POSIX 路径。
# 判定标准:工程依赖 POSIX 专属 API(epoll/socket/mmap)或 GCC 专属工具链特性,
# 属"平台性示例"——条件编译大改会偏离教学原意,显式跳过并在输出里注明原因。
# 仅 Windows 线生效,Ubuntu(gcc)线不受影响。
MSVC_SKIP_PROJECTS = {
    'volumn_codes/vol8/networking/00-traditional-socket':
        'POSIX socket API,教学即 POSIX 网络',
    'volumn_codes/vol8/networking/01-modern-socket':
        'POSIX socket API,教学即 POSIX 网络',
    'volumn_codes/vol8-labs/lab0-mini-reactor':
        'epoll/eventfd,Linux reactor 教学专属',
    'volumn_codes/vol10/cppcon/2025/02-some-assembly-required':
        'ARM32/GCC 内联汇编教学,MSVC 无对应',
}


def is_stm32_project(cmake_path: Path) -> bool:
    """Detect STM32 cross-compile project by reading CMakeLists.txt."""
    try:
        content = cmake_path.read_text(encoding='utf-8', errors='ignore')
    except Exception:
        return False
    indicators = [
        'CMAKE_SYSTEM_NAME      Generic',
        'CMAKE_SYSTEM_NAME Generic',
        'arm-none-eabi-gcc',
        'arm-none-eabi-g++',
        'cortex-m',
    ]
    if any(ind in content for ind in indicators):
        return True
    # 独立 toolchain 文件范式:交叉编译特征全在共享的 toolchain-*.cmake 里,
    # CMakeLists 本体平台无关(如 stm32-tutorials/f103/*)。这种工程靠路径识别——
    # STM32 线的目录名本身就是意图。
    return any('stm32' in part.lower() for part in cmake_path.parts)


def has_parent_cmake(project_dir: Path, code_root: Path) -> bool:
    """Check if this project is a subdirectory of another CMake project."""
    parent = project_dir.parent
    while parent != code_root and parent != parent.parent:
        parent_cmake = parent / 'CMakeLists.txt'
        if parent_cmake.exists():
            # Check if the parent adds this as a subdirectory
            try:
                content = parent_cmake.read_text(encoding='utf-8', errors='ignore')
                dirname = project_dir.name
                if f'add_subdirectory({dirname})' in content or f'add_subdirectory({dirname} ' in content:
                    return True
            except Exception:
                pass
        parent = parent.parent
    return False


def discover_projects(code_root: Path, target: str) -> list[Path]:
    """Discover top-level CMake projects.

    Args:
        target: 'host', 'stm32', or 'all'
    """
    projects = []
    for cmake_file in sorted(code_root.rglob('CMakeLists.txt')):
        # Skip build directories(_build_ci 是本脚本的构建目录,FetchContent 的
        # _deps/catch2-subbuild 里也有 CMakeLists.txt,不排除会被当成独立工程)
        if any(p == 'build' or p.startswith('_build') for p in cmake_file.parts) \
                or '.cache' in cmake_file.parts:
            continue

        # vol5-labs 练习手册特殊结构:
        #   templates/ 是空实现骨架(给初学者拷贝,不该 CI build);
        #   examples/ 是 standalone 参考实现(由顶层 vol5-labs/CMakeLists.txt 统一 add_subdirectory)。
        # 跳过这两类 standalone CMakeLists, 只 build 顶层 vol5-labs/(它编译已完成的 example)。
        if 'vol5-labs' in cmake_file.parts and ('templates' in cmake_file.parts or 'examples' in cmake_file.parts):
            continue

        project_dir = cmake_file.parent

        # Skip projects that are subdirectories of other CMake projects
        if has_parent_cmake(project_dir, code_root):
            continue

        is_stm32 = is_stm32_project(cmake_file)

        if target == 'host' and not is_stm32:
            projects.append(project_dir)
        elif target == 'stm32' and is_stm32:
            projects.append(project_dir)
        elif target == 'all':
            projects.append(project_dir)

    return projects


def find_toolchain_file(project_dir: Path):
    """向上找共享的 toolchain*.cmake(到 code/ 根为止)。

    独立 toolchain 文件范式的工程(如 stm32-tutorials/*)交叉编译器全在
    toolchain-*.cmake 里,configure 时必须以 -DCMAKE_TOOLCHAIN_FILE 传入;
    内联工具链的工程(老 stm32f1-tutorials)祖先链上没有该文件,不受影响。
    """
    d = project_dir.resolve()
    for _ in range(4):
        for cand in sorted(d.glob('toolchain*.cmake')):
            return cand
        if d.name == 'code' or d.parent == d:
            return None
        d = d.parent
    return None


def build_project(project_dir: Path) -> BuildResult:
    """Build a single CMake project."""
    build_dir = project_dir / '_build_ci'

    # Clean previous build artifacts(ignore_errors:Windows 上残留进程占用文件时
    # 不让清理失败炸掉整个构建,CMake 会覆盖式重配)
    if build_dir.exists():
        shutil.rmtree(build_dir, ignore_errors=True)

    start = time.time()
    all_output = []

    # Configure
    configure_cmd = ['cmake', '-B', str(build_dir), '-G', 'Ninja']
    # ccache 仅在环境里存在时启用(Linux CI 提速);Windows/MSVC 与未装 ccache 的
    # 本地环境自动降级为直连编译,不再因 launcher 缺失而 configure 失败。
    if shutil.which('ccache'):
        configure_cmd.append('-DCMAKE_CXX_COMPILER_LAUNCHER=ccache')
    # --msvc:显式选 cl。Windows 上若 PATH 里有 mingw/MSYS 的 g++,CMake 默认
    # 探测会抢先命中它;显式 cl 才能保证 MSVC 线名副其实(需在 VS 开发者环境下运行)。
    # Release:MSVC 无 build type 时按 Debug 走,默认 /RTC1 与示例的 /O2 冲突(D8016);
    # 基准类示例的语义本来就是优化构建,与 GCC 线手动 -O2 的意图一致。
    if FORCE_MSVC:
        configure_cmd.append('-DCMAKE_CXX_COMPILER=cl')
        configure_cmd.append('-DCMAKE_BUILD_TYPE=Release')
    toolchain = find_toolchain_file(project_dir)
    if toolchain:
        configure_cmd.append(f'-DCMAKE_TOOLCHAIN_FILE={toolchain}')
    try:
        result = subprocess.run(
            configure_cmd,
            cwd=str(project_dir),
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='replace',
            timeout=120,
        )
        all_output.append(result.stdout)
        all_output.append(result.stderr)
        if result.returncode != 0:
            return BuildResult(
                path=project_dir,
                success=False,
                duration=time.time() - start,
                output='\n'.join(all_output),
            )
    except subprocess.TimeoutExpired:
        return BuildResult(
            path=project_dir,
            success=False,
            duration=time.time() - start,
            output='Configure timed out (120s)',
        )
    except FileNotFoundError:
        return BuildResult(
            path=project_dir,
            success=False,
            duration=time.time() - start,
            output='cmake or ninja not found. Install: apt install cmake ninja-build',
        )

    # Build
    build_cmd = ['cmake', '--build', str(build_dir)]
    try:
        result = subprocess.run(
            build_cmd,
            cwd=str(project_dir),
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='replace',
            timeout=300,
        )
        all_output.append(result.stdout)
        all_output.append(result.stderr)
        success = result.returncode == 0
    except subprocess.TimeoutExpired:
        success = False
        all_output.append('Build timed out (300s)')

    # 跑测试(仅当工程配了 CTest: build_dir 里有 CTestTestfile.cmake)。
    # 没配 CTest 的工程(大多数纯示例)直接跳过, 不算失败。
    if success and (build_dir / 'CTestTestfile.cmake').exists():
        ctest_cmd = ['ctest', '--test-dir', str(build_dir),
                     '--output-on-failure', '--timeout', '60']
        try:
            ct = subprocess.run(ctest_cmd, cwd=str(project_dir),
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=180)
            all_output.append('--- ctest ---')
            all_output.append(ct.stdout)
            all_output.append(ct.stderr)
            if ct.returncode != 0:
                success = False
        except subprocess.TimeoutExpired:
            all_output.append('ctest timed out (180s)')
            success = False
        except FileNotFoundError:
            pass  # 环境没 ctest, 跳过

    duration = time.time() - start

    # Cleanup build dir
    if build_dir.exists():
        shutil.rmtree(build_dir, ignore_errors=True)

    return BuildResult(
        path=project_dir,
        success=success,
        duration=duration,
        output='\n'.join(all_output),
    )


def print_results(results: list[BuildResult], code_root: Path) -> None:
    """Print build results summary."""
    in_ci = os.environ.get('CI') is not None
    # Windows 控制台默认 GBK,输出里 errors='replace' 产生的 U+FFFD 会炸 print
    if sys.platform == 'win32':
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    passed = [r for r in results if r.success]
    failed = [r for r in results if not r.success]

    print(flush=True)
    print("=" * 60, flush=True)
    print("Build Results", flush=True)
    print("=" * 60, flush=True)

    for r in results:
        status = "PASS" if r.success else "FAIL"
        rel = r.path.relative_to(code_root)
        print(f"  [{status}] {rel} - {r.duration:.1f}s", flush=True)

    print(flush=True)
    print(f"Total: {len(results)} | Passed: {len(passed)} | Failed: {len(failed)}", flush=True)

    # Print detailed output for all builds, grouped in CI
    for r in results:
        if not r.output.strip():
            continue
        rel = r.path.relative_to(code_root)
        status = "PASS" if r.success else "FAIL"
        if in_ci:
            print(f"\n::group::[{status}] {rel}", flush=True)
        else:
            print(f"\n--- [{status}] {rel} ---", flush=True)

        if r.success:
            # Passing builds: show last 5 lines (configure + build summary)
            lines = r.output.strip().split('\n')
            for line in lines[-5:]:
                print(f"  {line}", flush=True)
        else:
            # Failed builds: show error lines, fallback to last 20
            lines = r.output.strip().split('\n')
            error_lines = [l for l in lines if 'error:' in l.lower()]
            if error_lines:
                for line in error_lines:
                    print(f"  {line}", flush=True)
            else:
                for line in lines[-20:]:
                    print(f"  {line}", flush=True)

        if in_ci:
            print("::endgroup::", flush=True)

    print(flush=True)
    if failed:
        print(f"FAILED: {len(failed)} build(s) failed", flush=True)
    else:
        print("All builds passed!", flush=True)


def main():
    parser = argparse.ArgumentParser(
        description='Build CMake projects under code/')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--host', action='store_true',
                       help='Build host examples only')
    group.add_argument('--stm32', action='store_true',
                       help='Build STM32 cross-compile projects only')
    group.add_argument('--all', action='store_true',
                       help='Build all projects')
    parser.add_argument('--discover', action='store_true',
                        help='Only list discovered projects, do not build')
    parser.add_argument('--msvc', action='store_true',
                        help='Configure with MSVC cl explicitly (run from a VS '
                             'developer environment; also enables the MSVC skip list)')
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count(),
                        help=f'Max concurrent builds (default: {os.cpu_count()})')
    args = parser.parse_args()

    global FORCE_MSVC
    FORCE_MSVC = args.msvc

    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    code_root = project_root / 'code'

    if not code_root.exists():
        print(f"Error: code/ directory not found: {code_root}")
        sys.exit(1)

    target = 'host' if args.host else 'stm32' if args.stm32 else 'all'
    projects = discover_projects(code_root, target)

    # MSVC 线:过滤显式列入 MSVC_SKIP_PROJECTS 的平台性工程
    if sys.platform == 'win32' or args.msvc:
        def skip_reason(p: Path) -> str | None:
            rel = p.relative_to(code_root).as_posix()
            return MSVC_SKIP_PROJECTS.get(rel)

        skipped = [(p, skip_reason(p)) for p in projects if skip_reason(p)]
        projects = [p for p in projects if not skip_reason(p)]
        if skipped:
            print(f"MSVC skip list: {len(skipped)} project(s)", flush=True)
            for p, reason in skipped:
                print(f"  [SKIP] {p.relative_to(code_root).as_posix()} - {reason}", flush=True)
            print(flush=True)

    if not projects:
        print(f"No {target} projects found under {code_root}")
        sys.exit(0)

    print(f"Discovered {len(projects)} {target} project(s):", flush=True)
    for p in projects:
        print(f"  {p.relative_to(code_root)}", flush=True)

    if args.discover:
        sys.exit(0)

    print()
    print(f"Building {len(projects)} project(s) with {args.jobs} worker(s)...", flush=True)
    print(flush=True)

    results_map: dict[Path, BuildResult] = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(build_project, p): p for p in projects
        }
        done_count = 0
        for future in as_completed(futures):
            done_count += 1
            result = future.result()
            rel = result.path.relative_to(code_root)
            status = "OK" if result.success else "FAILED"
            print(f"[{done_count}/{len(projects)}] {rel}: {status} ({result.duration:.1f}s)", flush=True)
            results_map[futures[future]] = result

    results = [results_map[p] for p in projects]
    print_results(results, code_root)
    sys.exit(1 if any(not r.success for r in results) else 0)


if __name__ == '__main__':
    main()
