---
chapter: 9
cpp_standard:
- 17
description: exists, copy, move, remove, permission, and space queries
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 9: Path Operations: Cross-Platform Path Handling'
reading_time_minutes: 16
related:
- Directory Traversal and Search
tags:
- host
- cpp-modern
- intermediate
title: File and Directory Operations
translation:
  source: documents/vol2-modern-features/ch09-filesystem/02-filesystem-ops.md
  source_hash: 9362ed2535b4ff9ef74aa8b9954f86f0d1d782480f8f68dbd3ee0ddb134f2bdd
  translated_at: '2026-09-25T16:16:36+00:00'
  engine: anthropic
  token_count: 7800
---
# File and Directory Operations: This Time We Actually Touch the Disk

In the previous article we learned to handle path syntax with `std::filesystem::path` — construction, decomposition, modification, comparison: all pure computation, no disk involved. This time we get serious: we use the `<filesystem>` library to operate on the file system directly — checking whether a file exists, creating directories, copying files, deleting files, querying permissions and disk space.

As in the previous article, our environment is C++17, GCC 13+ / Clang 15+ / MSVC 2022. The header is `<filesystem>`, with `namespace fs = std::filesystem;`.

First, let's lay out the operations this article spends the most time on against a single /tmp directory tree, so you can see which node each call lands on — and what the tree gains or loses once it runs:

![Where exists, copy, create, and remove land on a /tmp directory tree](./02-dir-tree-ops.drawio)

## File Status Queries: Does It Exist, and What Type Is It

The first step in file system work is usually "let's see what is actually at this path". `<filesystem>` provides a set of query functions to answer that question.

### exists: Does the Path Exist

`fs::exists(p)` checks whether the given path exists on the file system. It accepts a `path` object, or a `directory_entry` (which we cover in the next article). It returns a `bool`:

```cpp
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path p = "/usr/local/bin/gcc";
    if (fs::exists(p)) {
        std::cout << p << " 存在\n";
    } else {
        std::cout << p << " 不存在\n";
    }
    return 0;
}
```

`exists()` throws in some situations (for example, when insufficient permissions prevent access to the parent directory). If you don't want the exception to propagate, use the overload that does not take a `std::error_code`, or wrap the call in a try-catch. The better approach is the overload that takes a `std::error_code`:

```cpp
std::error_code ec;
bool exists = fs::exists(p, ec);
if (ec) {
    std::cerr << "查询失败: " << ec.message() << "\n";
}
```

### is_regular_file / is_directory / is_symlink: Type Checks

Once you know a path exists, the next step is determining its type. `fs::is_regular_file(p)` checks whether it is a regular file, `fs::is_directory(p)` whether it is a directory, and `fs::is_symlink(p)` whether it is a symbolic link. There are also finer-grained checks such as `is_block_file`, `is_character_file`, `is_fifo`, `is_socket`, and `is_other`, which come up occasionally in Linux systems programming.

```cpp
fs::path p = "/usr/local/bin";

if (fs::is_directory(p)) {
    std::cout << p << " 是一个目录\n";
} else if (fs::is_regular_file(p)) {
    std::cout << p << " 是一个普通文件\n";
} else if (fs::is_symlink(p)) {
    std::cout << p << " 是一个符号链接\n";
}
```

If the path does not exist, these functions return `false` — no exception. So you don't need to call `exists()` before checking the type; just check directly. But note: if the underlying `status()` call itself fails (say, due to permission problems), it throws a `filesystem_error`.

### file_size / last_write_time / status: Metadata Queries

Beyond the type, we often also need a file's size, last modification time, and permission status:

```cpp
#include <filesystem>
#include <iostream>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

void print_file_info(const fs::path& p) {
    std::error_code ec;

    // File size (bytes)
    auto size = fs::file_size(p, ec);
    if (!ec) {
        std::cout << "大小: " << size << " 字节\n";
        if (size > 1024 * 1024) {
            std::cout << "      "
                      << size / (1024.0 * 1024.0) << " MB\n";
        } else if (size > 1024) {
            std::cout << "      "
                      << size / 1024.0 << " KB\n";
        }
    }

    // Last modification time
    auto ftime = fs::last_write_time(p, ec);
    if (!ec) {
        // Before C++20: we must convert to time_t for display
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
        std::cout << "修改时间: "
                  << std::ctime(&time_t_val);
    }

    // File status (permissions etc.)
    auto status = fs::status(p, ec);
    if (!ec) {
        std::cout << "类型: " << static_cast<int>(status.type()) << "\n";
        std::cout << "权限: " << static_cast<unsigned>(status.permissions()) << "\n";
    }
}

int main() {
    print_file_info("/usr/local/bin/gcc");
    return 0;
}
```

