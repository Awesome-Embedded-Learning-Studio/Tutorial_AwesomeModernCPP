---
chapter: 7
cpp_standard:
- 17
- 20
description: 'Here is the translation of the description:


  We dive deep into C++17 filesystem path concatenation and normalization, queries,
  two-tier iterators, and CRUD operations. We explore the duality of exceptions and
  `error_code`, the distinction between `status` and `symlink_status`, and reveal
  the performance truth—"system calls are expensive, caching is king"—with a real-world
  benchmark traversing `/usr/include`.'
difficulty: intermediate
order: 57
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- 迭代器基础与 category
reading_time_minutes: 16
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'Filesystem: C++17 Cross-Platform Filesystem Operations'
translation:
  source: documents/vol3-standard-library/io/57-filesystem.md
  source_hash: 1a3edb4a01780494512a6f716989d09a2d1127b7826a00957ef674920b5fcf65
  translated_at: '2026-06-24T00:44:20.355746+00:00'
  engine: anthropic
  token_count: 4442
---
# filesystem: C++17 Cross-Platform Filesystem Operations

In the previous articles, we worked entirely within memory—containers, iterators, algorithms, strings—data all lived inside the process. Now, we shift our focus outside the process: the filesystem on the hard drive.

Before C++17, this was a notorious pain point. The standard library only offered `<fstream>` for reading and writing file contents, but it said nothing about basic needs like "creating a directory, checking file size, or recursively traversing a folder." To get the job done, we had to write our own `#ifdef` soup: POSIX used `opendir`/`stat`/`mkdir`, while Windows used `FindFirstFile`/`GetFileAttributes`/`CreateDirectory`. Two sets of APIs, two path separators, two error codes. Cross-platform projects inevitably ended up with a custom `fs_posix.cpp` + `fs_win.cpp`, which became a maintenance burden over time.

C++17 standardized Boost.FileSystem as `<filesystem>`. One API, one `path` type, one set of iterators, liberating "cross-platform filesystem operations" from hand-written `#ifdef` blocks. In this article, we will put this library through its paces—how to represent and compose paths, query attributes, traverse, perform CRUD operations, handle errors, and finally, run a real-world benchmark traversing `/usr/include` to reveal its performance characteristics. Reading and writing file **content** (`ifstream`/`ofstream`) belongs to Article 56; here, we focus strictly on the filesystem's metadata and structure.

## path: Portable Path Representation

The foundation of `<filesystem>` is `std::filesystem::path`. It abstracts a path as a "sequence of path components" rather than a raw string—this might sound trivial, but it ensures that all subsequent operations like concatenation, decomposition, and normalization work correctly across platforms.

