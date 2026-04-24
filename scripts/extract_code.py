#!/usr/bin/env python3
"""从 markdown 文件中提取所有含 main() 的 C++ 代码块，
生成可编译的 .cpp 文件和 CMakeLists.txt。

用法:
  python3 scripts/extract_code.py <docs_dir> <output_dir>
  python3 scripts/extract_code.py documents/vol1-fundamentals code/vol1
  python3 scripts/extract_code.py documents/vol2-modern-features code/vol2 --cstd 20

不传参数时默认提取 vol1-fundamentals。
"""

import argparse
import os
import re
import sys

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CODE_BLOCK_RE = re.compile(r'```cpp\n(.*?)```', re.DOTALL)
FILENAME_RE = re.compile(r'^//\s*(\w+\.cpp)\s*$', re.MULTILINE)
MAIN_RE = re.compile(r'\bint\s+main\s*\(')
CHAPTER_PREFIX_RE = re.compile(r'^ch\d+')


def extract_programs(md_path):
    with open(md_path, 'r', encoding='utf-8') as f:
        content = f.read()

    programs = []
    for i, block in enumerate(CODE_BLOCK_RE.findall(content)):
        if not MAIN_RE.search(block):
            continue

        fn_match = FILENAME_RE.search(block)
        if fn_match:
            filename = fn_match.group(1)
        else:
            md_base = os.path.splitext(os.path.basename(md_path))[0]
            stem = re.sub(r'^\d+-', '', md_base)
            filename = f"{stem}_{i}.cpp"

        programs.append((filename, block.strip()))
    return programs


def sanitize_filename(name):
    return name.replace(' ', '_')


def process_chapter(doc_dir, out_dir, ch_name, cxx_std):
    ch_dir = os.path.join(doc_dir, ch_name)
    if not os.path.isdir(ch_dir):
        return []

    ch_out_dir = os.path.join(out_dir, ch_name)
    os.makedirs(ch_out_dir, exist_ok=True)

    all_programs = []
    md_files = sorted(
        f for f in os.listdir(ch_dir)
        if f.endswith('.md') and not f.endswith('.en.md')
        and f not in ('index.md', 'tags.md')
    )

    for md_file in md_files:
        md_path = os.path.join(ch_dir, md_file)
        for filename, code in extract_programs(md_path):
            filename = sanitize_filename(filename)
            base, ext = os.path.splitext(filename)
            counter = 1
            final_name = filename
            while any(fn == final_name for fn, _ in all_programs):
                final_name = f"{base}_{counter}{ext}"
                counter += 1
            all_programs.append((final_name, code))
            print(f"    {md_file} -> {final_name}")

    if not all_programs:
        return []

    for filename, code in all_programs:
        filepath = os.path.join(ch_out_dir, filename)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(code + '\n')

    # CMake project name: use chapter dir name
    cmake_path = os.path.join(ch_out_dir, "CMakeLists.txt")
    project_name = ch_name.replace('-', '_')
    with open(cmake_path, 'w', encoding='utf-8') as f:
        f.write("cmake_minimum_required(VERSION 3.20)\n")
        f.write(f"project({project_name} VERSION 0.0.1 LANGUAGES CXX)\n\n")
        f.write(f"set(CMAKE_CXX_STANDARD {cxx_std})\n")
        f.write("set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n")
        for filename, _ in all_programs:
            target = os.path.splitext(filename)[0]
            f.write(f"add_executable({target} {filename})\n")

    return all_programs


def process_dir(doc_dir, out_dir, cxx_std):
    """Process all ch* subdirectories under doc_dir."""
    if not os.path.isdir(doc_dir):
        print(f"Error: {doc_dir} not found")
        sys.exit(1)

    chapters = sorted(
        d for d in os.listdir(doc_dir)
        if os.path.isdir(os.path.join(doc_dir, d)) and CHAPTER_PREFIX_RE.match(d)
    )

    if not chapters:
        print(f"No ch* directories found in {doc_dir}")
        sys.exit(1)

    total = 0
    for ch_name in chapters:
        print(f"\n=== Processing {ch_name} ===")
        programs = process_chapter(doc_dir, out_dir, ch_name, cxx_std)
        total += len(programs)
        print(f"  => {len(programs)} programs extracted")

    print(f"\n{'='*40}")
    print(f"Total: {total} programs extracted across {len(chapters)} chapters")


def main():
    parser = argparse.ArgumentParser(
        description="Extract C++ programs from markdown files")
    parser.add_argument(
        "docs_dir", nargs="?",
        default=os.path.join(BASE_DIR, "documents", "vol1-fundamentals"),
        help="Source markdown directory (default: documents/vol1-fundamentals)")
    parser.add_argument(
        "output_dir", nargs="?",
        default=os.path.join(BASE_DIR, "code", "volumn_codes", "vol1"),
        help="Output directory for .cpp and CMakeLists.txt")
    parser.add_argument(
        "--cstd", type=int, default=17,
        help="C++ standard version for CMakeLists.txt (default: 17)")
    args = parser.parse_args()

    process_dir(args.docs_dir, args.output_dir, args.cstd)


if __name__ == '__main__':
    main()