Before C++20, converting `last_write_time` to a readable format is somewhat tedious (as shown above), because `file_time_type`'s clock is not necessarily `system_clock`. C++20 offers a cleaner route via `std::chrono::clock_cast`, but C++17 is stuck with the approximation above. In real projects, `std::ctime` is good enough for simple display — just don't expect the result to be perfectly precise.

## Creating Directories

`fs::create_directory(p)` creates a directory — on the condition that the parent directory already exists. If the parent does not exist, the call fails:

```cpp
fs::path dir = "/tmp/myapp_config";
if (!fs::exists(dir)) {
    if (fs::create_directory(dir)) {
        std::cout << "目录创建成功\n";
    } else {
        std::cerr << "目录创建失败\n";
    }
}
```

If you need to create a multi-level directory (say `/tmp/a/b/c`, where neither `/tmp/a` nor `/tmp/a/b` exists), use `fs::create_directories(p)`. It automatically creates every missing intermediate directory in the path, similar to `mkdir -p`:

```cpp
fs::path deep_dir = "/tmp/myapp/data/cache/tmp";
fs::create_directories(deep_dir);  // Creates all intermediate directories automatically
std::cout << "创建完成\n";
```

`create_directories` is one of the file system operations I use the most. At program startup, making sure the config, log, and cache directories all exist is a very common requirement — `create_directories` settles it in one line, with no manual per-level checks.

`create_directory` returns `false` when the directory already exists, but that is not an error. Same for `create_directories` — if every directory already exists, it also returns `false`. So don't use the return value to decide "did it fail"; use the `std::error_code` overloads instead.

## Copying Files and Directories

`fs::copy(from, to)` is a multi-purpose copy function. Its behavior depends on the type of `from` and on whether `copy_options` are given:

```cpp
// Default behavior:
// - If from is a regular file, copy the file to to
// - If from is a directory, copy the directory structure to to (contents are not copied recursively)
// - If from is a symbolic link, copy the link itself

fs::path src = "/tmp/source.txt";
fs::path dst = "/tmp/dest.txt";

std::error_code ec;
fs::copy(src, dst, ec);
if (ec) {
    std::cerr << "复制失败: " << ec.message() << "\n";
}
```

### copy_options: Controlling Copy Behavior

`copy_options` is a bitmask type for fine-grained control over how copying behaves. Commonly used options include:

`fs::copy_options::overwrite_existing` — if the destination file already exists, overwrite it. By default, when the destination exists, `copy` fails (or skips, depending on the specific operation).

`fs::copy_options::recursive` — recursively copy directory contents. If `from` is a directory, every file and subdirectory under it gets copied.

`fs::copy_options::copy_symlinks` — copy the symbolic link itself (rather than following the link and copying the file it points to).

```cpp
// Recursively copy an entire directory
fs::copy("/tmp/source_dir", "/tmp/dest_dir",
         fs::copy_options::recursive |
         fs::copy_options::overwrite_existing);
```

`fs::copy_file(from, to, options)` is a function dedicated to copying files. The difference from `copy`: `copy_file` only handles regular files, and it offers finer control. Note: `copy_file` **provides no atomicity guarantee** — if the copy fails partway through (out of disk space, power loss, and so on), the destination may be left partially written. If you need atomicity, use the "copy to a temporary file + atomic rename" pattern. (See the `safe_write_file` example in the "Temporary File Handling" section.)

```cpp
// Unsafe file copy (no atomicity guarantee)
fs::path src = "/data/important_config.yaml";
fs::path dst = "/backup/important_config.yaml";

std::error_code ec;
fs::copy_file(src, dst,
              fs::copy_options::overwrite_existing, ec);
// Possibility 1: if dst already exists, its contents may be overwritten step by step
// during the copy, so other processes can observe a partially copied file
// Possibility 2: if the machine loses power mid-copy, dst may end up incomplete or even corrupted
if (ec) {
    std::cerr << "复制失败: " << ec.message() << "\n";
} else {
    std::cout << "复制成功\n";
}
```

## Deleting and Renaming

`fs::remove(p)` deletes a file or an empty directory. If the path does not exist, it returns `false` (no error). If the path is a symbolic link, it removes the link itself, not the target. If the path is a non-empty directory, the deletion fails:

```cpp
fs::path temp = "/tmp/temp_file.txt";
bool removed = fs::remove(temp);
if (removed) {
    std::cout << "已删除\n";
} else {
    std::cout << "文件不存在或删除失败\n";
}
```

`fs::remove_all(p)` recursively deletes a directory and everything inside it (files, subdirectories, symbolic links), returning the number of files removed. This is a "nuke-grade" operation — always confirm the path is right before calling it:

```cpp
fs::path temp_dir = "/tmp/my_temp_dir";
auto count = fs::remove_all(temp_dir);
std::cout << "删除了 " << count << " 个文件/目录\n";
```

`remove_all` is irreversible. Once, while debugging, I got the path wrong (dropped one directory level) and nearly wiped out an entire project directory. Luckily it was running in a test environment, so nothing real was lost. Ever since, I always print the path and double-check before calling `remove_all`. I suggest you build the same habit.

`fs::rename(old_path, new_path)` renames or moves a file/directory. In most implementations, renaming within the same file system is an atomic operation (only the directory entry changes; no data moves). Note: renaming across file systems usually **fails** (throwing an exception or returning an error) instead of automatically doing copy + delete. To move across file systems, use `copy` + `remove` explicitly:

```cpp
std::error_code ec;
fs::rename("/tmp/old_name.txt", "/tmp/new_name.txt", ec);
if (ec) {
    std::cerr << "重命名失败: " << ec.message() << "\n";
}
```

## Permissions and Disk Space

### permissions: Modifying File Permissions

`fs::permissions(p, prms)` modifies a file's permission bits, much like `chmod`. Permissions are represented by the `fs::perms` enum:

```cpp
fs::path script = "/tmp/my_script.sh";

// Set to rwxr-xr-x (755)
fs::permissions(script,
    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
    fs::perms::group_read | fs::perms::group_exec |
    fs::perms::others_read | fs::perms::others_exec);

// Or use perm_options to control how the bits are modified
fs::permissions(script,
    fs::perms::owner_exec,     // Modify only the owner_exec bit
    fs::perm_options::add);    // Add it (other bits unaffected)
```

The third parameter, `perm_options`, can be `replace` (replace all permissions — the default behavior), `add` (add the specified permission bits), or `remove` (remove the specified permission bits). When you only need to modify one or two bits, this is more convenient than replacing the whole permission set.

### space: Querying Disk Space

`fs::space(p)` returns a `space_info` struct holding the disk's capacity, used space, and available space:

```cpp
auto info = fs::space("/tmp");
if (info.capacity > 0) {
    std::cout << "总容量:   "
              << info.capacity / (1024.0 * 1024 * 1024) << " GB\n";
    std::cout << "可用空间: "
              << info.available / (1024.0 * 1024 * 1024) << " GB\n";
    std::cout << "剩余空间: "
              << info.free / (1024.0 * 1024 * 1024) << " GB\n";
}
```

Note the difference between `available` and `free`: `free` is the raw remaining space on the disk (including the portion only root can use), while `available` is what the current user can actually use. On Linux, the gap between the two comes from reserved blocks (ext4 reserves 5% for root by default).

## Temporary File Handling