Internally, `path` stores data in an implementation-defined "native format" (POSIX uses `/` as a separator, Windows recognizes both `\` and `/`), while exposing a platform-agnostic interface to the outside world. Let's start with the four most common decomposition tools:

```cpp
// Standard: C++20
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
    fs::path p = fs::path{"/home"} / "charlie" / "proj" / "main.cpp";
    std::cout << "full path       : " << p << '\n';
    std::cout << "parent_path     : " << p.parent_path() << '\n';
    std::cout << "filename        : " << p.filename() << '\n';
    std::cout << "stem            : " << p.stem() << '\n';
    std::cout << "extension       : " << p.extension() << '\n';
}
```

Here are the results obtained by running `g++ -std=c++20 -O2` (native GCC 16.1.1):

```text
full path       : "/home/charlie/proj/main.cpp"
parent_path     : "/home/charlie/proj"
filename        : "main.cpp"
stem            : "main"
extension       : ".cpp"
```

`operator/` is the primary method for `path` concatenation, with the semantics of "appending a path component to the end." Meanwhile, `stem` and `extension` are the golden duo for filename processing: `stem` is "the filename without the extension," and `extension` is "the extension starting from the last dot" (including the dot). Note that `extension` returns `.cpp`, not `cpp`—this differs from the intuition of many who hand-roll string processing, so don't forget the dot when concatenating it back.

### The Counterintuitive Pitfall of `operator/`: A Rooted Right Operand Swallows the Left

The concatenation rules of `operator/` have a pitfall that is easy to stumble into. Its semantics are not mindless string concatenation, but rather "appending the right operand to the left operand"—**however, when the right operand is an absolute path (with a root name or root directory), the left operand is discarded entirely**, and the right operand is returned directly. This actually aligns with path semantics (an absolute path is self-explanatory, so prefixing it with anything is meaningless), but it can be confusing when writing code:

```cpp
fs::path base = "/opt/app";
fs::path joined  = base / "/etc/config";   // 右边是绝对路径
fs::path joined2 = base / "etc/config";    // 右边是相对路径
std::cout << "base / \"/etc/config\" => " << joined << '\n';
std::cout << "base / \"etc/config\"  => " << joined2 << '\n';
```

```text
base / "/etc/config" => "/etc/config"
base / "etc/config"  => "/opt/app/etc/config"
```

In the first example, the right-hand side starts with `/`, making it an absolute path. The `base` is discarded entirely, resulting in `/etc/config`. This behavior is consistent with Python's `os.path.join`, but it contradicts the intuition of pure string concatenation. Therefore, when using `operator/` to join paths, **ensure the right-hand fragment does not start with `/`**, otherwise the preceding prefix is wasted.

### `lexically_normal` and `lexically_relative`: Pure Lexical Normalization

`path` also provides two purely lexical (lexical, meaning they do not touch the disk) transformation functions to handle scenarios involving "dots" in paths.

`lexically_normal` simplifies `.` and `..` **without accessing the file system**:

```cpp
fs::path messy = "a/b/../../c/./d";
std::cout << "lexically_normal  : " << messy.lexically_normal() << '\n';
// a/b/.. => a, a/.. => (空), c/./d => c/d, 最终 "c/d"
```

```text
lexically_normal  : "c/d"
```

It collapses strictly according to path component rules—`b/..` cancels out `b`, `a/..` cancels out `a`, `.` is a no-op, leaving `c/d`. Note that it **does not** resolve symbolic links, nor does it check if these directories exist; it is purely string-level normalization. If you need to follow symbolic links, use `weakly_canonical` or `canonical` (those will actually call `stat`).

`lexically_relative` calculates "how to get from path A to path B relatively":

```cpp
fs::path rel_from = "/a/b/c", rel_to = "/a/b/x/y";
std::cout << "lexically_relative: " << rel_to.lexically_relative(rel_from) << '\n';
```

```text
lexically_relative: "../x/y"
```

To go from `/a/b/c` to `/a/b/x/y`, we first go up one level to `/a/b`, then enter `x/y`, resulting in `../x/y`. This function is particularly useful when generating "relative paths relative to a base directory," for example, converting a batch of absolute paths to relative paths for writing into a configuration file.

## Querying: exists / file_size / is_directory / last_write_time

Once we have a path, the next step is to query its attributes. Query functions in `<filesystem>` fall into two categories: value-returning functions (such as `exists`, `file_size`, and `last_write_time`) and predicate functions (the whole family of `is_directory`, `is_regular_file`, `is_symlink`, etc.). Their usage is quite straightforward:

```cpp
// Standard: C++20
std::cout << "exists(/usr/include)         : " << fs::exists("/usr/include") << '\n';
std::cout << "is_directory(/usr/include)   : " << fs::is_directory("/usr/include") << '\n';
std::cout << "is_regular_file(/usr/include): " << fs::is_regular_file("/usr/include") << '\n';

