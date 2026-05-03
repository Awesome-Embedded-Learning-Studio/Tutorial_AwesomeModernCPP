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


@dataclass
class BuildResult:
    path: Path
    success: bool
    duration: float
    output: str


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
    return any(ind in content for ind in indicators)


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
        # Skip build directories
        if 'build' in cmake_file.parts or '.cache' in cmake_file.parts:
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


def build_project(project_dir: Path) -> BuildResult:
    """Build a single CMake project."""
    build_dir = project_dir / '_build_ci'

    # Clean previous build artifacts
    if build_dir.exists():
        shutil.rmtree(build_dir)

    start = time.time()
    all_output = []

    # Configure
    configure_cmd = ['cmake', '-B', str(build_dir), '-G', 'Ninja',
                      '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache']
    try:
        result = subprocess.run(
            configure_cmd,
            cwd=str(project_dir),
            capture_output=True,
            text=True,
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
            timeout=300,
        )
        all_output.append(result.stdout)
        all_output.append(result.stderr)
        success = result.returncode == 0
    except subprocess.TimeoutExpired:
        success = False
        all_output.append('Build timed out (300s)')

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
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count(),
                        help=f'Max concurrent builds (default: {os.cpu_count()})')
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    code_root = project_root / 'code'

    if not code_root.exists():
        print(f"Error: code/ directory not found: {code_root}")
        sys.exit(1)

    target = 'host' if args.host else 'stm32' if args.stm32 else 'all'
    projects = discover_projects(code_root, target)

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
