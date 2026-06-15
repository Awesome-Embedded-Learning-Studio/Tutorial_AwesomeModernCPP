---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: 'Cross-platform file system operation library: path manipulation, directory
  traversal, and file status queries'
difficulty: beginner
order: 6
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::filesystem
translation:
  engine: anthropic
  source: documents/cpp-reference/containers/06-filesystem.md
  source_hash: 31beda2f84b5927c98a29d0ae91c27a14284ad4dee010d9a06404c023839dc85
  token_count: 633
  translated_at: '2026-05-26T10:14:07.396187+00:00'
---
<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::filesystem (C++17)

## In a Nutshell

A platform-agnostic file system library: path concatenation and normalization, directory creation and traversal, file copying and deletion, permission and status queries — say goodbye to `stat()` and `opendir()`.

## Header

`#include <filesystem>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Path class | `class path` | Path construction, concatenation, and decomposition (cross-platform separator handling) |
| Path concatenation | `path operator/(const path& lhs, const path& rhs)` | `p / "subdir" / "file.txt"` |
| Current path | `path current_path()` | Get/set the working directory |
| Directory iteration | `class directory_iterator` | Iterate over a single-level directory |
| Recursive iteration | `class recursive_directory_iterator` | Recursively iterate over subdirectories |
| File status | `bool exists(const path& p)` | Check if a path exists |
| File size | `uintmax_t file_size(const path& p)` | Get the file size in bytes |
| Create directory | `bool create_directory(const path& p)` | Create a single directory |
| Create nested directories | `bool create_directories(const path& p)` | Recursively create the entire path |
| Copy file | `bool copy_file(const path& from, const path& to)` | Copy a single file |
| Remove | `bool remove(const path& p)` | Remove a file or an empty directory |
| Recursive remove | `uintmax_t remove_all(const path& p)` | Recursively remove a directory and its contents |
| Rename | `void rename(const path& old, const path& newp)` | Rename or move |

## Minimal Example

```cpp
// Standard: C++17
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path p = fs::current_path() / "test.txt";
    std::cout << p << "\n";                      // 完整路径
    std::cout << p.filename() << "\n";           // test.txt
    std::cout << p.extension() << "\n";          // .txt

    fs::create_directories("a/b/c");             // 递归创建
    std::cout << fs::exists("a/b") << "\n";      // true
    fs::remove_all("a");                         // 递归删除
}
```

## Embedded Applicability: Low

- Relies on the OS file system abstraction layer (POSIX or Win32); bare-metal environments lack a file system
- Suitable for embedded Linux (e.g., Buildroot/Yocto platforms) or host-side configuration/logging tools
- Header inclusion overhead is significant; not recommended for severely resource-constrained devices
- For embedded scenarios requiring a file system (e.g., FAT32 on SD card), consider lightweight alternatives (e.g., LittleFS)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 8 | 7 | 19.12 |

## See Also

- [Tutorial: std::filesystem](../../vol2-modern-features/ch09-filesystem/01-filesystem-path.md)
- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
