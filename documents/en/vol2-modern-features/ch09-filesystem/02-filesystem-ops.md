---
chapter: 9
cpp_standard:
- 17
description: exists, copy, move, remove, permission, and space queries
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 9: path 操作'
reading_time_minutes: 16
related:
- 目录遍历与搜索
tags:
- host
- cpp-modern
- intermediate
title: File and Directory Operations
translation:
  source: documents/vol2-modern-features/ch09-filesystem/02-filesystem-ops.md
  source_hash: bddc354baa3b809392cd5539c6d2b8a46359c8a248e8c8e8944aa7df81257eeb
  translated_at: '2026-06-16T03:59:22.562465+00:00'
  engine: anthropic
  token_count: 3354
---
# File and Directory Operations

In the previous post, we learned how to use `std::filesystem::path` to handle path syntax—construction, decomposition, modification, and comparison—all pure computation without touching the disk. In this post, we get real: we use the `std::filesystem` library to directly operate on the file system—checking if files exist, creating directories, copying files, deleting files, and querying permissions and disk space.

As before, our environment is C++17, GCC 13+ / Clang 15+ / MSVC 2022. The header file is `<filesystem>`, and the namespace is `std::filesystem`.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Use `exists`, `is_regular_file`, `is_directory` to check file status
> - [ ] Master the usage of `create_directory`, `create_directories`
> - [ ] Safely perform file copying and deletion operations
> - [ ] Understand `file_size`, `last_write_time`, `status` and other metadata queries
> - [ ] Write a practical log rotation tool

## File Status Queries: Does it exist? What type is it?

The first step in file system operations is usually "check what is actually at this path." `std::filesystem` provides a set of query functions to answer this.

### exists: Does the path exist?

`std::filesystem::exists` checks if a given path exists on the file system. It accepts a `path` object or a `symlink_status` (we will cover this in the next post). It returns `bool`:

```cpp
#include <filesystem>
namespace fs = std::filesystem;

int main() {
    fs::path p = "test.txt";

    if (fs::exists(p)) {
        // Path exists
    } else {
        // Path does not exist
    }
}
```

⚠️ `exists` may throw an exception in some cases (e.g., insufficient permissions preventing access to the parent directory). If you do not want exceptions to propagate, use the overload that does not accept `error_code&`, or wrap it in try-catch. A better approach is to use the overload that accepts `error_code&`:

```cpp
std::error_code ec;
bool exists = fs::exists("test.txt", ec);
if (ec) {
    // Handle error: ec.message()
}
```

### is_regular_file / is_directory / is_symlink: Type determination

Once we know a path exists, the next step is to determine its type. `is_regular_file` checks if it is a regular file, `is_directory` checks if it is a directory, and `is_symlink` checks if it is a symbolic link. There are also more specific type checks like `is_block_file`, `is_character_file`, `is_fifo`, `is_socket`, and `is_other`, which are occasionally used in Linux system programming.

```cpp
if (fs::is_regular_file(p)) {
    // It's a file
} else if (fs::is_directory(p)) {
    // It's a directory
} else if (fs::is_symlink(p)) {
    // It's a symlink
}
```

⚠️ If the path does not exist, these functions return `false`—they do not throw exceptions. So, you do not need to call `exists` before checking the type; just check directly. However, be aware: if the underlying `status` call itself fails (e.g., due to permission issues), it will throw a `filesystem_error` exception.

### file_size / last_write_time / status: Metadata queries

Beyond type, we often need to query file size, last modification time, and permission status:

```cpp
if (fs::is_regular_file(p)) {
    // File size in bytes
    std::uintmax_t size = fs::file_size(p);

    // Last modification time
    fs::file_time_type ftime = fs::last_write_time(p);

    // Convert to system time for display (C++17 approximation)
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
    std::cout << "File time: " << std::asctime(std::localtime(&cftime)) << std::endl;

    // Permission status
    fs::file_status status = fs::status(p);
    fs::perms perms = status.permissions();
}
```

⚠️ Converting `file_time_type` to a readable format was a bit verbose before C++20 (as shown above) because `file_time_type`'s clock is not necessarily `system_clock`. C++20 provides a more concise way via `std::chrono::clock_cast`, but in C++17, the approximation method above must be used. In actual projects, using `std::asctime` for simple display is sufficient, though the precision might not be perfectly accurate.

## Creating Directories

`create_directory` creates a directory—provided the parent directory already exists. If the parent directory does not exist, the call fails:

```cpp
fs::create_directory("foo"); // OK if parent exists
fs::create_directory("foo/bar/baz"); // Error: "foo/bar" does not exist
```

If you need to create a multi-level directory (e.g., `foo/bar/baz`, where `foo` and `foo/bar` do not exist), use `create_directories`. It automatically creates all missing intermediate directories in the path, similar to `mkdir -p`:

```cpp
fs::create_directories("foo/bar/baz"); // Creates foo, foo/bar, and foo/bar/baz
```

