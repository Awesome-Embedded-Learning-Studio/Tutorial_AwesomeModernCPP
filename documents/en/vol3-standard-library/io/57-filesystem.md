---
chapter: 7
cpp_standard:
- 17
- 20
description: 'A thorough walkthrough of C++17 filesystem: path concatenation and normalization,
  attribute queries, the two iterator tiers, create/remove/rename/copy operations,
  the dual error paths of exceptions and error_code, and the status vs symlink_status
  distinction for symlinks — plus a real benchmark traversing /usr/include that
  reveals the performance truth: every operation is a system call, and cache is king.'
difficulty: intermediate
order: 57
platform: host
prerequisites:
- 'Deep Dive into std::string: SSO, COW, and resize_and_overwrite'
- 'Iterator Basics and Categories: How Containers and Algorithms Interact'
reading_time_minutes: 16
related:
- 'Container Selection Guide: Choosing the Right Container Based on Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'filesystem: C++17 Cross-Platform Filesystem Operations'
translation:
  source: documents/vol3-standard-library/io/57-filesystem.md
  source_hash: 1a3edb4a01780494512a6f716989d09a2d1127b7826a00957ef674920b5fcf65
  translated_at: '2026-09-26T00:38:44+00:00'
  engine: anthropic
  token_count: 5500
---
# filesystem: C++17 Cross-Platform Filesystem Operations

In the previous articles we've been working entirely inside memory—containers, iterators, algorithms, strings—with all the data living in the process. This article moves the spotlight outside the process: the filesystem on your disk.

Before C++17, this was a chronic sore spot. The standard library had `<fstream>` for reading and writing file contents, but said not one word about the most basic needs: create a directory, check a file's size, recursively walk a folder. If you wanted any of that, you rolled your own `#ifdef` soup: POSIX went through `opendir`/`stat`/`mkdir`, Windows through `FindFirstFile`/`GetFileAttributes`/`CreateDirectory`—two API sets, two path separators, two error code systems. Every cross-platform project carried its own `fs_posix.cpp` + `fs_win.cpp`, and everyone grew tired of maintaining them.

C++17 brought Boost.FileSystem into the standard as `<filesystem>`. One API, one `path` type, one set of iterators—and cross-platform filesystem work was finally freed from hand-written `#ifdef` blocks. In this article we'll take the library apart and run it through: how paths are represented and joined, how attributes are queried, how traversal works, how to create/remove/rename/copy, how errors are handled, and finally a real benchmark traversing `/usr/include` to reveal its performance temperament. Reading and writing file **contents** (`ifstream`/`ofstream`) belongs to article 56; here we only care about the filesystem's own metadata and structure.

## path: A Portable Path Representation

The foundation of all of `<filesystem>` is `std::filesystem::path`. It abstracts a path into a sequence of path components rather than a raw string—this sounds unremarkable, but it's exactly what lets all the later joining, decomposition, and normalization operations work correctly across platforms.

