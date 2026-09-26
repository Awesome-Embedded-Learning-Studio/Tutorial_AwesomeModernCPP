---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: 'A cross-platform filesystem library: path operations, directory traversal, and file status queries'
difficulty: beginner
order: 6
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::filesystem
translation:
  source: documents/cpp-reference/containers/06-filesystem.md
  source_hash: 960df19ca6d36993f7dc7087f364040828ba75522435f758c80dba5171c9183d
  translated_at: '2026-09-26T17:15:59+00:00'
  engine: anthropic
  token_count: 650
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format with no narrative style required.

Tag usage rules:
1. Must include 1 platform tag (reference cards consistently use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::filesystem (C++17)

## In a Nutshell

A platform-independent filesystem library: path concatenation and normalization, directory creation and traversal, file copying and deletion, permission and status queries — say goodbye to `stat()` and `opendir()`.

## Header

`#include <filesystem>`

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Path class | `class path` | Path construction, concatenation, and decomposition (cross-platform separator handling) |
| Path concatenation | `path operator/(const path& lhs, const path& rhs)` | `p / "subdir" / "file.txt"` |
| Current path | `path current_path()` | Gets/sets the working directory |
| Directory iteration | `class directory_iterator` | Iterates a single-level directory |
| Recursive iteration | `class recursive_directory_iterator` | Recursively iterates subdirectories |
| File status | `bool exists(const path& p)` | Checks whether a path exists |
| File size | `uintmax_t file_size(const path& p)` | Gets the file size in bytes |
| Create directory | `bool create_directory(const path& p)` | Creates a single directory |
| Create multi-level directories | `bool create_directories(const path& p)` | Recursively creates the entire path |
| Copy file | `bool copy_file(const path& from, const path& to)` | Copies a single file |
| Delete | `bool remove(const path& p)` | Deletes a file or an empty directory |
| Recursive delete | `uintmax_t remove_all(const path& p)` | Recursively deletes a directory and its contents |
| Rename | `void rename(const path& old, const path& newp)` | Renames or moves |

## Minimal Example

```cpp
// Standard: C++17
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path p = fs::current_path() / "test.txt";
    std::cout << p << "\n";                      // full path
    std::cout << p.filename() << "\n";           // test.txt
    std::cout << p.extension() << "\n";          // .txt

    fs::create_directories("a/b/c");             // recursive creation
    std::cout << fs::exists("a/b") << "\n";      // true
    fs::remove_all("a");                         // recursive deletion
}
```

## Embedded Applicability: Low

- Depends on the operating system's filesystem abstraction layer (POSIX or Win32); bare-metal environments have no filesystem
- Suitable for embedded Linux (e.g., Buildroot/Yocto platforms) or host-side configuration/logging tools
- The header carries considerable inclusion overhead; not recommended for extremely resource-constrained devices
- For embedded scenarios that do need a filesystem (e.g., FAT32 on an SD card), consider a lightweight alternative such as LittleFS

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 8 | 7 | 19.12 |

## See Also

- [Tutorial: std::filesystem](../../vol2-modern-features/ch09-filesystem/01-filesystem-path.md)
- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)

---

*Some content references [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