`create_directories` is one of the file system operations I use most frequently. When a program starts, ensuring that configuration, log, and cache directories exist is a very common requirement. With `create_directories`, one line of code handles it, without manually checking if each level exists.

⚠️ `create_directory` returns `false` if the directory already exists, but it does not report an error. `create_directories` behaves similarly—if all directories exist, it also returns `false`. Therefore, you should not use the return value to judge "whether an error occurred," but rather use the `error_code&` version.

## Copying Files and Directories

`copy` is a multi-purpose copy function. Its behavior depends on the type of the `from` path and whether `copy_options` are specified:

```cpp
// Copy a file
fs::copy("src.txt", "dst.txt");

// Copy a directory (non-recursive by default)
fs::copy("src_dir", "dst_dir");

// Recursive directory copy
fs::copy("src_dir", "dst_dir", fs::copy_options::recursive);
```

### copy_options: Controlling copy behavior

`copy_options` is a bitmask type used to fine-tune copy behavior. Common options include:

`overwrite_existing`—if the target file exists, overwrite it. By default, if the target exists, `copy` will fail (or skip, depending on the specific operation).

`recursive`—recursively copy directory contents. If `from` is a directory, it recursively copies all files and subdirectories.

`copy_symlinks`—copy the symbolic link itself (rather than following the link and copying the target file).

```cpp
fs::copy("src", "dst",
    fs::copy_options::recursive |
    fs::copy_options::overwrite_existing |
    fs::copy_options::copy_symlinks
);
```

`copy_file` is a function specifically for file copying. The difference between it and `copy` is: `copy_file` only handles regular files and provides finer control. ⚠️ Note: `copy_file` **does not provide atomicity guarantees**—if the copy fails (e.g., insufficient disk space, power loss), the target file may be in a partially written state. If atomicity is required, use the "copy to temporary file + atomic rename" pattern. (See the `std::filesystem::rename` function example in the "Temporary File Handling" section).

```cpp
// Copy file, do not overwrite if exists
bool success = fs::copy_file("src.txt", "dst.txt");

// Force overwrite
fs::copy_file("src.txt", "dst.txt", fs::copy_options::overwrite_existing);
```

## Deleting and Renaming

`remove` deletes a file or an empty directory. If the path does not exist, it returns `false` (no error). If the path is a symbolic link, it deletes the link itself, not the target. If the path is a non-empty directory, deletion fails:

```cpp
bool deleted = fs::remove("tmp.txt"); // true if deleted
```

`remove_all` recursively deletes a directory and all its contents (files, subdirectories, symbolic links). It returns the number of files removed. This is a "nuclear" operation—always confirm the path is correct before calling:

```cpp
std::uintmax_t num_removed = fs::remove_all("build_dir");
std::cout << "Removed " << num_removed << " files/dirs\n";
```

⚠️ `remove_all` is an irreversible operation. Once, while debugging, I accidentally wrote the path wrong (missing a directory level) and nearly wiped the entire project directory. Fortunately, I was running in a test environment, so no actual damage occurred. Since then, I always print and confirm the path before calling `remove_all`. I suggest you develop this habit as well.

`rename` renames or moves a file/directory. In most implementations, renaming on the same file system is an atomic operation (modifying directory entries without moving data). ⚠️ Note: Cross-file system renaming usually **will fail** (throwing an exception or returning an error) rather than automatically performing copy + delete. To move across file systems, explicitly use `copy` + `remove`:

```cpp
// Atomic rename/move on the same filesystem
fs::rename("old.txt", "new.txt");

// Cross-filesystem move (manual implementation)
fs::copy("/src/src.txt", "/dst/src.txt");
fs::remove("/src/src.txt");
```

## Permissions and Disk Space

### permissions: Modifying file permissions

`permissions` modifies a file's permission bits, similar to the `chmod` command. Permissions are represented by the `perms` enum:

```cpp
fs::permissions("script.sh",
    fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read
);
```

The third parameter can be `replace_options::replace` (replace all permissions, default behavior), `replace_options::add` (add specified permission bits), or `replace_options::remove` (remove specified permission bits). This is more convenient than replacing all permissions when you only need to modify one or two bits.

### space: Querying disk space

`space` returns a `space_info` structure containing the disk's capacity, used space, and free space:

```cpp
fs::space_info si = fs::space(".");
std::cout << "Capacity: " << si.capacity << "\n";
std::cout << "Free: " << si.free << "\n";
std::cout << "Available: " << si.available << "\n";
```

Note the difference between `free` and `available`: `free` is the remaining space on the disk (including parts only root can use), while `available` is the space actually available to the current user. On Linux, this difference comes from reserved blocks (ext4 reserves 5% for root by default).

## Temporary File Handling

