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
reading_time_minutes: 13
related:
- Lambda 基础
tags:
- host
- cpp-modern
- intermediate
title: Directory Traversal and Search
translation:
  source: documents/vol2-modern-features/ch09-filesystem/03-directory-iteration.md
  source_hash: e89e323dcd44c03550272c2e2ff158c8e1efdc1e4be5c78682025f6d6aa40c98
  translated_at: '2026-06-16T03:58:58.201793+00:00'
  engine: anthropic
  token_count: 3170
---
# Directory Traversal and Search

In the previous two articles, we learned how to handle paths using `std::filesystem::path` and manage files and directories using file operation functions. However, in actual projects, the most common requirement is "finding the files I want in a specific directory." For example: collecting all `.cpp` files to pass to the compiler, finding all texture images in a resource directory, or counting the total lines of code in a project.

C++17 provides two iterators to handle directory traversal: `directory_iterator` for single-level traversal, and `recursive_directory_iterator` for recursive traversal. In this article, we will cover everything from basic usage to performance optimization and error handling, to thoroughly master directory traversal.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Use `directory_iterator` and `recursive_directory_iterator` to traverse directories
> - [ ] Understand the caching advantages of `directory_entry`
> - [ ] Write file searchers with filtering conditions
> - [ ] Handle permission errors and other exceptions during traversal

## Environment Setup

Just like the previous two articles: C++17 standard, GCC 13+ / Clang 15+ / MSVC 2022. Header file `<filesystem>`, namespace `std::filesystem`.

## directory_iterator: Single-level Traversal

`directory_iterator` is an input iterator that traverses the **direct children** of a specified directory (it does not recursively enter subdirectories). Dereferencing it returns a `directory_entry` object, which contains the filename and basic status information.

The most basic usage is to use it directly in a range-based for loop:

```cpp
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path current_dir = ".";  // Current directory

    for (const auto& entry : fs::directory_iterator(current_dir)) {
        std::cout << entry.path().filename() << '\n';
    }

    return 0;
}
```

Possible output (truncated):

```text
main.cpp
cmake-build-debug
.git
CMakeLists.txt
README.md
```

It's that simple—a range-based for loop traverses all items in the directory and outputs the filenames. If the directory is empty, the loop body will not execute. If the directory does not exist or there is no read permission, constructing the iterator will throw a `filesystem_error` exception.

⚠️ The traversal order of `directory_iterator` is **unspecified**—it does not guarantee alphabetical order, creation time, or any specific order. If you need sorting, collect the results into a `std::vector` and then `std::sort`.

### Filtering Files

In actual projects, we are usually only interested in specific types of files. The simplest way to filter is to add a conditional judgment inside the loop body:

```cpp
for (const auto& entry : fs::directory_iterator(current_dir)) {
    if (entry.path().extension() == ".cpp") {
        std::cout << "Found C++ file: " << entry.path().filename() << '\n';
    }
}
```

If you are familiar with C++20 ranges, you can combine views for a more functional style of filtering (but that requires C++20 support). In C++17, a lambda + `std::copy_if` is a good alternative:

```cpp
std::vector<fs::path> cpp_files;
for (const auto& entry : fs::directory_iterator(current_dir)) {
    if (entry.path().extension() == ".cpp") {
        cpp_files.push_back(entry.path());
    }
}
```

## recursive_directory_iterator: Recursive Traversal

If you need to traverse all files in a directory tree (including subdirectories, subdirectories of subdirectories...), you need `recursive_directory_iterator`. It works similarly to the `find` command—starting from the initial directory, it recursively enters every subdirectory in a depth-first manner.

```cpp
int main() {
    fs::path start_dir = ".";

    for (const auto& entry : fs::recursive_directory_iterator(start_dir)) {
        std::cout << entry.path() << '\n';
    }

    return 0;
}
```

Possible output:

```text
"./main.cpp"
"./cmake-build-debug/main.o"
"./cmake-build-debug/CMakeFiles/.../main.cpp.o"
"./.git/HEAD"
...
```

### Depth Control

`recursive_directory_iterator` provides a `depth()` method, which returns the current recursion depth (starting from 0). You can use it to limit the traversal depth:

```cpp
int max_depth = 1;

for (auto it = fs::recursive_directory_iterator(start_dir); it != fs::recursive_directory_iterator(); ++it) {
    if (it.depth() > max_depth) {
        it.disable_recursion_pending();  // Prevent entering deeper directories
        continue;
    }
    std::cout << "Depth " << it.depth() << ": " << it->path() << '\n';
}
```

Output example (max_depth = 1):

