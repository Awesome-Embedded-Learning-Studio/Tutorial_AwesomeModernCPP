---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: 'Cross-platform filesystem library: path manipulation, directory traversal,
  and file status queries'
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
  translated_at: '2026-06-16T03:28:29.376268+00:00'
  engine: anthropic
  token_count: 637
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::filesystem (C++17)

## TL;DR

A platform-agnostic file system library: path concatenation and normalization, directory creation and traversal, file copying and deletion, permissions and status queries—say goodbye to `std::ifstream`/`std::ofstream` and OS APIs.

## Header

```cpp
#include <filesystem>
namespace fs = std::filesystem;
```

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Path class | `std::filesystem::path` | Path construction, concatenation, decomposition (handles cross-platform separators) |
| Path concatenation | `p / "subdir"` | Joins paths with OS-specific separator |
| Current path | `fs::current_path` | Gets/sets the working directory |
| Directory iteration | `fs::directory_iterator` | Iterates over a single-level directory |
| Recursive iteration | `fs::recursive_directory_iterator` | Recursively iterates over subdirectories |
| File status | `fs::exists` | Checks if a path exists |
| File size | `fs::file_size` | Gets file size in bytes |
| Create directory | `fs::create_directory` | Creates a single directory |
| Create multi-level directory | `fs::create_directories` | Recursively creates the entire path |
| Copy file | `fs::copy_file` | Copies a single file |
| Delete | `fs::remove` | Deletes a file or empty directory |
| Recursive delete | `fs::remove_all` | Recursively deletes a directory and its contents |
| Rename | `fs::rename` | Renames or moves a file |

## Minimal Example

```cpp
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    // Create directories
    fs::create_directories("sandbox/dir1/dir2");

    // Copy file
    fs::copy_file("source.txt", "sandbox/source.txt");

    // Iterate directory
    for (const auto& entry : fs::directory_iterator("sandbox")) {
        std::cout << entry.path() << '\n';
    }

    // Cleanup
    fs::remove_all("sandbox");
}
```

## Embedded Applicability: Low

- Depends on the OS file system abstraction layer (POSIX or Win32); bare-metal environments lack a file system.
- Suitable for Embedded Linux (e.g., Buildroot/Yocto platforms) or host-side configuration/logging tools.
- Header inclusion overhead is significant; not recommended for resource-constrained devices.
- For embedded scenarios requiring a file system (e.g., FAT32 on SD card), consider lightweight alternatives like LittleFS.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 8 | 7 | 19.12 |

## See Also

- [Tutorial: std::filesystem](../../vol2-modern-features/ch09-filesystem/01-filesystem-path.md)
- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
