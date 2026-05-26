---
title: File and Directory Operations
description: exists, copy, move, remove, permission and space queries
chapter: 9
order: 2
tags:
- host
- cpp-modern
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 17
reading_time_minutes: 15
prerequisites:
- 'Chapter 9: path 操作'
related:
- 目录遍历与搜索
translation:
  source: documents/vol2-modern-features/ch09-filesystem/02-filesystem-ops.md
  source_hash: 4c4bf535b1461d4894271c9c1a332b3888c53edf90bd80a79ece569f303f954e
  translated_at: '2026-05-26T11:33:50.845819+00:00'
  engine: anthropic
  token_count: 3323
---
# File and Directory Operations

In the previous article, we learned how to use `std::filesystem::path` to handle path syntax—constructing, decomposing, modifying, and comparing paths, all as pure computations without touching the disk. In this article, we get down to business: using the `std::filesystem` library to directly manipulate the file system—checking if files exist, creating directories, copying files, deleting files, and querying permissions and disk space.

As with the previous article, our environment is C++17, with GCC 13+ / Clang 15+ / MSVC 2022. The header file is `<filesystem>`, and the namespace is `std::filesystem`.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Use `exists`, `is_regular_file`, and `is_directory` to check file status
> - [ ] Master the use of `create_directory` and `create_directories`
> - [ ] Safely perform file copy and delete operations
> - [ ] Understand metadata queries such as `file_size`, `last_write_time`, and `status`
> - [ ] Write a practical log rotation tool

## File Status Queries: Does it exist? What type is it?

The first step in file system operations is usually to "see what's actually at this path." `std::filesystem` provides a set of query functions to answer this question.

### exists: Does the path exist?

`exists` checks whether a given path exists on the file system. It can accept a `path` object or a `std::string_view` (which we will cover in the next article). It returns `bool`:

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

⚠️ `exists` throws an exception in certain situations (such as insufficient permissions preventing access to the parent directory). If you don't want exceptions to propagate, use the overload that does not accept an `error_code`, or wrap it in a try-catch block. A better approach is to use the overload that accepts an `error_code`:

```cpp
std::error_code ec;
bool exists = fs::exists(p, ec);
if (ec) {
    std::cerr << "查询失败: " << ec.message() << "\n";
}
```

### is_regular_file / is_directory / is_symlink: Type checking

Once we know a path exists, the next step is to determine its type. `is_regular_file` checks if it is a regular file, `is_directory` checks if it is a directory, and `is_symlink` checks if it is a symbolic link. There are also more granular type checks like `is_block_file`, `is_character_file`, `is_fifo`, `is_socket`, and `is_other`, which are occasionally used in Linux system programming.

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

⚠️ If the path does not exist, these functions return `false`—they do not throw exceptions. So you don't need to call `exists` first before checking the type; just check directly. However, note that if the underlying `status` call itself fails (e.g., due to permission issues), it will throw a `filesystem_error` exception.

### file_size / last_write_time / status: Metadata queries

Besides the type, we often need to query a file's size, last modification time, and permission status:

```cpp
#include <filesystem>
#include <iostream>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

void print_file_info(const fs::path& p) {
    std::error_code ec;

    // 文件大小（字节）
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

    // 最后修改时间
    auto ftime = fs::last_write_time(p, ec);
    if (!ec) {
        // C++20 之前：需要转换成 time_t 来显示
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
        std::cout << "修改时间: "
                  << std::ctime(&time_t_val);
    }

    // 文件状态（权限等）
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

⚠️ Converting `last_write_time` into a readable format before C++20 is a bit cumbersome (as shown above), because the clock used by `file_time_type` is not necessarily `std::chrono::system_clock`. C++20 provides a more concise approach via `std::chrono::clock_cast`, but in C++17, we can only use the approximate method shown above. In real projects, using `std::ctime` for simple display is sufficient, though the precision might not be completely accurate.

## Creating Directories

`create_directory` creates a single directory—provided that the parent directory already exists. If the parent directory does not exist, the call will fail:

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

If you need to create a multi-level directory (for example, `a/b/c`, where neither `a/b` nor `a` exists), use `create_directories`. It automatically creates all missing intermediate directories in the path, similar to `mkdir -p`:

```cpp
fs::path deep_dir = "/tmp/myapp/data/cache/tmp";
fs::create_directories(deep_dir);  // 自动创建所有中间目录
std::cout << "创建完成\n";
```

`create_directories` is one of the file system operations I use the most. Ensuring that configuration, log, and cache directories exist at program startup is a very common requirement. With `create_directories`, one line of code gets it done, without manually checking whether each level exists.

⚠️ `create_directory` returns `false` when the directory already exists, but it does not report an error. The same applies to `create_directories`—if all directories already exist, it also returns `false`. Therefore, you should not use the return value to determine "whether an error occurred," but rather use the `error_code` version.

## Copying Files and Directories

`copy` is a versatile copy function. Its behavior depends on the type of the `from` path and whether `copy_options` are specified:

```cpp
// 默认行为：
// - 如果 from 是普通文件，复制文件到 to
// - 如果 from 是目录，复制目录结构到 to（不递归复制内容）
// - 如果 from 是符号链接，复制链接本身

fs::path src = "/tmp/source.txt";
fs::path dst = "/tmp/dest.txt";

std::error_code ec;
fs::copy(src, dst, ec);
if (ec) {
    std::cerr << "复制失败: " << ec.message() << "\n";
}
```

### copy_options: Controlling copy behavior

`copy_options` is a bitmask type used to finely control copy behavior. Common options include:

`copy_options::overwrite_existing`—If the target file already exists, overwrite it. By default, if the target already exists, `copy` will fail (or skip, depending on the specific operation).

`copy_options::recursive`—Recursively copy directory contents. If `from` is a directory, it will recursively copy all files and subdirectories within it.

`copy_options::copy_symlinks`—Copy the symbolic link itself (rather than following the link to copy the target file).

```cpp
// 递归复制整个目录
fs::copy("/tmp/source_dir", "/tmp/dest_dir",
         fs::copy_options::recursive |
         fs::copy_options::overwrite_existing);
```

`copy_file` is a function specifically for copying files. The difference between it and `copy` is that `copy_file` only handles regular files and provides finer-grained control. ⚠️ Note: `copy_file` **does not provide atomicity guarantees**—if the copy fails midway (e.g., due to insufficient disk space or a power outage), the target file might be in a partially written state. If atomicity is required, you should use the "copy to a temporary file + atomic rename" pattern.

```cpp
// 安全的文件复制（原子性保证）
fs::path src = "/data/important_config.yaml";
fs::path dst = "/backup/important_config.yaml";

std::error_code ec;
fs::copy_file(src, dst,
              fs::copy_options::overwrite_existing, ec);
if (ec) {
    std::cerr << "复制失败: " << ec.message() << "\n";
} else {
    std::cout << "复制成功\n";
}
```

## Deleting and Renaming

`remove` deletes a file or an empty directory. If the path does not exist, it returns `false` (no error). If the path is a symbolic link, it deletes the link itself rather than the target. If the path is a non-empty directory, the deletion fails:

```cpp
fs::path temp = "/tmp/temp_file.txt";
bool removed = fs::remove(temp);
if (removed) {
    std::cout << "已删除\n";
} else {
    std::cout << "文件不存在或删除失败\n";
}
```

`remove_all` recursively deletes a directory and all its contents (files, subdirectories, symbolic links). It returns the number of deleted files. This is a "nuclear-level" operation—always confirm the path is correct before calling it:

```cpp
fs::path temp_dir = "/tmp/my_temp_dir";
auto count = fs::remove_all(temp_dir);
std::cout << "删除了 " << count << " 个文件/目录\n";
```

⚠️ `remove_all` is an irreversible operation. I once accidentally wrote the wrong path while debugging (missing a directory level) and almost wiped out the entire project directory. Fortunately, I was running in a test environment at the time, so no actual damage was done. Since then, I always print and confirm the path before calling `remove_all`. I recommend you build this habit too.

`rename` renames or moves a file/directory. In most implementations, renaming on the same file system is an atomic operation (it only modifies the directory entry, without moving data). ⚠️ Note: Renaming across file systems typically **fails** (throwing an exception or returning an error), rather than automatically performing a copy + delete. To move across file systems, you should explicitly use `copy` + `remove`:

```cpp
std::error_code ec;
fs::rename("/tmp/old_name.txt", "/tmp/new_name.txt", ec);
if (ec) {
    std::cerr << "重命名失败: " << ec.message() << "\n";
}
```

## Permissions and Disk Space

### permissions: Modifying file permissions

`permissions` modifies a file's permission bits, similar to `chmod`. Permissions are represented by the `perms` enum:

```cpp
fs::path script = "/tmp/my_script.sh";