C++ provides no standard API for "creating a temporary file" directly (C++23's `std::filesystem::temp_directory_path()` only tells you where the temporary directory is). But in C++17, we can combine the tools we already have to handle temporary files safely:

```cpp
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;

/// @brief Create a unique temporary file path
/// @return The temporary file's path (the file is not created yet)
fs::path make_temp_file() {
    auto temp_dir = fs::temp_directory_path();

    // Generate a random suffix
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 999999);
    auto suffix = std::to_string(dist(gen));

    auto temp_path = temp_dir / ("myapp_temp_" + suffix + ".tmp");
    return temp_path;
}

/// @brief Safely write data to a temporary file, then atomically rename it to the target file
/// @param target Target file path
/// @param data The data to write
/// @return Whether it succeeded
bool safe_write_file(const fs::path& target, const std::string& data) {
    auto temp = make_temp_file();

    // Write to the temporary file first
    {
        std::ofstream out(temp);
        if (!out) return false;
        out << data;
        out.close();
        if (out.fail()) {
            fs::remove(temp);
            return false;
        }
    }

    // Atomic rename
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp);  // Clean up the temporary file
        return false;
    }
    return true;
}
```

This "write to a temporary file + atomic rename" pattern matters a lot whenever data integrity must be guaranteed — if the program crashes or the power goes out during the write, the target file is either the complete old version or the complete new one, never a corrupted "half-written" state. Many databases, configuration file managers, and package managers rely on exactly this pattern.

## In Practice: A Log Rotation Tool

Let's combine everything from this article and write a practical log rotation tool. The core logic of log rotation: once the log file exceeds a certain size, rename it to a backup file (with a sequence number), then create a new empty log file. The number of backups is also capped — old backups beyond the cap get deleted.

```cpp
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>

namespace fs = std::filesystem;

/// @brief Perform a log rotation
/// @param log_path Log file path
/// @param max_size Maximum file size (bytes)
/// @param max_backups Maximum number of backups
void rotate_log(const fs::path& log_path,
                std::uintmax_t max_size,
                int max_backups) {
    std::error_code ec;

    // Check whether the log file exists and exceeds the size limit
    if (!fs::exists(log_path, ec) || ec) return;
    auto size = fs::file_size(log_path, ec);
    if (ec || size < max_size) return;

    auto stem = log_path.stem().string();
    auto ext = log_path.extension().string();
    auto parent = log_path.parent_path();

    // Collect the existing backup files
    std::vector<fs::path> backups;
    for (int i = 1; i <= max_backups + 1; ++i) {
        auto backup_name = stem + "." + std::to_string(i) + ext;
        auto backup_path = parent / backup_name;
        if (fs::exists(backup_path)) {
            backups.push_back(backup_path);
        }
    }

    // Delete old backups beyond the count limit
    std::sort(backups.begin(), backups.end());
    while (static_cast<int>(backups.size()) >= max_backups) {
        fs::remove(backups.back(), ec);
        backups.pop_back();
    }

    // Shift existing backup sequence numbers up by one
    for (int i = static_cast<int>(backups.size()); i >= 1; --i) {
        auto old_name = stem + "." + std::to_string(i) + ext;
        auto new_name = stem + "." + std::to_string(i + 1) + ext;
        fs::rename(parent / old_name, parent / new_name, ec);
    }

    // Rename the current log to the .1 backup
    auto first_backup = parent / (stem + ".1" + ext);
    fs::rename(log_path, first_backup, ec);

    // Create a new empty log file
    std::ofstream(log_path).close();

    std::cout << "日志轮转完成: " << log_path << "\n";
}

int main() {
    // Example: rotate app.log when it exceeds 1 MB, keeping at most 5 backups
    rotate_log("/tmp/app.log", 1024 * 1024, 5);
    return 0;
}
```

After it runs, the files under `/tmp/` will look like this:

```text
app.log         ← the new empty log file
app.1.log       ← the previous log
app.2.log       ← the log from two rotations ago
...
app.5.log       ← the oldest backup
```

This rotation tool uses `exists`, `file_size`, `rename`, `remove` — essentially every core operation from this article. The "atomic rename" guarantees that no log data is lost during rotation — even if the program crashes mid-rename, at worst one backup file doesn't finish renaming, and the next rotation sorts it out automatically.

## Two Patterns of Error Handling

Throughout this article, I have been handling errors in two ways: throwing exceptions and `std::error_code`. Let's sum up the best practices for error handling in `<filesystem>`.

Most `fs::xxx()` functions come in two overloads: one that throws a `fs::filesystem_error` exception on failure, and another that takes a `std::error_code&` parameter and reports the error code through it on failure. Which one to pick depends on your situation:

```cpp
// Pattern 1: throw (for operations that "should not fail")
fs::create_directories("/tmp/myapp/data");

// Pattern 2: error_code (for operations that "might fail")
std::error_code ec;
fs::copy(src, dst, ec);
if (ec) {
    // Handle the error
}
```

My personal preference: for initialization work at program startup (creating config directories and the like), use the throwing version — failure there means the program cannot run properly anyway, and an exception can abort the startup flow directly. For operations that can legitimately fail at runtime (copying files, deleting temporary files, and so on), use the `error_code` version — those failures are expected and need to be handled gracefully.

## References

- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)
- [cppreference: copy](https://en.cppreference.com/w/cpp/filesystem/copy)
- [cppreference: create_directory](https://en.cppreference.com/cpp/filesystem/create_directory)
- [cppreference: remove](https://en.cppreference.com/w/cpp/filesystem/remove)
- [cppreference: permissions](https://en.cppreference.com/w/cpp/filesystem/permissions)
- [C++ Stories: 22 Common Filesystem Tasks](https://www.cppstories.com/2024/common-filesystem-cpp20/)
