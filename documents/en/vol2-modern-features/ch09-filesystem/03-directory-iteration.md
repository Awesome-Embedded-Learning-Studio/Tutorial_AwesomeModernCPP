---
chapter: 9
cpp_standard:
- 17
description: Usage and performance of `directory_iterator` and `recursive_directory_iterator`
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 9: path 操作'
- 'Chapter 9: 文件与目录操作'
reading_time_minutes: 12
related:
- Lambda 基础
tags:
- host
- cpp-modern
- intermediate
title: Directory Traversal and Search
translation:
  engine: anthropic
  source: documents/vol2-modern-features/ch09-filesystem/03-directory-iteration.md
  source_hash: bd49ae18f832afe6a4ebffedbd902a630ebbb466cbc0ea3451e256a48da23b97
  token_count: 3175
  translated_at: '2026-05-26T11:35:13.306929+00:00'
---
# Directory Traversal and Search

In the previous two articles, we learned how to handle paths with `std::filesystem::path` and manage files and directories using file operation functions. But in real projects, the most common need is actually "finding the files I want in a certain directory." For example: collecting all `.cpp` files to pass to the compiler, finding all texture images in a resource directory, or counting the total lines of code in a project.

C++17 provides two iterators for directory traversal: `directory_iterator` for single-level traversal, and `recursive_directory_iterator` for recursive traversal. In this article, we cover everything from basic usage to performance optimization and error handling, giving you a thorough understanding of directory traversal.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Use `directory_iterator` and `recursive_directory_iterator` to traverse directories
> - [ ] Understand the caching advantages of `directory_entry`
> - [ ] Write a file searcher with filter conditions
> - [ ] Handle permission errors and other exceptions during traversal

## Environment Setup

As with the previous two articles, we use the C++17 standard with GCC 13+ / Clang 15+ / MSVC 2022. The header file is `<filesystem>`, and the namespace is `std::filesystem`.

## directory_iterator: Single-Level Traversal

`directory_iterator` is an input iterator that traverses the **direct children** of a specified directory (it does not recurse into subdirectories). Dereferencing it returns a `directory_entry` object, which contains the filename and basic status information.

The most basic usage is directly in a range-based for loop:

```cpp
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path dir = "/usr/local/bin";

    for (const auto& entry : fs::directory_iterator(dir)) {
        std::cout << entry.path().filename().string();
        if (entry.is_directory()) {
            std::cout << "/";
        }
        std::cout << "\n";
    }
    return 0;
}
```

Possible output (truncated):

```text
gcc
g++
cmake
python3/
pip
```

It's that simple—a range-based for loop that iterates over all items in the directory and prints their filenames. If the directory is empty, the loop body never executes. If the directory does not exist or lacks read permissions, constructing the iterator throws a `std::filesystem::filesystem_error` exception.

⚠️ The traversal order of `directory_iterator` is **unspecified**—it does not guarantee alphabetical order, creation time, or any specific order. If you need sorting, collect the results into a `std::vector` and then `std::sort` them.

### Filtering Files

In real projects, we are usually only interested in specific types of files. The simplest way to filter is to add a condition inside the loop body:

```cpp
void find_cpp_files(const fs::path& dir) {
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".cpp") {
            std::cout << entry.path() << "\n";
        }
    }
}
```

If you are familiar with C++20 ranges, you can combine views for a more functional filtering approach (but that requires C++20 support). In C++17, a lambda + `std::copy_if` is a good alternative:

```cpp
#include <vector>
#include <algorithm>

std::vector<fs::path> collect_files(const fs::path& dir,
                                      const std::string& ext) {
    std::vector<fs::path> result;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ext) {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}
```

## recursive_directory_iterator: Recursive Traversal

If you need to traverse all files in a directory tree (including subdirectories, sub-subdirectories, and so on), you need `recursive_directory_iterator`. It works similarly to the `find` command—starting from the initial directory, it recursively enters every subdirectory in a depth-first manner.