auto write_tp = fs::last_write_time("/usr/include");
// file_time_type 到 C++20 才有 clock 互转，这里只取 epoch 秒数佐证它能拿到时间
auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                write_tp.time_since_epoch()).count();
std::cout << "last_write_time epoch (sec)  : " << secs << '\n';
```

```text
exists(/usr/include)         : 1
is_directory(/usr/include)   : 1
is_regular_file(/usr/include): 0
last_write_time epoch (sec)  : -4655527457
```

Here are a few key points. `exists` checks for existence, `is_regular_file` and `is_directory` check the type, and `is_symlink` checks for symbolic links. Note that `is_regular_file` internally **follows symbolic links** (it checks the actual file the link points to). To check if the path itself is a link, use `is_symlink`. `last_write_time` returns a `file_time_type`, which is the type defined for interoperability with system clocks since C++20 (the negative epoch seconds mentioned earlier are because this `clock`'s epoch differs from `system_clock`'s—don't let that scare you; in real-world engineering, we just use it directly for timestamp comparisons).

## Traversal: Two-Level Iterators

`<filesystem>` provides two iterators that align perfectly with the STL mental model of "iterating over a sequence." This is why this volume spent so much effort covering iterators earlier—the power of the iterator abstraction pays off immediately here: you can traverse a directory just like you would a `vector`.

`directory_iterator` traverses only the current level and does not descend into subdirectories:

```cpp
// Standard: C++20
std::size_t top = 0;
for (const auto& e : fs::directory_iterator("/usr/include")) {
    (void)e;
    ++top;
}
std::cout << "directory_iterator 顶层条目数: " << top << '\n';
```

The `recursive_directory_iterator` traverses recursively, automatically descending into subdirectories:

```cpp
std::size_t all = 0;
for (const auto& e : fs::recursive_directory_iterator("/usr/include")) {
    (void)e;
    ++all;
}
std::cout << "recursive 总条目数          : " << all << '\n';
```

Here is the output:

```text
directory_iterator 顶层条目数: 791
recursive 总条目数          : 21173
```

There are 791 entries at the top level of `/usr/include`, and 21,173 in total recursively. That's the difference between the two: one looks at the surface, the other digs all the way down. Each dereference in the loop yields a `directory_entry`, which bundles the "path + attributes already queried during this traversal" together—that "cached attribute" part is critical, and we'll cover it in detail in the performance section.

::: warning What if the directory changes during iteration?
`directory_iterator` performs a snapshot-style scan of a directory, but it **does not lock** the directory. If another process (or you yourself) creates or deletes files during the traversal, the Standard allows the iterator to either see or miss these changes; the behavior is implementation-defined. So, don't rely on the traversal being a "consistent snapshot"—if you need consistency, read the directory into a `vector<path>` first before processing.
:::

`recursive_directory_iterator` has two handy switches. One is the `directory_options` parameter passed during construction, such as `skip_permission_denied` (skip directories without permissions instead of throwing an exception)—this is almost mandatory when traversing the entire `/`, otherwise a single root-only directory will crash your whole traversal. The other is the iterator's own member functions: `depth()` (current recursion level) and `recursion_pending()` (whether to descend into the next directory; setting this to `false` allows you to skip it).

## Operations: create / remove / rename / copy

Creating, deleting, renaming, and copying files make up the other major part of file system operations. The naming in `<filesystem>` is very consistent, so you can basically tell what they do just by looking at the names:

```cpp
// Standard: C++20
fs::path root = "/tmp/fs_demo_dir";
fs::remove_all(root);

// create_directories: 一次建多级目录(中间层不存在也建出来)
fs::create_directories(root / "sub1" / "sub2");
std::ofstream(root / "sub1" / "sub2" / "a.txt") << "hello";
std::cout << "create_directories + 写文件 ok\n";

// copy 单个文件
fs::copy(root / "sub1" / "sub2" / "a.txt", root / "sub1" / "a_copy.txt");
std::cout << "copy a.txt -> a_copy.txt ok, exists=" << fs::exists(root / "sub1" / "a_copy.txt") << '\n';

// copy 目录: 必须加 copy_options::recursive,否则只复制目录本身(空壳)
fs::copy(root / "sub1", root / "sub1_copy", fs::copy_options::recursive);
std::cout << "copy 目录(recursive) ok, sub1_copy/sub2/a.txt exists="
          << fs::exists(root / "sub1_copy" / "sub2" / "a.txt") << '\n';

// rename: 改名/移动,同文件系统内是原子的
fs::rename(root / "sub1" / "a_copy.txt", root / "sub1" / "a_renamed.txt");
std::cout << "rename ok\n";

