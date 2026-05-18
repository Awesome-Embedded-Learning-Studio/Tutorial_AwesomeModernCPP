---
title: "std::filesystem"
description: "Cross-platform file system operations: path manipulation, directory traversal, file status queries"
chapter: 99
order: 6
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [17, 20, 23]
---

<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::filesystem (C++17)

## In a Nutshell

Platform-independent file system library: path concatenation and normalization, directory creation and traversal, file copy and deletion, permission and status queries -- say goodbye to `stat()` and `opendir()`.

## Header

`#include <filesystem>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Path class | `class path` | Path construction, concatenation, decomposition (cross-platform separator handling) |
| Path concatenation | `path operator/(const path& lhs, const path& rhs)` | `p / "subdir" / "file.txt"` |
| Current path | `path current_path()` | Get/set working directory |
| Directory iteration | `class directory_iterator` | Iterate a single directory level |
| Recursive iteration | `class recursive_directory_iterator` | Recursively iterate subdirectories |
| File status | `bool exists(const path& p)` | Check whether a path exists |
| File size | `uintmax_t file_size(const path& p)` | Get file size in bytes |
| Create directory | `bool create_directory(const path& p)` | Create a single directory |
| Create directories | `bool create_directories(const path& p)` | Recursively create the entire path |
| Copy file | `bool copy_file(const path& from, const path& to)` | Copy a single file |
| Remove | `bool remove(const path& p)` | Remove a file or empty directory |
| Remove all | `uintmax_t remove_all(const path& p)` | Recursively remove a directory and its contents |
| Rename | `void rename(const path& old, const path& newp)` | Rename or move |

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
    fs::remove_all("a");                         // recursive removal
}
```

## Embedded Applicability: Low

- Depends on OS file system abstraction layers (POSIX or Win32); bare-metal environments have no file system
- Suitable for embedded Linux (e.g., Buildroot/Yocto platforms) or host-side configuration/logging tools
- Header inclusion overhead is significant; not recommended for extremely resource-constrained devices
- For embedded scenarios that require a file system (e.g., FAT32 on SD card), consider lightweight alternatives such as LittleFS

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 8 | 7 | 19.12 |

## See Also

- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