// 设置为 rwxr-xr-x (755)
fs::permissions(script,
    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
    fs::perms::group_read | fs::perms::group_exec |
    fs::perms::others_read | fs::perms::others_exec);

// 或者用 perm_options 控制修改方式
fs::permissions(script,
    fs::perms::owner_exec,     // 只修改 owner_exec 位
    fs::perm_options::add);    // 添加（不影响其他位）
```

The third parameter, `perm_options`, can be `perm_options::replace` (replace all permissions, the default behavior), `perm_options::add` (add the specified permission bits), or `perm_options::remove` (remove the specified permission bits). This is more convenient than replacing all permissions when you only need to modify one or two permission bits.

### space: Querying disk space

`space` returns a `space_info` struct containing the disk's capacity, used space, and available space:

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

Note the difference between `available` and `free`: `free` is the remaining space on the disk (including the portion only usable by root), while `available` is the space actually available to the current user. On Linux, the difference between these two values comes from reserved blocks (ext4 reserves 5% for root by default).

## Handling Temporary Files

C++ does not provide a standard API for "creating temporary files" directly (C++23's `temp_directory_path` only tells you where the temporary directory is). However, in C++17, we can combine existing tools to safely handle temporary files:

```cpp
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;

/// @brief 创建一个唯一的临时文件路径
/// @return 临时文件的路径（文件尚未创建）
fs::path make_temp_file() {
    auto temp_dir = fs::temp_directory_path();

    // 生成随机后缀
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 999999);
    auto suffix = std::to_string(dist(gen));

    auto temp_path = temp_dir / ("myapp_temp_" + suffix + ".tmp");
    return temp_path;
}

/// @brief 安全地将数据写入临时文件，然后原子性地重命名为目标文件
/// @param target 目标文件路径
/// @param data 要写入的数据
/// @return 是否成功
bool safe_write_file(const fs::path& target, const std::string& data) {
    auto temp = make_temp_file();

    // 先写入临时文件
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

    // 原子性重命名
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp);  // 清理临时文件
        return false;
    }
    return true;
}
```

This "write to temporary file + atomic rename" pattern is crucial in scenarios that require data integrity guarantees—if the program crashes or the power goes out during the write, the target file will either be the old complete version or the new complete version, and will never end up in a corrupted "half-written" state. Many databases, configuration file managers, and package managers use this pattern.

## Practical Example: Log Rotation Tool

Let's combine all the operations we learned in this article to write a practical log rotation tool. The core logic of log rotation is: when a log file exceeds a certain size, rename it to a backup file (with a sequence number), and then create a new empty log file. At the same time, we limit the number of backups, deleting old backups that exceed the limit.

```cpp
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>

namespace fs = std::filesystem;