// remove_all: 递归删整个目录树,返回删除的条目数
auto removed = fs::remove_all(root / "sub1_copy");
std::cout << "remove_all(sub1_copy) 删除条目数: " << removed << '\n';
fs::remove_all(root);
```

```text
create_directories + 写文件 ok
copy a.txt -> a_copy.txt ok, exists=1
copy 目录(recursive) ok, sub1_copy/sub2/a.txt exists=1
rename ok
remove_all(sub1_copy) 删除条目数: 4
```

Here are a few pitfalls that are worth expanding on.

`create_directory` and `create_directories` differ by a single letter `s`, but their semantics differ significantly: the former only creates the final level directory and **fails if parent directories do not exist**; the latter behaves like `mkdir -p`, filling in all intermediate levels. A novice writing `create_directory("a/b/c")` where `a` and `b` do not exist will be greeted with an exception or an error code. In the vast majority of scenarios, what you want is `create_directories`.

`copy` is a versatile tool, controlled by `copy_options`. Here are the commonly used flags:

- `copy_options::recursive` — recursive when copying directories (without this, copying a directory only yields an empty directory shell);
- `copy_options::overwrite_existing` — overwrites if the target exists (by default it does not overwrite; if both source and target exist, it simply skips without error);
- `copy_options::copy_symlinks` — copies the symbolic link itself (the default is to follow links, copying the content they point to);
- `copy_options::directories_only` — copies only the directory structure, not files.

`copy_options` is a bitmask type. Combine multiple options with `|`, for example: `copy_options::recursive | copy_options::overwrite_existing`.

`remove` deletes a single file or empty directory (returns `bool`, telling you whether it was deleted), while `remove_all` recursively deletes the entire tree (returns the count of removed items; deleting `sub1_copy` above removed 4 items: the directory itself, `sub2`, and `a.txt` count as one each). `remove_all` silently returns 0 for non-existent paths and does not throw—making it more tolerant than `remove`.

## Error Handling: Dual Paths with Exceptions and `error_code`

The error handling design of `<filesystem>` is one of the most valuable aspects of this library to understand thoroughly. Almost every function that can fail has **two overloads**: one that throws a `filesystem_error` exception, and another that takes a `std::error_code&` as the last output parameter and does not throw. Let's use `file_size` to query a non-existent file and walk through both paths:

```cpp
// Standard: C++20
fs::path bad = "/tmp/this_definitely_does_not_exist_xyz";

// 路径一:不传 error_code,失败抛 filesystem_error
try {
    [[maybe_unused]] auto sz = fs::file_size(bad);   // [[maybe_unused]]: 避免编译器警告返回值没用
} catch (const fs::filesystem_error& ex) {
    std::cout << "抛异常: " << ex.what() << '\n';
}

// 路径二:传 error_code&,失败不抛,错误写进 ec
std::error_code ec;
auto sz = fs::file_size(bad, ec);
std::cout << "error_code 重载: size=" << sz
          << ", ec.value=" << ec.value()
          << ", ec.message=" << ec.message() << '\n';
```

```text
抛异常: filesystem error: cannot get file size: No such file or directory [/tmp/this_definitely_does_not_exist_xyz]
error_code 重载: size=18446744073709551615, ec.value=2, ec.message=No such file or directory
```

The behavioral differences between the two approaches are clear at a glance:

- The **exception version** packs failure details into `filesystem_error::what()`, including the operation name ("cannot get file size"), the system error description ("No such file or directory"), and the paths involved. It provides comprehensive information and is readable, making it suitable for scenarios where "this operation must succeed, or the program cannot proceed."
- The **`error_code` version** does not throw. Upon failure, `ec` is populated with the error code (`ec.value()` is 2, corresponding to POSIX's `ENOENT`; `ec.message()` provides a human-readable description), and the function returns an "invalid value"—`file_size` returns `static_cast<uintmax_t>(-1)`, which is the `18446744073709551615` seen above (the maximum value of `uintmax_t`).

So, when should we use which? This is a practical engineering decision, not a matter of preference.

- The **exception version** is suitable when "the success of this operation is a prerequisite for program correctness"—for example, reading a mandatory configuration file. If it cannot be found, the program should crash, allowing the exception to bubble up to a handler (or simply terminate the process). The main code path remains clean, avoiding the need to check `ec` on every line.
- The **`error_code` version** fits two scenarios: First is **traversal**—when recursively scanning a directory tree, if a subdirectory lacks permissions or has been deleted, we don't want the entire scan to abort due to a single failure. Instead, we use the `error_code` version to quietly retrieve the error, log it, and continue. Second is **performance-sensitive or exception-disabled environments** (such as embedded systems or game engines compiled with `-fno-exceptions`). The exception mechanism incurs overhead, whereas `error_code` is zero-overhead.

The `error_code` type itself (how to check it, categorize it, and use it with `system_category` / `errc`) is a substantial topic on its own; we will cover it in detail in Chapter 66. For now, just remember: filesystem operations almost always provide an overload that "takes `error_code&` and does not throw," and we should prioritize using this for traversal and fault-tolerant scenarios.

## Permissions and Symbolic Links: `status` vs `symlink_status`

`<filesystem>` provides two functions for checking status: `status` and `symlink_status`. The difference comes down to a single sentence—`status` **follows** symbolic links (inspecting the attributes of the actual target object), while `symlink_status` **does not follow** (inspecting the link itself). This distinction is decisive when handling symbolic links; confusing them will lead to completely incorrect judgments.

Let's create a symbolic link pointing to `/usr/include/stdio.h` (a regular file) and inspect it using both functions:

```cpp
// Standard: C++20
fs::path sym_target = "/usr/include/stdio.h";
fs::path sym_link = "/tmp/fs_demo_symlink";
fs::remove(sym_link);
fs::create_symlink(sym_target, sym_link);