C++ does not provide a standard API for "creating temporary files" directly (C++23's `std::filesystem::temp_directory_path` only tells you where the temporary directory is). However, in C++17, we can combine existing tools to safely handle temporary files:

```cpp
fs::path temp_file = fs::temp_directory_path() / "tmp_XXXXXX";
// Create a unique filename (simplified logic)
// ... generate unique name logic ...
fs::path target = "data.json";

// Write to temp file
{
    std::ofstream ofs(temp_file);
    ofs << "Important data";
} // File closed here

// Atomic rename
std::error_code ec;
fs::rename(temp_file, target, ec);
if (ec) {
    fs::remove(temp_file); // Clean up if rename failed
}
```

This "write to temp file + atomic rename" pattern is crucial in scenarios requiring data integrity—if the program crashes or power is lost during the write, the target file is either the old complete version or the new complete version; there is no "half-written" corrupted state. Many databases, configuration file managers, and package managers use this pattern.

## Real-world Example: Log Rotation Tool

Let's combine all the operations learned in this post to write a practical log rotation tool. The core logic of log rotation is: when a log file exceeds a certain size, rename it to a backup file (with a sequence number), then create a new empty log file. We also limit the number of backups, deleting old backups that exceed the limit.

```cpp
void rotate_log(const fs::path& log_file, std::size_t max_size, std::size_t max_backups) {
    if (!fs::exists(log_file)) return;

    // Check size
    if (fs::file_size(log_file) < max_size) return;

    // Rotate backups: log.3 -> log.4, log.2 -> log.3, etc.
    for (std::size_t i = max_backups; i > 1; --i) {
        fs::path old = log_file.string() + "." + std::to_string(i - 1);
        fs::path target = log_file.string() + "." + std::to_string(i);
        if (fs::exists(old)) {
            fs::rename(old, target);
        }
    }

    // Move current log to .1
    fs::path backup = log_file.string() + ".1";
    fs::rename(log_file, backup);

    // Create new log file
    std::ofstream(log_file);
}
```

After running, the file status under the log directory will look like this:

```text
app.log      (new empty file)
app.log.1    (previous log)
app.log.2    (previous backup 1)
app.log.3    (previous backup 2)
```

This rotation tool uses `exists`, `file_size`, `rename`, and `remove` (implicit when overwriting) — all core operations learned in this post. The "atomic rename" ensures that no log data is lost during rotation—even if the program crashes during the rename process, at most one backup file will not be renamed, and the next rotation will handle it automatically.

## Two Modes of Error Handling

Throughout this post, I have been using two ways to handle errors: throwing exceptions and using `error_code&`. Let's summarize the best practices for error handling in `std::filesystem`.

Most `std::filesystem` functions have two overloads: one that throws a `filesystem_error` exception on error, and another that accepts an `error_code&` parameter and returns an error code through it on failure. The choice depends on your scenario:

```cpp
// Method 1: Exception (suitable for initialization/fatal errors)
try {
    fs::create_directories("config");
} catch (const fs::filesystem_error& e) {
    std::cerr << "Init failed: " << e.what() << std::endl;
    std::exit(1);
}

// Method 2: error_code (suitable for runtime operations)
std::error_code ec;
fs::copy_file(src, dst, ec);
if (ec) {
    std::cerr << "Copy failed: " << ec.message() << std::endl;
    // Handle error (retry, skip, etc.)
}
```

My personal preference is: for initialization operations at program startup (creating config directories, etc.), use the throwing version—because if these fail, the program cannot run normally, and an exception can directly terminate the startup process. For operations that might fail normally at runtime (copying files, deleting temporary files, etc.), use the `error_code&` version—because these failures are expected and need to be handled gracefully.

## Summary

In this post, we covered the core file operations of the `std::filesystem` library. File status queries (`exists`, `is_regular_file`, `is_directory`) and metadata queries (`file_size`, `last_write_time`, `status`) let us understand "what is actually on the file system." `create_directory` and `create_directories` handle directory creation, with the latter automatically creating intermediate directories, which is very convenient. `copy` / `copy_file` provide flexible file copying, `remove` / `remove_all` provide file deletion, and `rename` provides atomic renaming. `permissions` and `space` handle permission and disk space queries respectively. `temp_directory_path` and the "write to temp file + atomic rename" pattern are key techniques for ensuring data integrity.

In the next post, let's talk about directory traversal—`directory_iterator` and `recursive_directory_iterator`, and how to efficiently search for files in the file system.

## Reference Resources

- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)
- [cppreference: copy](https://en.cppreference.com/w/cpp/filesystem/copy)
- [cppreference: create_directory](https://en.cppreference.com/cpp/filesystem/create_directory)
- [cppreference: remove](https://en.cppreference.com/w/cpp/filesystem/remove)
- [cppreference: permissions](https://en.cppreference.com/w/cpp/filesystem/permissions)
- [C++ Stories: 22 Common Filesystem Tasks](https://www.cppstories.com/2024/common-filesystem-cpp20/)