Internally, `path` stores an implementation-defined native format (POSIX separates with `/`; Windows accepts both `\` and `/`), while exposing a platform-independent interface to the outside. Let's start with the four most commonly used decomposition helpers:

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

Compiled with `g++ -std=c++20 -O2` (local GCC 16.1.1), the output is:

```text
full path       : "/home/charlie/proj/main.cpp"
parent_path     : "/home/charlie/proj"
filename        : "main.cpp"
stem            : "main"
extension       : ".cpp"
```

`operator/` is the workhorse of `path` joining; its semantics are "append one path component at the end". The `stem` / `extension` pair, meanwhile, is the golden duo for filename handling: `stem` is the filename with the extension stripped, and `extension` is everything starting from the last dot (dot included). Note that `extension` returns `.cpp`, not `cpp`—this runs against the intuition of most hand-rolled string processing, so don't forget the dot when stitching names back together.

### The operator/ Trap: A Rooted Right Side Eats the Left

The joining rules of `operator/` hide a trap that's easy to step on. Its semantics are not mindless string concatenation but "append the right operand **after** the left operand"—**except that when the right operand is an absolute path (with a root name/root directory), the left operand gets discarded entirely** and the right is returned as-is. This actually matches path semantics (an absolute path is already self-explanatory; prefixing anything onto it is meaningless), but it's easy to get confused when writing:

```cpp
fs::path base = "/opt/app";
fs::path joined  = base / "/etc/config";   // right side is an absolute path
fs::path joined2 = base / "etc/config";    // right side is a relative path
std::cout << "base / \"/etc/config\" => " << joined << '\n';
std::cout << "base / \"etc/config\"  => " << joined2 << '\n';
```

```text
base / "/etc/config" => "/etc/config"
base / "etc/config"  => "/opt/app/etc/config"
```

In the first example the right side starts with `/`, making it absolute, so `base` vanishes entirely and the result is `/etc/config`. This matches Python's `os.path.join` exactly, but runs against pure-string-concatenation intuition. So when joining paths with `operator/`, **never let the right-hand fragment start with a `/`**, or everything you prefixed goes to waste.

### lexically_normal and lexically_relative: Purely Lexical Normalization

`path` also offers two purely lexical transformation functions (lexical as in they never touch the disk), for those paths littered with dots.

`lexically_normal` collapses `.` and `..` **without accessing the filesystem**:

```cpp
fs::path messy = "a/b/../../c/./d";
std::cout << "lexically_normal  : " << messy.lexically_normal() << '\n';
// a/b/.. => a, a/.. => (empty), c/./d => c/d, final result "c/d"
```

```text
lexically_normal  : "c/d"
```

It folds purely by the rules of path components—`b/..` cancels `b`, `a/..` cancels `a`, `.` is a no-op, leaving `c/d`. Note that it does **not** resolve symlinks, nor does it check whether any of these directories exist; it's normalization purely at the string level. To follow symlinks you need `weakly_canonical` / `canonical` (those two actually go and `stat`).

`lexically_relative` computes "starting from path A, how do I walk relatively to path B":

```cpp
fs::path rel_from = "/a/b/c", rel_to = "/a/b/x/y";
std::cout << "lexically_relative: " << rel_to.lexically_relative(rel_from) << '\n';
```

```text
lexically_relative: "../x/y"
```

From `/a/b/c` to `/a/b/x/y`, you first back up one level to `/a/b`, then descend into `x/y`, hence `../x/y`. This function is especially handy when producing "paths relative to some base directory"—say, converting a batch of absolute paths into relative ones to write into a config file.

## Queries: exists / file_size / is_directory / last_write_time

With paths in hand, the next step is querying attributes. `<filesystem>`'s query functions fall into two categories: value-returning (`exists`, `file_size`, `last_write_time`) and predicate-style (the whole `is_directory`, `is_regular_file`, `is_symlink` family). Both are straightforward to use:

```cpp
// Standard: C++20
std::cout << "exists(/usr/include)         : " << fs::exists("/usr/include") << '\n';
std::cout << "is_directory(/usr/include)   : " << fs::is_directory("/usr/include") << '\n';
std::cout << "is_regular_file(/usr/include): " << fs::is_regular_file("/usr/include") << '\n';

auto write_tp = fs::last_write_time("/usr/include");
// file_time_type only gained clock conversions in C++20; here we just take epoch seconds as evidence we can read the time
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

A few key points. `exists` checks existence, `is_regular_file` and `is_directory` check type, and `is_symlink` checks for a symlink—note that `is_regular_file` internally **follows symlinks** (it looks at the real file the link points to); to ask "is this entry itself a link", use `is_symlink`. `last_write_time` returns a `file_time_type`, a type whose conversions to and from the system clock have been pinned down since C++20 (the epoch seconds printed above are negative because this `clock`'s epoch differs from `system_clock`'s—don't be alarmed; in real projects you just use it directly for timestamp comparisons).

## Traversal: Two Tiers of Iterators

`<filesystem>` provides two iterators that map neatly onto the STL's "iterate over a sequence" mental model. This is also why this volume spent so much effort on iterators earlier—once we reach the filesystem, the power of the iterator abstraction pays off immediately: you can traverse a directory the same way you traverse a `vector`.

`directory_iterator` covers only the current level, without drilling down:

```cpp
// Standard: C++20
std::size_t top = 0;
for (const auto& e : fs::directory_iterator("/usr/include")) {
    (void)e;
    ++top;
}
std::cout << "directory_iterator 顶层条目数: " << top << '\n';
```

`recursive_directory_iterator` recurses downward, automatically descending into subdirectories:

```cpp
std::size_t all = 0;
for (const auto& e : fs::recursive_directory_iterator("/usr/include")) {
    (void)e;
    ++all;
}
std::cout << "recursive 总条目数          : " << all << '\n';
```

The output:

```text
directory_iterator 顶层条目数: 791
recursive 总条目数          : 21173
```

The top level of `/usr/include` holds 791 entries; recursing yields 21173 in total. That's the difference between the two: one skims the surface, the other digs to the bottom. Each dereference inside the loop yields a `directory_entry`, which bundles together "the path + the attributes already fetched during this traversal"—and those "already fetched attributes" matter a lot; the performance section is devoted to them.

::: warning What if the directory changes during iteration
A `directory_iterator` scan behaves like a snapshot, and it does **not** lock the directory. If another process (or you yourself) is deleting or creating files while you iterate, the standard permits the iterator to see or to miss those changes—behavior is implementation-defined. So don't count on traversal being a "consistent snapshot"; if you need one, first read the directory into a `vector<path>` and process that.
:::

`recursive_directory_iterator` adds two practical switches. One is the `directory_options` passed at construction, such as `skip_permission_denied` (skip directories you lack permission for instead of throwing)—nearly mandatory when traversing all of `/`, otherwise a single root-only directory sinks your entire traversal. The other is the iterator's own `depth()` (which level you're currently on) and `recursion_pending()` (whether to keep drilling into the next directory; set it to false to skip).

## Operations: create / remove / rename / copy

Create, remove, rename, copy is the other big chunk of filesystem work. `<filesystem>`'s naming is uniform enough that the name mostly tells you what it does:

```cpp
// Standard: C++20
fs::path root = "/tmp/fs_demo_dir";
fs::remove_all(root);

// create_directories: creates multiple directory levels in one shot (missing intermediate levels get created too)
fs::create_directories(root / "sub1" / "sub2");
std::ofstream(root / "sub1" / "sub2" / "a.txt") << "hello";
std::cout << "create_directories + 写文件 ok\n";

// copy a single file
fs::copy(root / "sub1" / "sub2" / "a.txt", root / "sub1" / "a_copy.txt");
std::cout << "copy a.txt -> a_copy.txt ok, exists=" << fs::exists(root / "sub1" / "a_copy.txt") << '\n';

// copy a directory: must add copy_options::recursive, otherwise only the directory itself gets copied (an empty shell)
fs::copy(root / "sub1", root / "sub1_copy", fs::copy_options::recursive);
std::cout << "copy 目录(recursive) ok, sub1_copy/sub2/a.txt exists="
          << fs::exists(root / "sub1_copy" / "sub2" / "a.txt") << '\n';

// rename: rename/move, atomic within the same filesystem
fs::rename(root / "sub1" / "a_copy.txt", root / "sub1" / "a_renamed.txt");
std::cout << "rename ok\n";

// remove_all: recursively deletes the whole directory tree, returns the number of entries removed
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

A few common pitfalls are worth expanding on.

`create_directory` and `create_directories` differ by one `s`, and by one notch of semantics: the former creates only the last level and **fails outright if the parent doesn't exist**; the latter works like `mkdir -p`, filling in every intermediate level for you. A newcomer writing `create_directory("a/b/c")` when neither `a` nor `b` exists will be rewarded with an exception or an error code. In the vast majority of cases what you want is `create_directories`.

`copy` is a generalist whose behavior is steered by `copy_options`; the most-used bits:

- `copy_options::recursive` — recurse when copying directories (without it, copying a directory yields only an empty directory shell);
- `copy_options::overwrite_existing` — overwrite when the destination exists (the default doesn't: when both source and destination exist, the file is simply skipped, with no error);
- `copy_options::copy_symlinks` — copy the symlink itself (the default follows the link and copies what it points to);
- `copy_options::directories_only` — copy only the directory structure, not the files.

`copy_options` is a bitmask, so combine several with `|`, e.g. `copy_options::recursive | copy_options::overwrite_existing`.

`remove` deletes a single file or an empty directory (returns a `bool` telling you whether anything was actually removed), while `remove_all` recursively deletes the whole tree (returning the number of entries removed—the `sub1_copy` above removed 4: the directory itself, sub2, and a.txt each count as one). `remove_all` on a nonexistent path quietly returns 0 instead of throwing—more forgiving than `remove` in that respect.

## Error Handling: The Exception and error_code Dual Paths

The error handling design of `<filesystem>` is one of the most worthwhile parts of this library to unpack. Almost every fallible function comes in **two overloads**: one that throws a `filesystem_error` exception, and one with an extra trailing `std::error_code&` out-parameter that doesn't throw. Let's call `file_size` on a nonexistent file and walk both paths:

```cpp
// Standard: C++20
fs::path bad = "/tmp/this_definitely_does_not_exist_xyz";

// Path 1: pass no error_code, failure throws filesystem_error
try {
    [[maybe_unused]] auto sz = fs::file_size(bad);   // [[maybe_unused]]: silences the compiler warning about the unused result
} catch (const fs::filesystem_error& ex) {
    std::cout << "抛异常: " << ex.what() << '\n';
}

// Path 2: pass error_code&, no throw on failure, the error lands in ec
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

The behavioral difference between the two paths is plain at a glance:

- The **exception version** packs the failure details into `filesystem_error::what()`, carrying the operation name ("cannot get file size"), the system error description ("No such file or directory"), and the paths involved. Complete information, comfortable to read—suited to scenarios of "this must succeed, otherwise the program cannot go on".
- The **error_code version** doesn't throw; on failure `ec` gets filled in (`ec.value()` is 2, corresponding to POSIX `ENOENT`; `ec.message()` is the human-readable description), and the return value is an "invalid value"—a failed `file_size` returns `static_cast<uintmax_t>(-1)`, i.e. the `18446744073709551615` above (the maximum of uintmax).

So when to use which? This is a real engineering judgment, not a matter of taste.

- The **exception version** fits "this operation's success is a precondition of program correctness"—say, reading a config file that must exist: if the read fails, crashing is the right response, letting the exception bubble up to wherever can handle it (or simply letting it terminate). The mainline code stays clean, with no per-line checking of `ec`.
- The **error_code version** fits two situations. One is **traversal**: you're recursively scanning a directory tree, one subdirectory lacks permissions or has already been deleted by someone else, and you don't want that single failure to kill the whole scan—you want to skip it and move on, so you take the error quietly via the error_code version, log it, and carry on. The other is **performance-sensitive or exception-disabled** settings (embedded systems, certain game engines built with `-fno-exceptions`): the exception machinery itself has a cost, while error_code is zero-overhead.

The `error_code` type itself (how to test it, classify it, and pair it with `system_category` / `errc`) is a big standalone topic, which we save for article 66. All you need to remember here: filesystem operations almost all offer a "pass `error_code&`, don't throw" overload, and traversal and fault-tolerant scenarios should prefer it.

## Permissions and Symlinks: status vs symlink_status

`<filesystem>` has two status-querying functions: `status` and `symlink_status`. Their difference fits in one sentence—`status` **follows** symlinks (reporting the attributes of the real object the link points to), while `symlink_status` **does not** (reporting the link itself). When handling symlinks this difference is decisive; mix them up and you'll reach completely wrong conclusions.

Let's create a symlink pointing at `/usr/include/stdio.h` (a regular file) and inspect it with both functions:

```cpp
// Standard: C++20
fs::path sym_target = "/usr/include/stdio.h";
fs::path sym_link = "/tmp/fs_demo_symlink";
fs::remove(sym_link);
fs::create_symlink(sym_target, sym_link);

auto st = fs::status(sym_link);            // follows: sees stdio.h's attributes
auto sl_st = fs::symlink_status(sym_link); // doesn't follow: sees the link itself
```

Printing the two `file_type` values in plain language:

```text
status(link) 类型        : regular       (它跟随了链接,看到 stdio.h 是普通文件)
symlink_status(link) 类型: symlink       (它没跟随,看到这个条目本身是个链接)
```

There's the difference. `status(link)` sees through the link and reports `regular` (the pointed-to stdio.h is a regular file); `symlink_status(link)` reports `symlink` (this entry itself is a link). Consequently, feeding the two statuses to `is_symlink` gives different results:

```cpp
std::cout << "is_symlink(status)        : " << fs::is_symlink(st) << '\n';
std::cout << "is_symlink(symlink_status): " << fs::is_symlink(sl_st) << '\n';
```

```text
is_symlink(status)        : 0
is_symlink(symlink_status): 1
```

`is_symlink` internally calls `symlink_status`, so it answers "is this entry itself a link". Meanwhile `is_regular_file`, `is_directory`, and friends use `status` (following), so they look at the object the link points to. Memorize this rule and you can't go wrong: **to inspect the link itself use `symlink_status` / `is_symlink`; to inspect what the link points to use `status` / `is_regular_file` / `is_directory`**.

As an aside, the `exists` / `is_directory` / `is_regular_file` from the earlier query section all follow links (they use `status`), so a link pointing at a directory gets reported as `true` by `is_directory`—which is what you want in most scenarios. But for jobs like "backup tool that must not follow links", you have to check explicitly with `symlink_status` yourself.

Permissions are represented by the `perms` enum, a bitmask (the whole `owner_read` / `owner_write` / `group_exec` family), obtained via `status(p).permissions()` and tested with bitwise operations:

```cpp
auto pm = fs::status(sym_target).permissions();
std::cout << "owner_write 位: "
          << ((pm & fs::perms::owner_write) != fs::perms::none) << '\n';
// Is stdio.h writable by everyone? The output here depends on your system, but this is the mechanism
```

```text
owner_write 位: 1
```

`perms` also pairs with `perm_options` in the `permissions(p, perms, perm_options)` function for changing permissions (`replace` / `add` / `remove`)—rarely used, but very handy when needed: jobs like granting permissions to the symlink itself via `add | symlink_nofollow` are exactly its territory.

## Performance: A System Call per Operation, Cache Is King

Having gotten this far, it's time to face the question we've been dodging: are these `<filesystem>` operations actually fast?

The answer comes in two layers. First, **the cost of a single operation is essentially one system call** (`stat`, `readdir`, `mkdir`, and the like), plus the standard library's thin wrapper. That cost isn't high, but it's by no means "free" either—a system call traps into the kernel and pays the context-switch overhead.

Second, **when traversing in bulk, the overhead stacks up fast**. Every step `directory_iterator` takes to yield an entry is backed by a `readdir`; if you also want each entry's size or type along the way, that's another round of `stat`. Tens of thousands of entries times one or two system calls each, and the cost adds up.

Let's run a benchmark that really traverses `/usr/include` (21000+ entries), calling `is_regular_file` + `file_size` on each entry along the way, for three rounds in a row:

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

These three lines of numbers carry a lot of information.

Round one takes 1352 ms; rounds two and three take 73 ms—nearly 20x apart, yet not a single line of code changed. Where's the difference? **The operating system's page cache.** In round one the disk/directory entries aren't cached yet, so every `stat` has to dig through real storage; from round two onward all that data sits in the kernel cache, and `stat` becomes a memory lookup—two orders of magnitude faster. This is the first law of filesystem performance: **how fast your program runs depends largely on whether the data you access is in the kernel cache**, not on how fancy the API you used is.

(Don't take the absolute numbers too seriously—switch machines, directories, or time of day, and they will fluctuate. What to remember is the order-of-magnitude conclusion "cold vs warm cache differs by one to two orders of magnitude"; it's as stable as an iron law.)

### directory_entry's Cache: Skip a stat Whenever You Can

Notice that the benchmark above used `e.is_regular_file(ec)` and `e.file_size(ec)`—these are **member functions** of `directory_entry`, not free functions like `fs::is_regular_file(path)`. The distinction looks minor, but behind it lies a carefully designed caching mechanism.

When a `directory_entry` is constructed during directory traversal, implementations typically **cache that entry's `stat` information along the way** (traversal itself has already obtained the inode from `readdir`, so an opportunistic attribute query is very cheap). As a result, members like `e.is_regular_file()` / `e.file_size()` / `e.is_directory()` often **hit the cache directly, without issuing another system call**.

Whereas if you write `fs::is_regular_file(e.path())`, `fs::file_size(e.path())`—passing a `path` to the free function—it will dutifully **issue another `stat`**, because the free function has no idea that the `path` in your hand was just queried during traversal.

Let's run a controlled experiment: both groups traverse `/usr/include` summing total bytes, differing only in "use `directory_entry` members" vs "re-stat via free functions", both warmed up first to keep cold-cache interference out:

```cpp
// Standard: C++20
// A: use directory_entry's cached members (already stat'ed during traversal, cache hit)
uintmax_t sum_cached(const fs::path& root) {
    std::uintmax_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::error_code ec;
        if (e.is_regular_file(ec)) total += e.file_size(ec);
    }
    return total;
}

// B: re-call fs::is_regular_file / fs::file_size(path) per entry -> one extra stat
uintmax_t sum_restat(const fs::path& root) {
    std::uintmax_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::error_code ec;
        if (fs::is_regular_file(e.path(), ec)) total += fs::file_size(e.path(), ec);
    }
    return total;
}
```

Two runs (page cache warm in both):

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

With a warm cache, using `directory_entry` members is **1.4 to 1.5x faster** than re-statting via free functions—solely because the latter spends one extra `stat` system call per entry, and multiplied over 21000+ entries that comes out to a gap of thirty to forty milliseconds. That is the entire motivation for the standard library's caching design in `directory_entry`: **attributes already fetched during traversal should not be requested again**.

::: warning In traversal, use directory_entry members, not paths handed to free functions
When batch-traversing and querying attributes, the member functions `e.is_regular_file()`, `e.file_size()`, `e.is_directory()` hit `directory_entry`'s internal cache; free functions like `fs::is_regular_file(e.path())` will `stat` again. Across 21000 entries that's a 1.5x gap. Inside traversal loops, **always prefer the entry's member functions**, and re-stat only when "this entry may have changed while in my hands and needs refreshing".
:::

To summarize the performance mental model for filesystems: one operation = one system call, neither expensive nor free; bulk traversal cost = entry count × system calls per entry, so saving calls is saving money; and above it all looms an even fiercer variable—the kernel page cache, worth one to two orders of magnitude between cold and warm. This model has nothing to do with the specific API and holds for any filesystem-intensive code you write.

## Summary

The core of `<filesystem>` is one sentence: **one `path` type + one set of iterators + a batch of operation functions unify cross-platform filesystem work**. Let's collect the key conclusions:

- **path is the foundation**: joining with `operator/` (a rooted right side eats the left—don't slip in that leading `/`), decomposition via `parent_path` / `filename` / `stem` / `extension` (`extension` includes the dot), purely lexical normalization with `lexically_normal` / `lexically_relative` (no disk access).
- **Two iterator tiers**: `directory_iterator` sees only the current level, `recursive_directory_iterator` recurses to the bottom; traversal doesn't lock the directory and isn't a consistent snapshot—behavior under concurrent directory modification is undefined.
- **Operations are usable by name**: `create_directory` builds a single level, `create_directories` is `mkdir -p`; copying a directory needs `copy_options::recursive`; `remove_all` recursively deletes and returns the entry count.
- **Dual error paths**: the exception version throwing `filesystem_error` (mainline code, operations that must succeed), and the non-throwing version taking `error_code&` (fault-tolerant traversal, exception-disabled settings)—directory-tree traversal almost always uses the latter, because a single failure must not drag down the whole scan. The `error_code` machinery itself is covered in article 66.
- **status vs symlink_status for symlinks**: `status` follows (looking at the object the link points to), `symlink_status` doesn't (looking at the link itself); `is_symlink` uses the latter, `is_regular_file` / `is_directory` the former.
- **Three laws of performance**: one operation = one system call; in bulk traversal, saving calls is saving money (use `directory_entry` members to hit the cache—don't pass `path` to free functions and re-stat, a 1.5x gap across 21000 entries); and above everything, the kernel page cache's cold-warm gap is one to two orders of magnitude.

Reading and writing file **contents**—`ifstream`, `ofstream`, binary vs text mode, working with large files—is the territory of article 56. This article stayed focused on the filesystem's "skeleton": paths, attributes, structure, operations. Put the two together, and your cross-platform file-handling toolbox is complete.

## References

- [cppreference: Filesystem library](https://en.cppreference.com/w/cpp/filesystem) — overview of `<filesystem>` and its component index
- [cppreference: std::filesystem::path](https://en.cppreference.com/w/cpp/filesystem/path) — `path` joining, decomposition, and lexical transformations (`lexically_normal` / `lexically_relative`)
- [cppreference: std::filesystem::directory_entry](https://en.cppreference.com/w/cpp/filesystem/directory_entry) — iterator entries and the "cached attribute members" mechanism
- [cppreference: std::filesystem::copy_options](https://en.cppreference.com/w/cpp/filesystem/copy_options) — bitmask options for `copy`
- [cppreference: std::filesystem::file_status](https://en.cppreference.com/w/cpp/filesystem/file_status) — the following semantics of `status` and `symlink_status`