auto st = fs::status(sym_link);            // 跟随:看到的是 stdio.h 的属性
auto sl_st = fs::symlink_status(sym_link); // 不跟随:看到的是链接本身
```

Print the two `file_type` values in human-readable form:

```text
status(link) 类型        : regular       (它跟随了链接,看到 stdio.h 是普通文件)
symlink_status(link) 类型: symlink       (它没跟随,看到这个条目本身是个链接)
```

The difference lies right here. `status(link)` follows the link and reports it as `regular` (the target `stdio.h` is a regular file); `symlink_status(link)` reports it as `symlink` (the entry itself is a link). Therefore, using `is_symlink` with the two status functions yields different results:

```cpp
std::cout << "is_symlink(status)        : " << fs::is_symlink(st) << '\n';
std::cout << "is_symlink(symlink_status): " << fs::is_symlink(sl_st) << '\n';
```

```text
is_symlink(status)        : 0
is_symlink(symlink_status): 1
```

`is_symlink` internally calls `symlink_status`, so it checks whether "this entry itself is a symlink". In contrast, `is_regular_file` and `is_directory` use `status` (which follows symlinks), so they examine the object the symlink points to. Remember this rule to avoid mistakes: **To inspect the symlink itself, use `symlink_status` / `is_symlink`; to inspect the target of the symlink, use `status` / `is_regular_file` / `is_directory`**.

By the way, the `exists` / `is_directory` / `is_regular_file` functions mentioned in the query section earlier all follow symlinks (they use `status`). Therefore, a symlink pointing to a directory will report `true` for `is_directory`—this is usually what we want, but if we are building a "backup tool that must not follow symlinks," we must explicitly use `symlink_status` to check manually.

Permissions are represented by the `perms` enumeration, which is a bitmask (consisting of `owner_read`, `owner_write`, `group_exec`, and others). We can obtain them via `status(p).permissions()` and check them using bitwise operations:

```cpp
auto pm = fs::status(sym_target).permissions();
std::cout << "owner_write 位: "
          << ((pm & fs::perms::owner_write) != fs::perms::none) << '\n';
