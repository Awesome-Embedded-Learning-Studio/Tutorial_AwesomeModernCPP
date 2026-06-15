---
chapter: 9
cpp_standard:
- 17
description: exists, copy, move, remove, permission and space queries
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 9: path 操作'
reading_time_minutes: 12
related:
- 目录遍历与搜索
tags:
- host
- cpp-modern
- intermediate
title: File and Directory Operations
translation:
  engine: anthropic
  source: documents/vol2-modern-features/ch09-filesystem/02-filesystem-ops.md
  source_hash: 8fd5e0b1e8e7a44582eb5a5973bf711a2a3129b326f15711a412ff2248853fdc
  token_count: 3359
  translated_at: '2026-06-14T00:19:02.053785+00:00'
---
# File and Directory Operations

In the previous post, we learned how to use `std::filesystem::path` to handle path syntax issues—construction, decomposition, modification, and comparison—all pure computation without touching the disk. In this post, we get real: we use the `std::filesystem` library to manipulate the file system directly—checking if files exist, creating directories, copying files, deleting files, and querying permissions and disk space.

As before, our environment is C++17 with GCC 13+ / Clang 15+ / MSVC 2022. The header file is `<filesystem>`, and the namespace is `std::filesystem`.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Use `exists`, `is_regular_file`, `is_directory` to check file status
> - [ ] Master the usage of `create_directory`, `create_directories`
> - [ ] Safely perform file copy and delete operations
> - [ ] Understand metadata queries like `file_size`, `last_write_time`, `status`
> - [ ] Write a practical log rotation tool

## File Status Queries: Does it exist? What type is it?

The first step in file system manipulation is usually "check what is actually at this path." `std::filesystem` provides a set of query functions to answer this.

### exists: Does the path exist?

