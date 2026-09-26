---
chapter: 9
cpp_standard:
- 17
description: Usage and performance of directory_iterator and recursive_directory_iterator
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 9: Path Operations: Cross-Platform Path Handling'
- 'Chapter 9: File and Directory Operations'
reading_time_minutes: 13
related:
- 'Lambda Basics: The Elegant Expression of Anonymous Functions'
tags:
- host
- cpp-modern
- intermediate
title: Directory Traversal and Search
translation:
  source: documents/vol2-modern-features/ch09-filesystem/03-directory-iteration.md
  source_hash: c693225e24648ad0f731b0f7c9d3dfd2f980b93405825049c04b704163642113
  translated_at: '2026-09-25T16:30:30+00:00'
  engine: anthropic
  token_count: 3200
---
# Directory Traversal and Search: Walking the Directory Tree Recursively

In the previous two articles we learned to handle paths with `path` and to manage files and directories with the file operation functions. In real projects, though, the most common need is actually "find the files I want under this directory." For example: collect all `.cpp` files and hand them to the compiler, find every texture image in an assets directory, or count the total lines of code in a project.

C++17 provides two iterators for directory traversal: `directory_iterator` for single-level traversal, and `recursive_directory_iterator` for recursive traversal. In this article we go from basic usage through performance optimization to error handling, until directory traversal holds no more secrets.

As in the previous two articles: C++17, GCC 13+ / Clang 15+ / MSVC 2022. Header `<filesystem>`, namespace `namespace fs = std::filesystem;`.

## directory_iterator: Single-Level Traversal

`fs::directory_iterator` is an input iterator that walks the **direct children** of a given directory (it does not recurse into subdirectories). Each dereference returns an `fs::directory_entry` object, which carries the filename and basic status information.

The most basic usage is to drop it straight into a range-based for loop:

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

Possible output (excerpt):

```text
gcc
g++
cmake
python3/
pip
```

It is that simple — one range-based for loop walks every entry in the directory and prints the filename. If the directory is empty, the loop body never executes. If the directory does not exist or you lack read permission, constructing the iterator throws a `filesystem_error` exception.

The order in which `directory_iterator` visits entries is **unspecified** — no alphabetical order guaranteed, no creation-time order guaranteed, no particular order of any kind guaranteed. If you need a specific order, collect the results into a `vector` and run `std::sort`.

### Filtering Files

In real projects we usually care only about files of certain types. The simplest way to filter is to add a condition inside the loop body:

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

If you are familiar with C++20 ranges, you can build a more functional style of filtering with views (but that requires C++20 support). In C++17, lambda + `std::copy_if` is a decent alternative:

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

When you need to walk every file in a directory tree (subdirectories, subdirectories of subdirectories, ...), you need `fs::recursive_directory_iterator`. It works much like the `find` command — starting from the initial directory, it recurses into every subdirectory, depth-first.

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

Here is the visit order of the two iterators marked on the same directory tree:

![Visit order of single-level vs. recursive traversal](./03-iteration-order.drawio)

### Depth Control

`recursive_directory_iterator` provides a `depth()` method that returns the current recursion depth (starting from 0). You can use it to limit the traversal depth:

```cpp
void list_with_depth_limit(const fs::path& dir, int max_depth) {
    for (auto it = fs::recursive_directory_iterator(dir);
         it != fs::recursive_directory_iterator(); ++it) {
        if (it.depth() > max_depth) {
            it.disable_recursion_pending();  // skip this subdirectory
            continue;
        }
        std::cout << std::string(it.depth() * 2, ' ')
                  << it->path().filename().string() << "\n";
    }
}
```

Sample output (max_depth = 1):

```text
src/
  main.cpp
  utils/
CMakeLists.txt
```

Note that `depth()` returns the current entry's depth relative to the starting directory, not to the filesystem root. Direct children of the starting directory are at depth 0, entries inside those subdirectories are at depth 1, and so on. If, during traversal, you want to skip a particular subdirectory (not recurse into it), call the iterator's `disable_recursion_pending()` method — we will show concrete uses in the next article.

### directory_options: Controlling Traversal Behavior

When constructing a `recursive_directory_iterator`, you can pass in `directory_options` to control traversal behavior. The commonly used options are:

`fs::directory_options::none` (the default) — throws an exception when it hits a directory that denies permission.

`fs::directory_options::skip_permission_denied` — skips directories that deny permission instead of throwing. This option is extremely useful in real projects, because you constantly run into system directories (such as `/proc` and `/sys`) that you have no read permission for.

`fs::directory_options::follow_directory_symlink` — when it encounters a symbolic link pointing to a directory, it follows the link and recurses into it. The default is not to follow (because that can lead to infinite loops).

```cpp
// Safe recursive traversal: skip directories we lack permission for
for (const auto& entry : fs::recursive_directory_iterator(
         dir, fs::directory_options::skip_permission_denied)) {
    // process entry...
}
```

We strongly recommend always adding `skip_permission_denied` when traversing a user filesystem (especially when starting from the root directory or the home directory). Otherwise, the moment you hit one subdirectory you cannot access, the whole traversal aborts — and the half-finished results you already collected are lost too.

## directory_entry: More Than Just a path

Each time you dereference a directory iterator, what you get is not a `path` object but a `directory_entry` object. `directory_entry` is a `path` with upgrades — it stores the path and also caches file status information.

### The Advantage of Caching

A `directory_entry` may cache file status information (type, size, and so on) to cut down on the number of system calls. When you call `is_regular_file()`, `is_directory()`, `file_size()`, and similar methods repeatedly during traversal, they can read straight from the cache and avoid duplicate `stat()` calls. Note: caching behavior is **implementation-defined** — the standard guarantees neither that anything is cached nor when a cached entry goes stale.