// stdio.h 所有人可写? 这里输出取决于你系统,但机制就是这样
```

```text
owner_write 位: 1
```

The `perms` type also works with `perm_options` to modify permissions via the `permissions(p, perms, perm_options)` function. It supports options like `replace`, `add`, and `remove`. It isn't used often, but it's handy when needed—it can handle tasks like adding permissions to a symbolic link itself using `add | symlink_nofollow`.

## Performance: System Calls and the Importance of Caching

Now, let's address the question we've been avoiding: are `<filesystem>` operations actually fast?

The answer has two layers. First, the cost of a **single operation is basically one system call** (like `stat`, `readdir`, or `mkdir`), plus a thin layer of standard library wrapping. This cost isn't high, but it's definitely not "free"—system calls involve a context switch into the kernel.

Second, **overhead accumulates rapidly during bulk traversal**. Every time a `directory_iterator` advances to the next entry, it triggers a `readdir` in the background. If you check the size or type of each entry, that's another round of `stat` calls. Multiply tens of thousands of entries by one or two system calls each, and the overhead adds up.

We ran a benchmark traversing the real `/usr/include` directory (21,000+ entries). For each entry, we called `is_regular_file` and `file_size`, running three rounds:

```cpp
// Standard: C++20
for (int i = 0; i < kRounds; ++i) {
    std::size_t count = 0;
    std::uintmax_t total_bytes = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        ++count;
        std::error_code ec;
        if (e.is_regular_file(ec)) {
            total_bytes += e.file_size(ec);
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "round " << (i + 1) << ": " << count
              << " entries, " << total_bytes << " bytes, " << ms << " ms\n";
}
```

```text
round 1: 21173 entries, 204330437 bytes, 1352 ms
round 2: 21173 entries, 204330437 bytes, 73 ms
round 3: 21173 entries, 204330437 bytes, 73 ms
```

These three lines of numbers tell us a lot.

The first round took 1352 ms, while the second and third rounds took only 73 ms—a difference of nearly 20 times, yet the code didn't change by a single line. Where does the difference lie? **The operating system's page cache**. In the first round, the disk and directory entries were not yet cached, so every `stat` call had to physically access the storage. By the second round, this data was entirely in the kernel cache, so `stat` simply checked memory, speeding things up by two orders of magnitude. This is the first law of file system performance: **whether your program is fast depends largely on whether the data you access is in the kernel cache, not on how fancy your API is.**

(Don't take the absolute values too seriously—the numbers will fluctuate depending on the machine, directory, or time. What to remember here is the qualitative conclusion that "cold vs. hot cache differs by one to two orders of magnitude." This rule is as solid as iron.)

### Caching in `directory_entry`: Save a `stat` whenever possible

Note that the benchmark above used `e.is_regular_file(ec)` and `e.file_size(ec)`—these are **member functions** of `directory_entry`, not free functions like `fs::is_regular_file(path)`. This difference might look trivial, but behind the scenes lies a carefully designed caching mechanism.

When a `directory_entry` is constructed during directory traversal, the implementation usually **caches the `stat` information for that entry on the spot** (since the traversal already obtained the inode from `readdir`, fetching attributes once is very cheap). Therefore, member functions like `e.is_regular_file()`, `e.file_size()`, and `e.is_directory()` often **hit the cache directly, avoiding another system call**.

However, if you write `fs::is_regular_file(e.path())` or `fs::file_size(e.path())`—passing a `path` to a free function—it will dutifully **issue another `stat` call**, because the free function is unaware that the `path` you are holding was just checked during traversal.

Let's run a controlled experiment. Both groups traverse `/usr/include` to calculate the total bytes. The only difference is whether we use "`directory_entry` members" or "free functions to re-stat." We will warm up the cache first to avoid interference from cold starts:

```cpp
// Standard: C++20
// A: 用 directory_entry 缓存的成员(遍历时已 stat 过,命中缓存)
uintmax_t sum_cached(const fs::path& root) {
    std::uintmax_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::error_code ec;
        if (e.is_regular_file(ec)) total += e.file_size(ec);
    }
    return total;
}