`std::filesystem::exists` checks if a given path exists on the file system. It accepts a `path` object or a `symlink_permission` (we'll cover this in the next post). It returns `bool`:

```cpp
#include <filesystem>
namespace fs = std::filesystem;

int main() {
    fs::path p = "test.txt";

    if (fs::exists(p)) {
        // File exists
    } else {
        // File does not exist
    }
}
```

⚠️ `exists` may throw an exception in certain cases (e.g., insufficient permissions to access a parent directory). If you do not want exceptions to propagate, use the overload that does not accept `error_code&`, or wrap it in try-catch. A better approach is to use the overload accepting `error_code&`:

```cpp
std::error_code ec;
if (fs::exists(p, ec)) {
    // ...
} else if (ec) {
    // An error occurred
    std::cerr << "Error: " << ec.message() << std::endl;
}
```

### is_regular_file / is_directory / is_symlink: Type determination

Once we know a path exists, the next step is to determine its type. `is_regular_file` checks if it is a regular file, `is_directory` checks if it is a directory, and `is_symlink` checks if it is a symbolic link. There are also more specific type checks like `is_block_file`, `is_character_file`, `is_fifo`, `is_socket`, and `is_other`, which are occasionally used in Linux system programming.

```cpp
if (fs::is_regular_file(p)) {
    std::cout << "This is a regular file.\n";
} else if (fs::is_directory(p)) {
    std::cout << "This is a directory.\n";
} else if (fs::is_symlink(p)) {
    std::cout << "This is a symbolic link.\n";
}
```

⚠️ If the path does not exist, these functions return `false`—they do not throw exceptions. So you don't need to call `exists` before checking the type; just check directly. However, be aware that if the underlying `status` call fails (e.g., due to permission issues), it will throw a `filesystem_error` exception.

### file_size / last_write_time / status: Metadata queries

Beyond type, we often need to query file size, last modification time, and permission status:

```cpp
if (fs::is_regular_file(p)) {
    // Get file size in bytes
    uintmax_t size = fs::file_size(p);
    std::cout << "Size: " << size << " bytes\n";

    // Get last write time
    auto ftime = fs::last_write_time(p);

    // Convert to system time (approximate for C++17)
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
    std::cout << "Last write time: " << std::asctime(std::localtime(&cftime)) << std::endl;
}
```

⚠️ Converting `last_write_time` to a readable format is a bit verbose in C++17 (as shown above) because the `file_time_type`'s clock is not necessarily `system_clock`. C++20 provides a simpler way via `std::chrono::clock_cast`, but in C++17 we must use the approximation above. In actual projects, using `std::asctime` for simple display is sufficient, though the precision might not be perfectly accurate.

## Creating Directories

`create_directory` creates a directory—provided the parent directory already exists. If the parent does not exist, the call fails:

```cpp
fs::create_directory("foo"); // OK if parent exists
// fs::create_directory("bar/baz"); // Error if "bar" does not exist
```

If you need to create a multi-level directory (e.g., `a/b/c`, where `a` and `a/b` do not exist), use `create_directories`. It automatically creates all missing intermediate directories in the path, similar to `mkdir -p`:

```cpp
fs::create_directories("a/b/c"); // Creates "a", "a/b", and "a/b/c"
```

`create_directories` is one of the file system operations I use most. When a program starts, ensuring that configuration, log, and cache directories exist is a very common requirement. With `create_directories`, one line of code handles it, without manually checking each level.

⚠️ `create_directory` returns `false` if the directory already exists, but it does not report an error. The same applies to `create_directories`—if all directories exist, it returns `false`. Therefore, you should not use the return value to judge "whether an error occurred"; instead, use the `error_code&` version.

## Copying Files and Directories

`std::filesystem::copy` is a multi-function copy utility. Its behavior depends on the type of `from` and whether `options` are specified:

```cpp
fs::copy("src.txt", "dst.txt"); // Copy file
fs::copy("src_dir", "dst_dir", fs::copy_options::recursive); // Copy directory
```

### copy_options: Controlling copy behavior

`copy_options` is a bitmask type used to fine-tune copy behavior. Common options include:

`copy_options::overwrite_existing`—If the target file exists, overwrite it. By default, if the target exists, `copy` fails (or skips, depending on the specific operation).

`copy_options::recursive`—Recursively copy directory contents. If `from` is a directory, it recursively copies all files and subdirectories.

`copy_options::copy_symlinks`—Copy the symbolic link itself (rather than following the link to copy the target file).

```cpp
fs::copy(
    "src_dir", "dst_dir",
    fs::copy_options::recursive |
    fs::copy_options::overwrite_existing
);
```

`copy_file` is a function specifically for copying files. The difference between it and `copy` is that `copy_file` only handles regular files and provides finer control. ⚠️ Note: `copy_file` **provides no atomicity guarantee**—if the copy fails (e.g., insufficient disk space, power outage), the target file may be in a partially written state. For atomicity, use the "copy to temporary file + atomic rename" pattern. (See the `safe_write` function example in the "Temporary File Handling" section).

```cpp
fs::copy_file("src.txt", "dst.txt", fs::copy_options::overwrite_existing);
```

## Deleting and Renaming

`remove` deletes a file or an empty directory. If the path does not exist, it returns `false` (no error). If the path is a symbolic link, it deletes the link itself, not the target. If the path is a non-empty directory, deletion fails:

```cpp
bool deleted = fs::remove("tmp.txt"); // Returns true if deleted
```

`remove_all` recursively deletes a directory and all its contents (files, subdirectories, symbolic links). It returns the count of deleted files. This is a "nuclear" operation—always confirm the path is correct before calling:

```cpp
uintmax_t count = fs::remove_all("build_dir"); // Deletes everything inside
std::cout << "Deleted " << count << " items.\n";
```

⚠️ `remove_all` is irreversible. Once, while debugging, I accidentally wrote the path wrong (missing a directory level) and nearly wiped the entire project directory. Fortunately, I was running in a test environment, so no actual damage occurred. Since then, I always print and confirm the path before calling `remove_all`. I suggest you build this habit too.

`rename` renames or moves a file/directory. In most implementations, renaming on the same file system is an atomic operation (modifying directory entries only, not moving data). ⚠️ Note: Cross-filesystem renaming usually **fails** (throwing an exception or returning an error) rather than automatically performing copy + delete. For cross-filesystem moves, explicitly use `copy` + `remove_all`:

```cpp
// Move file to another disk (not atomic)
fs::copy("src.txt", "/mnt/backup/src.txt");
fs::remove("src.txt");
```

## Permissions and Disk Space

### permissions: Modifying file permissions

`permissions` modifies a file's permission bits, similar to `chmod`. Permissions are represented by the `perms` enum:

```cpp
fs::permissions(
    "script.sh",
    fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
    fs::perm_options::replace
);
```

The third parameter can be `perm_options::replace` (replace all permissions, default behavior), `perm_options::add` (add specified permission bits), or `perm_options::remove` (remove specified permission bits). This is more convenient than replacing all permissions when you only need to modify one or two bits.

### space: Querying disk space

`space` returns a `space_info` struct containing the disk's capacity, used space, and free space:

```cpp
fs::space_info root = fs::space("/");
std::cout << "Total: " << root.capacity << "\n";
std::cout << "Free:  " << root.free << "\n";
std::cout << "Avail: " << root.available << "\n";
```

Note the difference between `free` and `available`: `free` is the remaining space on the disk (including parts only root can use), while `available` is the space actually available to the current user. On Linux, this difference comes from reserved blocks (ext4 reserves 5% for root by default).

## Temporary File Handling

C++ does not provide a standard API for "creating temporary files" directly (C++23's `std::filesystem::temp_directory_path` only tells you where the temporary directory is). However, in C++17, we can combine existing tools to handle temporary files safely:

```cpp
#include <filesystem>
#include <fstream>
#include <random>

namespace fs = std::filesystem;

// Generate a random temporary filename
fs::path temp_filename() {
    std::string random_str;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    for (int i = 0; i < 8; ++i) {
        random_str += "0123456789abcdef"[dis(gen)];
    }
    return fs::temp_directory_path() / ("tmp_" + random_str);
}

// Safely write to a file (atomic rename)
void safe_write(const fs::path& dest, const std::string& content) {
    auto temp = temp_filename();
    {
        std::ofstream ofs(temp, std::ios::binary);
        ofs << content;
    } // File closed here
    fs::rename(temp, dest); // Atomic operation
}
```

This "write to temporary file + atomic rename" pattern is crucial in scenarios requiring data integrity. If the program crashes or power is lost during the write, the target file is either the old complete version or the new complete version—never a "half-written" corrupted state. Many databases, configuration file managers, and package managers use this pattern.

## Real-World Example: Log Rotation Tool

Let's combine all the operations learned in this post to write a practical log rotation tool. The core logic of log rotation is: when a log file exceeds a certain size, rename it to a backup file (with a sequence number) and create a new empty log file. We also limit the number of backups, deleting old ones that exceed the limit.

```cpp
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

void rotate_logs(const fs::path& log_dir, const std::string& base_name, uintmax_t max_size, int max_backups) {
    fs::path current_log = log_dir / (base_name + ".log");

    // Check if log file exists and exceeds size limit
    if (fs::exists(current_log) && fs::file_size(current_log) > max_size) {
        // Rename existing backups (e.g., .log.1 -> .log.2)
        for (int i = max_backups - 1; i >= 1; --i) {
            fs::path old = log_dir / (base_name + ".log." + std::to_string(i));
            fs::path next = log_dir / (base_name + ".log." + std::to_string(i + 1));

            if (fs::exists(old)) {
                fs::rename(old, next);
            }
        }

        // Rename current log to .log.1
        fs::path backup = log_dir / (base_name + ".log.1");
        fs::rename(current_log, backup);

        // Delete excess backup
        fs::path excess = log_dir / (base_name + ".log." + std::to_string(max_backups + 1));
        fs::remove(excess);
    }

    // Create new log file if it doesn't exist
    if (!fs::exists(current_log)) {
        std::ofstream(current_log); // Create empty file
    }
}

int main() {
    // Rotate logs in "./logs" directory
    // Max size 10MB, keep 3 backups
    rotate_logs("./logs", "app", 10 * 1024 * 1024, 3);
    return 0;
}
```

After running, the file status under `./logs` will look like this:

```text
./logs/
├── app.log       (new empty file)
├── app.log.1     (previous app.log)
├── app.log.2     (previous app.log.1)
└── app.log.3     (previous app.log.2)
```

This rotation tool uses all core operations covered in this post: `exists`, `file_size`, `rename`, `remove`. The "atomic rename" ensures no log data is lost during rotation—even if the program crashes during the rename, the worst case is a backup file isn't renamed, which the next rotation will handle automatically.

## Two Modes of Error Handling

Throughout this post, I have been using two ways to handle errors: throwing exceptions and `error_code&`. Let's summarize the best practices for error handling in `std::filesystem`.

Most `std::filesystem` functions have two overloads: one that throws a `filesystem_error` exception on error, and another that accepts an `error_code&` parameter and returns an error code through it. The choice depends on your scenario:

```cpp
// Method 1: Exception (for initialization)
try {
    fs::create_directories("config");
} catch (const fs::filesystem_error& e) {
    std::cerr << "Init failed: " << e.what() << std::endl;
    std::exit(1);
}

// Method 2: error_code (for runtime operations)
std::error_code ec;
fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
if (ec) {
    std::cerr << "Copy failed: " << ec.message() << std::endl;
}
```

My personal preference is: for initialization operations at program startup (creating config directories, etc.), use the throwing version—because failure here means the program cannot run normally, and an exception can directly terminate the startup process. For operations that might fail normally at runtime (copying files, deleting temporary files, etc.), use the `error_code&` version—because these failures are expected and need to be handled gracefully.

## Summary

In this post, we covered the core file operations of the `std::filesystem` library. File status queries (`exists`, `is_regular_file`, `is_directory`) and metadata queries (`file_size`, `last_write_time`, `status`) let us understand "what is actually on the file system." `create_directory` and `create_directories` handle directory creation, with the latter automatically creating intermediate directories, which is very convenient. `copy` / `copy_file` provide flexible file copying, `remove` / `remove_all` provide file deletion, and `rename` provides atomic renaming. `permissions` and `space` handle permission and disk space queries respectively. `std::filesystem::path` and the "write temporary file + atomic rename" pattern are key techniques for ensuring data integrity.

In the next post, we will discuss directory traversal—`directory_iterator` and `recursive_directory_iterator`—and how to efficiently search for files in the file system.

## Reference Resources

- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)
- [cppreference: copy](https://en.cppreference.com/w/cpp/filesystem/copy)
- [cppreference: create_directory](https://en.cppreference.com/cpp/filesystem/create_directory)
- [cppreference: remove](https://en.cppreference.com/w/cpp/filesystem/remove)
- [cppreference: permissions](https://en.cppreference.com/w/cpp/filesystem/permissions)
- [C++ Stories: 22 Common Filesystem Tasks](https://www.cppstories.com/2024/common-filesystem-cpp20/)