```cpp
for (const auto& entry : fs::directory_iterator(dir)) {
    // These calls use cached values and trigger no extra system calls
    auto name = entry.path().filename().string();
    auto is_file = entry.is_regular_file();
    auto is_dir = entry.is_directory();
    auto size = entry.file_size();  // only valid for regular files

    std::cout << name << " "
              << (is_file ? "file" : "dir")
              << " " << size << "\n";
}
```

A `directory_entry`'s cache is populated when the iterator is constructed. If a file is modified or deleted during traversal, the cache may already be stale. If you need the live status, call `entry.refresh()` to force a refresh, or query the latest state directly with `fs::status(entry.path())`. In practice this is rare — for most traversal scenarios the cached data is accurate enough.

## Filtering While Traversing: By Extension, Size, and Time

Let's combine what we covered above into a file-search function that supports multi-dimensional filtering. It can filter results by extension, minimum file size, and maximum file size:

```cpp
#include <filesystem>
#include <vector>
#include <algorithm>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;

struct SearchFilter {
    std::string extension;                // target extension; empty means no filtering
    std::uintmax_t min_size = 0;          // minimum file size
    std::uintmax_t max_size = UINTMAX_MAX; // maximum file size
    int max_depth = -1;                   // maximum recursion depth; -1 means unlimited
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

        // depth filtering
        if (filter.max_depth >= 0 &&
            it.depth() > filter.max_depth) {
            it.disable_recursion_pending();
            continue;
        }

        const auto& entry = *it;

        // only process regular files
        if (!entry.is_regular_file()) {
            continue;
        }

        // extension filtering
        if (!filter.extension.empty()) {
            if (entry.path().extension() != filter.extension) {
                continue;
            }
        }

        // file size filtering
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
    filter.min_size = 100;      // at least 100 bytes
    filter.max_size = 1000000;  // at most 1MB

    auto files = search_files("/home/user/project", filter);
    std::cout << "找到 " << files.size() << " 个文件:\n";
    for (const auto& f : files) {
        std::cout << "  " << f << "\n";
    }
    return 0;
}
```

This search function demonstrates the typical usage pattern of `recursive_directory_iterator`: add `skip_permission_denied` at construction, filter inside the loop body with `directory_entry`'s cached methods, and collect the results at the end. This "traverse + filter + collect" pattern is extremely common in real projects.

## Performance Considerations

The performance of directory traversal depends on two factors: the size of the directory and the number of system calls. `directory_entry`'s caching already saves us many unnecessary `stat()` calls, but there are a few other factors to watch.

### Symlink Handling

By default, `recursive_directory_iterator` does not follow symbolic links. This is the correct default behavior — following links can lead to infinite loops (A points to B, B points to A), and it can also cause the same file to be visited multiple times. If you really do need to follow symbolic links, add the `follow_directory_symlink` option, but make absolutely sure there are no cyclic links.

### Depth Control

Recursively traversing a deeply nested directory structure can consume a lot of time and memory. If your goal is only a shallow search, limiting the recursion depth with `depth()` is well worth it. In our tests, traversing the entire `/usr` directory tree took about 5 seconds, but with the depth limited to 2 it took only 0.3 seconds.

### Performance Comparison with Manual Recursion

Sometimes you will see people hand-write recursion to traverse directories (recursively calling `directory_iterator` inside every subdirectory). This approach usually performs worse than `recursive_directory_iterator` — because `recursive_directory_iterator` applies internal optimizations (such as reading directory entries in batches), while manual recursion has to construct a new iterator every time. So prefer `recursive_directory_iterator`.

## In Practice: A Code Statistics Tool

To wrap up this article, let's write a practical code statistics tool. It recursively traverses a given directory and tallies, for each kind of source code file, the file count and the total line count:

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

/// @brief Count the lines of a single file
/// @param path File path
/// @return Line count (0 on failure)
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

/// @brief Gather code-file statistics under a directory
/// @param root Root directory
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
        // only count common source code files
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp" &&
            ext != ".c" && ext != ".py" && ext != ".java" &&
            ext != ".rs" && ext != ".go") {
            continue;
        }

        // skip hidden directories and build directories
        bool skip = false;
        for (const auto& component : entry.path()) {
            auto s = component.string();
            if (s == ".git" || s == "build" || s == "cmake-build-*"
                || (s.size() > 1 && s[0] == '.')) {
                // simple skip logic
            }
        }
        // a complete version should handle this with disable_recursion_pending()
        // simplified here

        auto lines = count_lines(entry.path());
        stats[ext].file_count++;
        stats[ext].total_lines += lines;
    }

    // print the results
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

This tool combines everything from this article and the previous two: `recursive_directory_iterator` for recursive traversal, `directory_entry::is_regular_file()` for type filtering, `path::extension()` for extension filtering, and `path`'s iterator for directory-name filtering. In a real project, you can extend it to finer-grained metrics such as counts of blank lines, comment lines, and code lines.

## References

- [cppreference: directory_iterator](https://en.cppreference.com/w/cpp/filesystem/directory_iterator)
- [cppreference: recursive_directory_iterator](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
- [cppreference: directory_entry](https://en.cppreference.com/w/cpp/filesystem/directory_entry)
- [cppreference: directory_options](https://en.cppreference.com/w/cpp/filesystem/directory_options)
- [C++ Stories: Directory Iteration](https://www.sandordargo.com/blog/2024/03/06/std-filesystem-part2-iterate-over-directories)