/// @brief 执行日志轮转
/// @param log_path 日志文件路径
/// @param max_size 最大文件大小（字节）
/// @param max_backups 最大备份数量
void rotate_log(const fs::path& log_path,
                std::uintmax_t max_size,
                int max_backups) {
    std::error_code ec;

    // 检查日志文件是否存在且超过大小限制
    if (!fs::exists(log_path, ec) || ec) return;
    auto size = fs::file_size(log_path, ec);
    if (ec || size < max_size) return;

    auto stem = log_path.stem().string();
    auto ext = log_path.extension().string();
    auto parent = log_path.parent_path();

    // 收集已有的备份文件
    std::vector<fs::path> backups;
    for (int i = 1; i <= max_backups + 1; ++i) {
        auto backup_name = stem + "." + std::to_string(i) + ext;
        auto backup_path = parent / backup_name;
        if (fs::exists(backup_path)) {
            backups.push_back(backup_path);
        }
    }

    // 删除超出数量限制的旧备份
    std::sort(backups.begin(), backups.end());
    while (static_cast<int>(backups.size()) >= max_backups) {
        fs::remove(backups.back(), ec);
        backups.pop_back();
    }

    // 将现有备份序号 +1
    for (int i = static_cast<int>(backups.size()); i >= 1; --i) {
        auto old_name = stem + "." + std::to_string(i) + ext;
        auto new_name = stem + "." + std::to_string(i + 1) + ext;
        fs::rename(parent / old_name, parent / new_name, ec);
    }

    // 将当前日志重命名为 .1 备份
    auto first_backup = parent / (stem + ".1" + ext);
    fs::rename(log_path, first_backup, ec);

    // 创建新的空日志文件
    std::ofstream(log_path).close();

    std::cout << "日志轮转完成: " << log_path << "\n";
}

int main() {
    // 示例：当 app.log 超过 1MB 时轮转，最多保留 5 个备份
    rotate_log("/tmp/app.log", 1024 * 1024, 5);
    return 0;
}
```

After running, the file status under `./logs` will look like this:

```text
app.log         ← 新的空日志文件
app.1.log       ← 上一次的日志
app.2.log       ← 上上次的日志
...
app.5.log       ← 最老的备份
```

This rotation tool uses all the core operations we learned in this article: `exists`, `file_size`, `rename`, and `remove`. The "atomic rename" ensures that no log data is lost during rotation—even if the program crashes during the rename process, the worst-case scenario is that a particular backup file did not finish renaming, and the next rotation will handle it automatically.

## Two Modes of Error Handling

Throughout this article, I have been using two ways to handle errors: throwing exceptions and using `error_code`. Let's summarize the best practices for error handling in `std::filesystem`.

Most `std::filesystem` functions have two overloaded versions: one that throws a `filesystem_error` exception on error, and another that accepts an `error_code` parameter and returns the error code through it on failure. Which one to choose depends on your scenario:

```cpp
// 模式一：抛异常（适合"不应该失败"的操作）
fs::create_directories("/tmp/myapp/data");

// 模式二：error_code（适合"可能失败"的操作）
std::error_code ec;
fs::copy(src, dst, ec);
if (ec) {
    // 处理错误
}
```

My personal preference is: for initialization operations at program startup (like creating configuration directories), use the exception-throwing version—because if these operations fail, it means the program cannot run normally, and an exception can directly terminate the startup process. For operations that might legitimately fail at runtime (like copying files, deleting temporary files, etc.), use the `error_code` version—because these failures are expected and need to be handled gracefully.

## Summary

In this article, we covered the core file operations of the `std::filesystem` library. File status queries (`exists`, `is_regular_file`, `is_directory`) and metadata queries (`file_size`, `last_write_time`, `status`) let us understand "what is actually on the file system." `create_directory` and `create_directories` handle directory creation, with the latter automatically creating intermediate directories, which is very convenient. `copy` / `copy_file` provide flexible file copying, `remove` / `remove_all` handle file deletion, and `rename` provides atomic renaming. `permissions` and `space` handle permission and disk space queries, respectively. `temp_directory_path` and the "write to temporary file + atomic rename" pattern are key techniques for ensuring data integrity.

In the next article, we will discuss directory traversal—`directory_iterator` and `recursive_directory_iterator`—and how to efficiently search for files in a file system.

## Reference Resources

- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)
- [cppreference: copy](https://en.cppreference.com/w/cpp/filesystem/copy)
- [cppreference: create_directory](https://en.cppreference.com/cpp/filesystem/create_directory)
- [cppreference: remove](https://en.cppreference.com/w/cpp/filesystem/remove)
- [cppreference: permissions](https://en.cppreference.com/w/cpp/filesystem/permissions)
- [C++ Stories: 22 Common Filesystem Tasks](https://www.cppstories.com/2024/common-filesystem-cpp20/)