// B: 每个条目重新调 fs::is_regular_file / fs::file_size(path) -> 多一次 stat
uintmax_t sum_restat(const fs::path& root) {
    std::uintmax_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::error_code ec;
        if (fs::is_regular_file(e.path(), ec)) total += fs::file_size(e.path(), ec);
    }
    return total;
}
```

Two rounds of execution (all page caches are hot):

```text
cached (entry members) : 204330437 bytes, 79 ms
re-stat (free fns)     : 204330437 bytes, 108 ms
re-stat / cached       : 1.37x
```

```text
cached (entry members) : 204330437 bytes, 71 ms
re-stat (free fns)     : 204330437 bytes, 110 ms
re-stat / cached       : 1.55x
```

With a hot cache, using `directory_entry` member functions is **1.4 to 1.5 times faster** than re-statting with free functions—simply because the latter adds one `stat` system call per entry. Multiply that by over 21,000 entries, and the difference adds up to 30 or 40 milliseconds. This is the entire motivation behind the standard library's caching design for `directory_entry`: **don't ask for attributes again if we already retrieved them during traversal**.

::: warning Use directory_entry Members During Traversal, Not Paths with Free Functions
When batch traversing and querying attributes, member functions like `e.is_regular_file()`, `e.file_size()`, and `e.is_directory()` hit the internal cache of `directory_entry`. Free functions like `fs::is_regular_file(e.path())` will perform a fresh `stat`. With 21,000 entries, this results in a 1.5x performance difference. **Always prioritize the entry's member functions** in traversal loops; only re-stat if "the entry might have been modified while I held it and needs a refresh."
:::

To summarize the performance mental model for file systems: a single operation equals one system call—neither cheap nor expensive; the cost of batch traversal equals the number of entries multiplied by the number of system calls per entry, so saving calls saves money; and above all, there is an even more critical variable—the kernel page cache, where the difference between cold and hot spans one to two orders of magnitude. This model is independent of specific APIs and holds true for any file system-intensive code.

## Summary

The core of `<filesystem>` boils down to one sentence: **unify cross-platform file system operations using a `path` type, a set of iterators, and a batch of operation functions**. Let's collect a few key conclusions:

- **`path` is the foundation**: `operator/` for concatenation (if the right side is absolute, it eats the left side, so don't accidentally add `/`), `parent_path` / `filename` / `stem` / `extension` for decomposition (`extension` includes the dot), and `lexically_normal` / `lexically_relative` for purely lexical normalization (no disk access).
- **Two levels of iterators**: `directory_iterator` looks only at the current level, while `recursive_directory_iterator` recurses to the end; traversal does not lock the directory and is not a consistent snapshot—concurrent directory modification leads to undefined behavior.
- **Operations are self-explanatory**: `create_directory` creates a single level, `create_directories` acts like `mkdir -p`; `copy` requires `copy_options::recursive` for directories; `remove_all` deletes recursively and returns the count of removed entries.
- **Dual error paths**: The exception-throwing version (throws `filesystem_error`, for the main path and operations that must succeed), and the non-throwing version (takes `error_code&`, for traversal fault tolerance and disabled exception scenarios)—the latter is almost mandatory for traversing directory trees, so a single failure doesn't crash the whole scan. See Article 66 for the `error_code` mechanism itself.
- **Symbolic links: `status` vs `symlink_status`**: `status` follows (looks at the target object), `symlink_status` does not (looks at the link itself); `is_symlink` uses the latter, while `is_regular_file` / `is_directory` use the former.
- **Three laws of performance**: Single operation = one system call; in batch traversal, saving calls saves money (use `directory_entry` members to hit the cache, don't pass `path` to free functions to re-stat, 21,000 entries results in a 1.5x difference); above all, the kernel page cache difference between cold and hot spans one to two orders of magnitude.

Reading and writing file **content**—`ifstream`, `ofstream`, binary vs. text mode, handling large files—is covered in Article 56. This post focuses on the "skeleton" of the file system: paths, attributes, structure, and operations. Together, these two articles complete the cross-platform file processing toolkit.

## References

- [cppreference: Filesystem library](https://en.cppreference.com/w/cpp/filesystem) — Overview and component index for `<filesystem>`
- [cppreference: std::filesystem::path](https://en.cppreference.com/w/cpp/filesystem/path) — Concatenation, decomposition, and lexical transformations of `path` (`lexically_normal` / `lexically_relative`)
- [cppreference: std::filesystem::directory_entry](https://en.cppreference.com/w/cpp/filesystem/directory_entry) — Iterator entries and the "cached attribute member" mechanism
- [cppreference: std::filesystem::copy_options](https://en.cppreference.com/w/cpp/filesystem/copy_options) — Bitmask options for `copy`
- [cppreference: std::filesystem::file_status](https://en.cppreference.com/w/cpp/filesystem/file_status) — Following semantics of `status` vs `symlink_status`