```text
Depth 0: "./main.cpp"
Depth 0: "./src"
Depth 1: "./src/utils.cpp"
Depth 0: "./include"
```

⚠️ Note that `depth()` returns the depth of the current entry relative to the starting directory, not the root directory. Direct children of the starting directory have a depth of 0, children of subdirectories have a depth of 1, and so on. If you need to skip a specific subdirectory during traversal (don't want to recurse into it), you can call the iterator's `disable_recursion_pending()` method—we will show specific usage in the next article.

### directory_options: Controlling Traversal Behavior

When constructing `recursive_directory_iterator`, you can pass `directory_options` to control traversal behavior. Common options include:

`none` (default)—throws an exception when encountering a directory with denied permission.

`skip_permission_denied`—skips directories with denied permission without throwing an exception. This option is very useful in actual projects, as you often encounter system directories (like `/root`, `/System`) that do not have read permissions.

`follow_directory_symlink`—when encountering a symbolic link pointing to a directory, follow the link and recurse into it. By default, it does not follow (because it may lead to infinite loops).

```cpp
auto opts = fs::directory_options::skip_permission_denied;
for (const auto& entry : fs::recursive_directory_iterator(start_dir, opts)) {
    // ...
}
```

I strongly recommend always adding `skip_permission_denied` when traversing user file systems (especially when starting from the root or home directory). Otherwise, once a subdirectory without permissions is encountered, the entire traversal will be interrupted, and the results that have already been half-traversed will be lost.

## directory_entry: More Than Just a path

When you dereference a directory iterator, you don't get a `path` object, but a `directory_entry` object. `directory_entry` is an "enhanced version" of `path`—it not only stores the path but also caches file status information.

### The Advantage of Caching

`directory_entry` may cache file status information (type, size, etc.) to reduce the number of system calls. When you call methods like `is_directory()`, `is_regular_file()`, or `file_size()` multiple times during traversal, it can read directly from the cache, avoiding repetitive `stat` calls.

⚠️ Note: Caching behavior is **implementation-defined**; the standard does not guarantee that caching will definitely occur or when the cache will be invalidated.

```cpp
for (const auto& entry : fs::recursive_directory_iterator(start_dir)) {
    // These calls usually read from the cache, avoiding system calls
    if (entry.is_regular_file() && entry.file_size() > 1024) {
        std::cout << entry.path() << " is a large file\n";
    }
}
```

⚠️ `directory_entry`'s cache is acquired when the iterator is constructed. If a file is modified or deleted during traversal, the cache may be stale. If you need real-time status, you can call `entry.refresh()` to force a refresh, or use `fs::status(entry.path())` to get the latest status. However, this situation is rare—in most traversal scenarios, the cached data is accurate enough.

## Filtering During Traversal: By Extension, Size, Time

Let's combine our previous knowledge to write a file search function that supports multi-dimensional filtering. It can filter results based on extension, minimum file size, and maximum file size:

```cpp
#include <filesystem>
#include <vector>
#include <cstddef>

namespace fs = std::filesystem;

struct FileFilter {
    std::vector<std::string> extensions;
    std::size_t min_size = 0;
    std::size_t max_size = SIZE_MAX;
};

std::vector<fs::path> search_files(const fs::path& dir, const FileFilter& filter) {
    std::vector<fs::path> results;
    auto opts = fs::directory_options::skip_permission_denied;

    for (const auto& entry : fs::recursive_directory_iterator(dir, opts)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto ext = entry.path().extension().string();
        bool ext_match = std::find(filter.extensions.begin(), filter.extensions.end(), ext) != filter.extensions.end();

        if (!ext_match) continue;

        try {
            auto size = entry.file_size();
            if (size >= filter.min_size && size <= filter.max_size) {
                results.push_back(entry.path());
            }
        } catch (const fs::filesystem_error&) {
            // Skip files where size cannot be determined
            continue;
        }
    }

    return results;
}
```

Usage example:

```cpp
int main() {
    FileFilter filter;
    filter.extensions = {".cpp", ".h"};
    filter.min_size = 100;  // At least 100 bytes

    auto found = search_files(".", filter);
    for (const auto& p : found) {
        std::cout << "Found: " << p << '\n';
    }
    return 0;
}
```

This search function demonstrates the typical usage pattern of `recursive_directory_iterator`: add `skip_permission_denied` during construction, use the cached methods of `directory_entry` for filtering inside the loop, and finally collect the results. This "traverse + filter + collect" pattern is very common in actual projects.

## Performance Considerations

The performance of directory traversal depends on two factors: the size of the directory and the number of system calls. `directory_entry`'s caching has already helped us reduce many unnecessary `stat` calls, but there are other factors to keep in mind.

### Symbolic Link Handling

By default, `recursive_directory_iterator` does not follow symbolic links. This is the correct default behavior—following links can lead to infinite loops (A points to B, B points to A), or cause the same file to be accessed multiple times. If you确实 need to follow symbolic links, add the `follow_directory_symlink` option, but ensure there are no circular links.

### Depth Control

Recursively traversing a deeply nested directory structure can consume a significant amount of time and memory. If your goal is just a shallow search, using `depth()` to limit the recursion depth is necessary. In my tests, traversing the entire `/usr` directory tree takes about 5 seconds, but limiting the depth to 2 takes only 0.3 seconds.

### Performance Comparison with Manual Recursion

Sometimes you might see people manually write recursion to traverse directories (using `directory_iterator` to recursively call in each subdirectory). This approach usually performs worse than `recursive_directory_iterator`—because `recursive_directory_iterator` is optimized internally (such as batch reading directory entries), while manual recursion constructs a new iterator every time. So prioritize using `recursive_directory_iterator`.

## Real-world Example: Code Statistics Tool

As a conclusion to this article, let's write a practical code statistics tool. It recursively traverses a specified directory and counts the number of files and total lines for each source code type:

```cpp
#include <filesystem>
#include <iostream>
#include <fstream>
#include <map>
#include <string>

namespace fs = std::filesystem;

using LineStats = std::map<std::string, std::pair<size_t, size_t>>; // ext -> {count, lines}

void count_lines(const fs::path& dir, LineStats& stats) {
    auto opts = fs::directory_options::skip_permission_denied;

    for (const auto& entry : fs::recursive_directory_iterator(dir, opts)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        if (ext.empty()) continue;

        // Filter only source code files
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".c" && ext != ".cc") continue;

        std::ifstream file(entry.path(), std::ios::in);
        if (!file) continue;

        size_t lines = 0;
        std::string line;
        while (std::getline(file, line)) {
            lines++;
        }

        stats[ext].first++;  // Increment file count
        stats[ext].second += lines; // Add line count
    }
}

int main() {
    fs::path project_dir = ".";
    LineStats stats;

    try {
        count_lines(project_dir, stats);

        std::cout << "Extension\tFiles\tLines\n";
        std::cout << "---------\t-----\t-----\n";
        for (const auto& [ext, data] : stats) {
            std::cout << ext << "\t" << data.first << "\t" << data.second << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }

    return 0;
}
```

Possible output:

```text
Extension       Files   Lines
---------       -----   -----
.cpp            12      3450
.h              5       820
.hpp            3       450
```

This tool comprehensively uses the knowledge from this article and the previous two: `recursive_directory_iterator` for recursive traversal, `is_regular_file` for type filtering, `extension` for extension filtering, and `directory_entry`'s iterator for directory name filtering. In actual projects, you can extend it to count empty lines, comment lines, code lines, and other more fine-grained metrics.

## Summary

In this article, we learned the usage of `directory_iterator` and `recursive_directory_iterator`. `directory_iterator` performs single-level traversal and is suitable for scenarios with known directory structures. `recursive_directory_iterator` performs depth-first recursive traversal and is suitable for scenarios requiring searching the entire directory tree. The caching mechanism of `directory_entry` avoids unnecessary `stat` calls and offers significant performance advantages when traversing large directories.

Regarding error handling, always use the `skip_permission_denied` option to avoid traversal being interrupted by permission errors. Regarding performance, limit recursion depth, avoid following symbolic links, and prioritize using `recursive_directory_iterator` over manual recursion. In the practical section, we wrote a code statistics tool and a batch renaming tool, which comprehensively applied the knowledge from all three articles in this series.

At this point, we have covered the core content of the `std::filesystem` library. From the syntax handling of `path`, to file operation status queries and modifications, to directory traversal and search—this set of APIs finally gives C++ standardized file system operation capabilities, eliminating the need to rely on POSIX APIs or third-party libraries.

## Reference Resources

- [cppreference: directory_iterator](https://en.cppreference.com/w/cpp/filesystem/directory_iterator)
- [cppreference: recursive_directory_iterator](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
- [cppreference: directory_entry](https://en.cppreference.com/w/cpp/filesystem/directory_entry)
- [cppreference: directory_options](https://en.cppreference.com/w/cpp/filesystem/directory_options)
- [C++ Stories: Directory Iteration](https://www.sandordargo.com/blog/2024/03/06/std-filesystem-part2-iterate-over-directories)