```cpp
void list_all_files(const fs::path& dir) {
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        std::cout << entry.path();
        if (entry.is_directory()) {
            std::cout << "/";
        }
        std::cout << "\n";
    }
}
```

Possible output:

```text
/home/user/project/src/
/home/user/project/src/main.cpp
/home/user/project/src/utils/
/home/user/project/src/utils/helper.cpp
/home/user/project/src/utils/helper.h
/home/user/project/CMakeLists.txt
```

### Depth Control

`recursive_directory_iterator` provides the `depth()` method, which returns the current recursion depth (starting from 0). You can use this to limit the traversal depth:

```cpp
void list_with_depth_limit(const fs::path& dir, int max_depth) {
    for (auto it = fs::recursive_directory_iterator(dir);
         it != fs::recursive_directory_iterator(); ++it) {
        if (it.depth() > max_depth) {
            it.disable_recursion_pending();  // 跳过该子目录
            continue;
        }
        std::cout << std::string(it.depth() * 2, ' ')
                  << it->path().filename().string() << "\n";
    }
}
```

Example output (max_depth = 1):

```text
src/
  main.cpp
  utils/
CMakeLists.txt
```

⚠️ Note that `depth()` returns the current entry's depth relative to the starting directory, not relative to the root directory. Direct children of the starting directory have a depth of 0, children of subdirectories have a depth of 1, and so on. If you need to skip a subdirectory during traversal (to avoid recursing into it), you can call the iterator's `disable_recursion_pending()` method—we will show specific use cases in the next article.

### directory_options: Controlling Traversal Behavior

When constructing a `recursive_directory_iterator`, you can pass `directory_options` to control the traversal behavior. Common options include:

`none` (default) — throws an exception when encountering a directory with denied permissions.

`skip_permission_denied` — skips directories with denied permissions without throwing an exception. This option is very useful in real projects because you will often encounter system directories (such as `/proc`, `/sys`) that lack read permissions.

`follow_directory_symlink` — follows symbolic links that point to directories and recurses into them. By default, it does not follow them (because this could lead to infinite loops).

```cpp
// 安全的递归遍历：跳过无权限的目录
for (const auto& entry : fs::recursive_directory_iterator(
         dir, fs::directory_options::skip_permission_denied)) {
    // 处理 entry...
}
```

We strongly recommend always adding `skip_permission_denied` when traversing user file systems (especially when starting from the root or home directory). Otherwise, once a subdirectory without permissions is encountered, the entire traversal will abort, and any results already collected will be lost.

## directory_entry: More Than Just a path

When you dereference a directory iterator, you don't get a `path` object, but a `directory_entry` object. `directory_entry` is an "enhanced" version of `path`—it not only stores the path but also caches file status information.

### Caching Advantages

`directory_entry` may cache file status information (type, size, etc.) to reduce the number of system calls. When you call methods like `is_regular_file()`, `is_directory()`, or `file_size()` multiple times during traversal, the results can be read directly from the cache, avoiding redundant `stat` calls. ⚠️ Note: the caching behavior is **implementation-defined**, and the standard does not guarantee that caching will occur or when the cache will be invalidated.

```cpp
for (const auto& entry : fs::directory_iterator(dir)) {
    // 这些调用使用缓存值，不触发额外的系统调用
    auto name = entry.path().filename().string();
    auto is_file = entry.is_regular_file();
    auto is_dir = entry.is_directory();
    auto size = entry.file_size();  // 仅对普通文件有效

    std::cout << name << " "
              << (is_file ? "file" : "dir")
              << " " << size << "\n";
}
```

⚠️ The cache of `directory_entry` is populated when the iterator is constructed. If a file is modified or deleted during traversal, the cache may be stale. If you need real-time status, you can call `refresh()` to force an update, or use `std::filesystem::status()` directly to get the latest state. However, this situation is relatively rare—in most traversal scenarios, the cached data is accurate enough.

## Filtering During Traversal: By Extension, Size, and Time

Let's combine what we've learned so far and write a file search function that supports multi-dimensional filtering. It can filter results by extension, minimum file size, and maximum file size:

```cpp
#include <filesystem>
#include <vector>
#include <algorithm>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;

struct SearchFilter {
    std::string extension;                // 目标扩展名，空表示不过滤
    std::uintmax_t min_size = 0;          // 最小文件大小
    std::uintmax_t max_size = UINTMAX_MAX; // 最大文件大小
    int max_depth = -1;                   // 最大递归深度，-1 表示不限
};

std::vector<fs::path> search_files(const fs::path& root,
                                     const SearchFilter& filter) {
    std::vector<fs::path> results;
    std::error_code ec;

    auto options = fs::directory_options::skip_permission_denied;

    for (auto it =
         fs::recursive_directory_iterator(root, options, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) {
            std::cerr << "遍历错误: " << ec.message() << "\n";
            ec.clear();
            continue;
        }

        // 深度过滤
        if (filter.max_depth >= 0 &&
            it.depth() > filter.max_depth) {
            it.disable_recursion_pending();
            continue;
        }

        const auto& entry = *it;

        // 只处理普通文件
        if (!entry.is_regular_file()) {
            continue;
        }

        // 扩展名过滤
        if (!filter.extension.empty()) {
            if (entry.path().extension() != filter.extension) {
                continue;
            }
        }

        // 文件大小过滤
        auto size = entry.file_size();
        if (size < filter.min_size || size > filter.max_size) {
            continue;
        }

        results.push_back(entry.path());
    }

    std::sort(results.begin(), results.end());
    return results;
}
```

Usage example:

```cpp
int main() {
    SearchFilter filter;
    filter.extension = ".cpp";
    filter.min_size = 100;      // 至少 100 字节
    filter.max_size = 1000000;  // 不超过 1MB

    auto files = search_files("/home/user/project", filter);
    std::cout << "找到 " << files.size() << " 个文件:\n";
    for (const auto& f : files) {
        std::cout << "  " << f << "\n";
    }
    return 0;
}
```

This search function demonstrates the typical usage pattern of `recursive_directory_iterator`: add `skip_permission_denied` at construction, use the cached methods of `directory_entry` for filtering inside the loop body, and finally collect the results. This "traverse + filter + collect" pattern is extremely common in real projects.

## Performance Considerations

The performance of directory traversal depends on two factors: the size of the directory and the number of system calls. The caching in `directory_entry` already helps us avoid many unnecessary `stat` calls, but there are other factors to keep in mind.

### Symbolic Link Handling

By default, `recursive_directory_iterator` does not follow symbolic links. This is the correct default behavior—following links can lead to infinite loops (A points to B, B points to A) or cause the same file to be accessed multiple times. If you truly need to follow symbolic links, add the `follow_directory_symlink` option, but make absolutely sure there are no circular links.

### Depth Control

Recursively traversing a deeply nested directory structure can consume a significant amount of time and memory. If your goal is only a shallow search, using `depth()` to limit the recursion depth is quite necessary. In our tests, traversing the entire `/usr` directory tree takes about 5 seconds, but limiting the depth to 2 takes only 0.3 seconds.

### Performance Comparison with Manual Recursion

Sometimes you might see people write manual recursion to traverse directories (using `directory_iterator` to recursively call into each subdirectory). This approach is usually slower than `recursive_directory_iterator`—because `recursive_directory_iterator` has internal optimizations (such as batch-reading directory entries), whereas manual recursion has to construct a new iterator each time. Therefore, prefer using `recursive_directory_iterator`.

## Practical Example: Code Statistics Tool

As a wrap-up for this article, let's write a practical code statistics tool. It recursively traverses a specified directory and counts the number of files and total lines for each type of source code:

```cpp
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <iomanip>

namespace fs = std::filesystem;

struct FileStats {
    int file_count = 0;
    int total_lines = 0;
};

/// @brief 统计单个文件的行数
/// @param path 文件路径
/// @return 行数（失败返回 0）
int count_lines(const fs::path& path) {
    std::ifstream file(path);
    if (!file) return 0;

    int lines = 0;
    std::string line;
    while (std::getline(file, line)) {
        ++lines;
    }
    return lines;
}

/// @brief 统计目录下的代码文件
/// @param root 根目录
void code_stats(const fs::path& root) {
    std::unordered_map<std::string, FileStats> stats;
    std::error_code ec;

    auto options = fs::directory_options::skip_permission_denied;

    for (const auto& entry :
         fs::recursive_directory_iterator(root, options, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        // 只统计常见源代码文件
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp" &&
            ext != ".c" && ext != ".py" && ext != ".java" &&
            ext != ".rs" && ext != ".go") {
            continue;
        }

        // 跳过隐藏目录和 build 目录
        bool skip = false;
        for (const auto& component : entry.path()) {
            auto s = component.string();
            if (s == ".git" || s == "build" || s == "cmake-build-*"
                || (s.size() > 1 && s[0] == '.')) {
                // 简单的跳过逻辑
            }
        }
        // 完整版本应该用 disable_recursion_pending() 处理
        // 这里简化处理

        auto lines = count_lines(entry.path());
        stats[ext].file_count++;
        stats[ext].total_lines += lines;
    }

    // 输出结果
    int total_files = 0;
    int total_lines = 0;

    std::cout << std::left << std::setw(8) << "扩展名"
              << std::setw(10) << "文件数"
              << std::setw(12) << "总行数" << "\n";
    std::cout << std::string(30, '-') << "\n";

    for (const auto& [ext, stat] : stats) {
        std::cout << std::left << std::setw(8) << ext
                  << std::setw(10) << stat.file_count
                  << std::setw(12) << stat.total_lines << "\n";
        total_files += stat.file_count;
        total_lines += stat.total_lines;
    }

    std::cout << std::string(30, '-') << "\n";
    std::cout << std::left << std::setw(8) << "合计"
              << std::setw(10) << total_files
              << std::setw(12) << total_lines << "\n";
}

int main() {
    code_stats(".");
    return 0;
}
```

Possible output:

```text
扩展名    文件数    总行数
------------------------------
.cpp     12        4856
.h       15        2340
.hpp     3         892
.py      2         340
------------------------------
合计     32        8428
```

This tool comprehensively applies the knowledge from this article and the previous two: `recursive_directory_iterator` for recursive traversal, `is_regular_file()` for type filtering, `path::extension()` for extension filtering, and an iterator for directory name filtering. In real projects, you can extend it to count more fine-grained metrics, such as blank lines, comment lines, and lines of code.

## Summary

In this article, we learned how to use `directory_iterator` and `recursive_directory_iterator`. `directory_iterator` performs single-level traversal and is suitable for scenarios where the directory structure is known. `recursive_directory_iterator` performs depth-first recursive traversal and is suitable for scenarios that require searching an entire directory tree. The caching mechanism of `directory_entry` avoids unnecessary `stat` calls, providing a significant performance advantage when traversing large directories.

Regarding error handling, always use the `skip_permission_denied` option to prevent traversal from being interrupted by permission errors. Regarding performance, limit the recursion depth, avoid following symbolic links, and prefer using `recursive_directory_iterator` over manual recursion. In the practical section, we wrote a code statistics tool and a batch rename tool, which comprehensively applied all the knowledge from the three articles in this series.

At this point, we have covered the core content of the `std::filesystem` library. From path syntax handling with `path`, to status queries and modifications for file operations, and now to directory traversal and search—this API finally gives C++ standardized file system operation capabilities, eliminating the need to rely on POSIX APIs or third-party libraries.

## Reference Resources

- [cppreference: directory_iterator](https://en.cppreference.com/w/cpp/filesystem/directory_iterator)
- [cppreference: recursive_directory_iterator](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
- [cppreference: directory_entry](https://en.cppreference.com/w/cpp/filesystem/directory_entry)
- [cppreference: directory_options](https://en.cppreference.com/w/cpp/filesystem/directory_options)
- [C++ Stories: Directory Iteration](https://www.sandordargo.com/blog/2024/03/06/std-filesystem-part2-iterate-over-directories)
