---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: In-depth explanation of the three types of `fstream` file streams and
  `open` modes, the lifecycle pitfalls of RAII automatic `close`, the error state
  machine, differences between text and binary modes, cross-platform pitfalls of directly
  writing structs, and why we should switch to `mmap` or `stdio` for large file I/O.
difficulty: intermediate
order: 56
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- charconv：零开销的数字与字符串互转
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 16
related:
- charconv：零开销的数字与字符串互转
- string 深入：SSO、COW 与 resize_and_overwrite
tags:
- host
- cpp-modern
- intermediate
- 基础
- RAII
title: 'fstream: File Stream I/O, RAII, and Its Portability Pitfalls'
translation:
  source: documents/vol3-standard-library/io/56-fstream.md
  source_hash: 49d39c679c7891d7efcfa9acf1310ad116ec33a64d33e308323b33a859c6c827
  translated_at: '2026-06-24T00:43:23.350805+00:00'
  engine: anthropic
  token_count: 4310
---
# fstream: File Stream I/O, RAII, and Its Portability Pitfalls

Persisting data to disk and reading configuration files back are tasks almost every C++ program can't avoid. The standard library's answer is `<fstream>`: `ifstream` for reading, `ofstream` for writing, and `fstream` for both. Under the hood, RAII manages the lifecycle of file descriptors for us.

It looks like a simple "open, read/write, close" routine, but when you actually write the code, you'll find it full of counter-intuitive corners: Why does the same code work fine on Linux but produce extra `\r` characters on Windows? Why do the fields of a struct I `write` out get completely misaligned when I `read` them back on another machine? Why does the program continue silently when opening fails? Why does data vanish immediately after I write to it following a `close()`? The roots of these pitfalls lie either in the C-legacy "text/binary" modes, the uncertainty of struct memory layouts, or the stream's "state bits + auto-reset" mechanism.

In this article, we will dissect `<fstream>` inside out. The focus isn't on listing every API, but on clarifying three things: **what open mode flags actually change, what RAII manages and what it doesn't, and when you shouldn't use it**. All examples were tested locally on GCC 16.1.1, with output pasted verbatim.

## Three Stream Types and Open Modes: First, Understand What Each Flag Does

`<fstream>` provides three classes, which are essentially the same mechanism with different "directions":

- `std::ifstream` — Read-only by default (implies `std::ios::in`);
- `std::ofstream` — Write-only by default, and **clears on open** (implies `std::ios::out | std::ios::trunc`);
- `std::fstream` — Supports both reading and writing, but **does not imply any direction**, so you must specify `in | out` yourself.

What truly determines behavior is the string of open mode flags. It helps to think of them as "actions performed on the file when opening":

| Flag | What it does | Memory aid |
|------|--------------|-------------|
| `in` | Read | Required, added automatically by `ifstream` |
| `out` | Write | Required, added automatically by `ofstream` |
| `trunc` | Truncate file on open | Default for `ofstream`, most prone to accidents |
| `app` | Seek to end before every write | Appending logs |
| `ate` | Seek to end immediately after open (once) | Checking file size |
| `binary` | Disable platform-specific character translation | Mandatory for binary data |
| `noreplace` (C++23) | Fail if file exists | Safely "create new file only" |

The easiest trap here is `trunc`. In the following code, many beginners assume they are "opening an existing file and writing to it," but the old content vanishes as soon as it opens:

```cpp
// Standard: C++17
#include <fstream>
#include <iostream>
#include <string>

std::string read_all(const char* path) {
    std::ifstream in(path);
    std::string s, line;
    while (std::getline(in, line)) s += line + "|";
    return s;
}

int main() {
    { std::ofstream out("/tmp/m.txt"); out << "OLD"; }
    { std::ofstream out("/tmp/m.txt"); out << "new"; }  // 默认 trunc
    std::cout << "trunc (默认 ofstream): " << read_all("/tmp/m.txt") << "\n";
    return 0;
}
```

```text
trunc (默认 ofstream): new|
```

`OLD` is gone. To preserve the old content and append to the end, we must explicitly add `app`:

```cpp
// Standard: C++17
{ std::ofstream out("/tmp/m.txt", std::ios::app); out << "APPENDED"; }
```

```text
after app: newAPPENDED|
```

The semantics of `app` are stricter than just "positioning at the end": it forces the write pointer to the end of the file **before every write operation**, regardless of where you previously `seekp`ed to. This is exactly the behavior we want for appending logs—multiple threads can write independently without overwriting each other's data regions.

::: warning ate won't save you, trunc still clears
A very common misconception is believing that `std::ios::ate` (position at end upon opening) preserves existing content. **It does not**. `ate` simply means "move the pointer to the end after opening"; it **does not override** the default `trunc` behavior implied by `ofstream`. We have verified this locally:

```cpp
{ std::ofstream out("/tmp/a.txt"); out << "0123456789"; }   // 先写 10 字节
{ std::ofstream out("/tmp/a.txt", std::ios::ate);           // 想保留? 不行
  std::cout << "ate tellp=" << out.tellp() << "\n";
  out << "XY"; }
```

```text
ate tellp=0
```

`tellp` is 0 instead of 10, which indicates that the file has been cleared by `trunc`. `ate` moved to the end of an empty file (which is position 0). Ultimately, only `XY` remains on disk, and `0123456789` is gone.

To "preserve old content, seek to the end upon opening, and still allow arbitrary `seekp`", the combination must be `in | out | ate`:

```cpp
// Standard: C++17
{ std::fstream f("/tmp/a.txt", std::ios::in | std::ios::out | std::ios::ate);
  std::cout << "in|out|ate tellp=" << f.tellp() << "\n";   // 10，内容保住了
  f << "XY"; }
```

```text
in|out|ate tellp=10
content: 0123456789XY
```

Don't worry if you can't remember it all; just memorize this one rule: **whenever you open an existing file with `ofstream`, it is truncated by default**. If you want to preserve the content, you must use `app` or `in | out`.
:::

As for the `noreplace` option added in C++23, the name speaks for itself—it refuses to open if the file already exists, designed specifically for safely "creating new files only, without overwriting." Our local GCC 16.1.1 already supports this:

```cpp
// Standard: C++23
#include <fstream>
#include <iostream>

int main() {
    { std::ofstream out("/tmp/np.txt"); out << "original"; }
    // 文件已存在 -> noreplace 拒绝打开
    std::ofstream a("/tmp/np.txt", std::ios::out | std::ios::noreplace);
    std::cout << "已存在: is_open=" << a.is_open() << " fail=" << a.fail() << "\n";
    // 文件不存在 -> 正常创建
    std::ofstream b("/tmp/np_new.txt", std::ios::out | std::ios::noreplace);
    std::cout << "新文件: is_open=" << b.is_open() << " fail=" << b.fail() << "\n";
    return 0;
}
```

```text
已存在: is_open=0 fail=1
新文件: is_open=1 fail=0
```

Previously, implementing this required checking with `std::filesystem::exists()` first, but "check-then-open" introduces a TOCTOU (time-of-check to time-of-use) race condition—someone else might create the file between the check and the open. `noreplace` turns "create only if not exists" into an atomic `open(2)` operation (underlyingly `O_EXCL | O_CREAT`), eliminating the race condition at the root. For scenarios like writing configuration files, PID files, or lock files where "never overwrite" is required, using this is the most robust approach starting with C++23.

## RAII: Destruction is close, but don't fight with manual close

`<fstream>` is a textbook example of RAII. When the file stream object destructs, the underlying file is automatically closed—you almost never need to manually call `close()`. This means a classic piece of C code:

```cpp
// Standard: C++11
std::ofstream out("config.txt");   // 构造时打开
out << "key=value\n";
// 作用域结束自动 close，哪怕中间抛异常也关
```

The constructor accepts a filename directly, eliminating the long C-style sequence of "first `fopen`, check for null, perform operations, and finally `fclose`". Furthermore, it is exception safe: as long as the object is constructed successfully, the destructor guarantees the file will be closed, regardless of what happens within the scope.

::: warning Manual `close` followed by writes results in data loss
While RAII's automatic `close` is beneficial, if you explicitly call `close()` and then continue writing to the object, the behavior becomes subtle. After `close()`, the stream enters an "unopened" state, and subsequent write operations will be silently discarded:

```cpp
// Standard: C++11
std::ofstream out("/tmp/close_use.txt");
out << "first";
out.close();
out << "second";   // 写给一个已关闭的流
std::cout << "fail=" << out.fail() << " bad=" << out.bad() << "\n";
```

```text
fail=1 bad=1
```

Let's check `/tmp/close_use.txt` on disk. It contains **only `first`**, while `second` has vanished without a trace. Both `failbit` and `badbit` are set, yet the program reports no error and throws no exception; it just silently swallowed the failure.

Therefore, the principle is: **either delegate the lifecycle to RAII (let the object handle close), or if you manually close it, do not touch that object again**. If you insist on reusing the same variable, you must explicitly reopen it with `out.open("...")`, and if necessary, call `out.clear()` first to clear the error flags. Mixing both mechanisms—manually closing while expecting RAII to handle it—is a breeding ground for data loss.
:::

Scenarios where you need to manually call `close()` are actually rare. The main one is when you hold a stream in a long-lived object and want to **proactively** confirm that data has been successfully flushed before destruction. Since destructors cannot throw exceptions (otherwise `std::terminate` is called), if a flush fails during close (e.g., disk full), the destructor has no choice but to swallow the error. However, after manually calling `close()`, you can check `fail()` and actively report the issue. Therefore, "open-write-manual close-check" is a valid pattern for "I must know if this write succeeded," but remember not to use the stream after closing it.

## Error Checking: Always Report Open Failures, Don't Continue Silently

Stream objects have a state bit mechanism: `goodbit`, `failbit`, `badbit`, and `eofbit`. For daily use, you only need to remember two query paths:

- For overall health, use `if (!stream)` or `if (stream)`—this is equivalent to `!fail()`. It evaluates to true if either `failbit` or `badbit` is set.
- To check for end-of-file, use `eof()`—this is only set when "a read attempt is made but no more data is available." **Do not** use it as the sole condition for a loop termination.

The most important habit to form is: **check immediately after opening a file**. Open failures are the most common runtime errors (wrong path, insufficient permissions, file not found), but by default, they do not throw exceptions or report errors. If you don't check and continue silently, all subsequent read/write operations will fail. The program will output garbage, and you won't be able to figure out why. The following code is a counter-example:

```cpp
// Standard: C++11
std::ifstream bad("/tmp/does_not_exist_xyz.txt");
int x = 42;
bad >> x;   // 打开失败，读也失败，x 原封不动
std::cout << "没检查打开: x=" << x << " fail=" << bad.fail() << "\n";
```

```text
没检查打开: x=42 fail=1
```

`x` remains 42, which looks like "we read 42," but in reality, we read nothing and the initial value was preserved. This kind of bug can be incredibly frustrating in production. The correct approach is to check `is_open()` or `!stream` immediately after construction:

```cpp
// Standard: C++11
std::ifstream in("data.bin", std::ios::binary);
if (!in.is_open()) {                       // 或 if (!in)
    std::cerr << "无法打开 data.bin\n";
    return 1;                              // 早退，别硬撑
}
```

`is_open()` is more precise than `!fail()`—it simply asks "is the file actually open?", without mixing in other error states. It is best used during the opening phase.

There is also a classic pitfall regarding `eof()`: using `while (!in.eof())` as the read loop termination condition will almost certainly result in an extra read. This is because the `eofbit` is set only **after a read operation attempts to go past the end**, not when the last valid byte is read. Consequently, at the end of the loop, you will read a failed result and mistake it for valid data. The correct approach is to put the read operation itself into the loop condition:

```cpp
// Standard: C++11
while (in >> x) {        // 读成功才进循环体，读到 EOF/失败自动退出
    use(x);
}
```

`in >> x` returns a reference to the stream itself. In a boolean context, this invokes `operator bool` (equivalent to `!fail()`), so the loop exits cleanly upon reaching EOF or encountering an error. This rule applies equally to `std::getline`: `while (std::getline(in, line))`.

## Text Mode vs. Binary Mode: A Disaster Caused by a Single `\r`

That `binary` flag in the open mode is a design relic from the C era, and it is the source of cross-platform pitfalls for fstream.

The key point is: **In text mode, the platform performs newline translation**. On Windows, the `\n` in your program becomes `\r\n` when written out, and turns back into `\n` when read back in; on Linux/macOS, `\n` remains `\n` with no translation. While this translation is beneficial for plain text files (adhering to platform conventions), it is **catastrophic for any data that is not plain text**.

We tested this locally on Linux. The same string containing newlines resulted in identical byte lengths whether written in text mode or binary mode:

```cpp
// Standard: C++17
const std::string data = "line1\nline2\nline3\n";
{ std::ofstream out("/tmp/text.txt");            out << data; }   // 文本模式
{ std::ofstream out("/tmp/bin.txt", std::ios::binary); out << data; }   // 二进制
std::ifstream t("/tmp/text.txt", std::ios::binary | std::ios::ate);
std::ifstream b("/tmp/bin.txt",  std::ios::binary | std::ios::ate);
std::cout << "text:   " << t.tellg() << " bytes\n";
std::cout << "binary: " << b.tellg() << " bytes\n";
```

```text
text:   18 bytes
binary: 18 bytes
```

On Linux, the two are identical (18 bytes), because Linux performs no translation. However, moving the same code to Windows causes `text.txt` to become 21 bytes (three `\n` characters are each translated to `\r\n`, adding 3 bytes), while `bin.txt` remains 18 bytes. This "cross-platform inconsistency" is the fundamental risk of using text mode.

A more subtle pitfall lies in binary I/O: text mode translation **corrupts your carefully calculated byte offsets**. If you `seekg(100)` to a specific position, that offset in text mode might not correspond to the actual byte position (because translation alters the byte count), and the value returned by `tellg()` will no longer be the true file position. Therefore, in any scenario involving `read`, `write`, or `seek`, always use `binary`. The rule of thumb is simple: **if the data is anything other than human-readable plain text, add `binary`.**

## Binary I/O: `read` / `write` and `char` Buffers

`ifstream::read` and `ofstream::write` operate on **bytes**, and their signatures only accept `char*`. To write an `int`, a `double`, or a custom data structure, we must take the address, cast it to `char*`, and specify the byte count:

```cpp
// Standard: C++11
std::int32_t n = 42;
double d = 3.14;
std::ofstream out("nums.bin", std::ios::binary);
out.write(reinterpret_cast<const char*>(&n), sizeof(n));
out.write(reinterpret_cast<const char*>(&d), sizeof(d));
```

This `reinterpret_cast<const char*>` is essentially a standard trick in C++ binary I/O—it doesn't change the bytes, it simply fools the type system into treating them as a byte stream. Reading it back is the reverse:

```cpp
// Standard: C++11
std::int32_t n;
double d;
std::ifstream in("nums.bin", std::ios::binary);
in.read(reinterpret_cast<char*>(&n), sizeof(n));
in.read(reinterpret_cast<char*>(&d), sizeof(d));
```

You are responsible for ensuring type safety: if you write an `int32_t`, you must read it back as an `int32_t`. You cannot rely on `int` being 4 bytes on one machine and 8 bytes on another and expect it to match. Therefore, **always use fixed-width integers** (like `int32_t` or `uint64_t` from `<cstdint>`) in binary formats, and avoid bare `int` or `long`.

## Directly `write`ing structs: The most tempting and dangerous approach

Writing `int` or `double` is fine, but the real temptation arises with structs: can't we just `reinterpret_cast` a struct to a `char*` and write it out in one go? After all, it's just a contiguous block of memory.

```cpp
// Standard: C++17  —— 危险写法，别在生产里用
struct Record {
    char name[8];
    std::int32_t id;
    double score;
};
out.write(reinterpret_cast<const char*>(&rec), sizeof(Record));
```

It compiles and runs, and reads/writes are consistent on **the same compiler and the same machine**. However, this code contains portability time bombs at three independent levels. Let's examine them one by one.

The first is **endianness**. The value 42 for `int32_t` is stored in memory as `2A 00 00 00` on little-endian machines (x86, ARM by default), and `00 00 00 2A` on big-endian machines. Writing memory bytes directly to a file is equivalent to dumping the "machine's internal representation" to disk as-is. If a file written by a little-endian machine is read by a big-endian machine, the `id` will not be 42, but `0x2A000000` (704643072). Reading and writing between x86 devices or between ARM devices works fine, but as soon as a big-endian device is introduced (certain network equipment, older PowerPC), it breaks.

The second is **padding**. To satisfy memory alignment requirements, C++ inserts padding bytes between structure members and at the end. However, padding is **implementation-defined**—different compilers and platforms may insert it differently. Let's verify this with measurements on our local machine:

```cpp
// Standard: C++17
struct Record {
    char name[8];       // 8 字节，偏移 0
    std::int32_t id;    // 4 字节
    double score;       // 8 字节，要 8 字节对齐
};
std::cout << "sizeof(Record) = " << sizeof(Record) << "\n";
std::cout << "offsetof id    = " << offsetof(Record, id) << "\n";
std::cout << "offsetof score = " << offsetof(Record, score) << "\n";
std::cout << "alignof(Record)= " << alignof(Record) << "\n";
```

```text
sizeof(Record) = 24
offsetof id    = 8
offsetof score = 16
alignof(Record)= 8
```

The members add up to `8 + 4 + 8 = 20` bytes, yet `sizeof` reports 24. Where did the extra 4 bytes go? Since `score` is a `double`, it requires 8-byte alignment. However, the data preceding it is `name(8) + id(4) = 12` bytes, which is not a multiple of 8. Consequently, the compiler **inserted 4 bytes of padding** after `id` to push `score` to offset 16 (a multiple of 8). Additionally, the struct's overall alignment is 8 (determined by the largest member, `score`), so the total size must be a multiple of 8. `20 + 4(padding) = 24` fits perfectly.

The problem is: **what is inside those 4 bytes of padding? It is undefined.** In many implementations, this is uninitialized memory garbage. If you write the same `Record` instance twice, those 4 padding bytes in the file might be completely different—identical data producing different byte sequences. When reading this data elsewhere, ignoring the padding bits might be fine, but if you change compilers or compiler options (like `-fpack-struct`), the padding strategy changes and fields will become misaligned.

The third issue is **type copyability**. The `reinterpret_cast` to `char*` followed by `write` is only safe for **trivially copyable** types. Once a struct contains `std::string`, `std::vector`, virtual functions, or pointers, this approach completely collapses—you are writing out pointer values and internal state. If another process reads this back, the memory those pointers referenced is long gone, leading to a segmentation fault upon dereferencing. Compilers may warn about this usage, but not in all cases.

::: warning Don't do this in production
Directly `write`-ing a struct as a "memory dump" is only valid under a very narrow set of conditions: same compiler, same platform, same endianness, same alignment options, the type must be trivially copyable, and you must not care about the contents of padding bytes. If any one of these conditions breaks, the file becomes unreadable.

For proper binary file formats, choose one of three approaches:

1. **Serialization**: `write` each field individually with a defined width. Define your own endianness (use big-endian for network formats) and manage padding yourself (writing fields individually eliminates padding issues). The cost is verbosity, but it offers total control and portability.
2. **Use an existing serialization library**: protobuf, FlatBuffers, or JSON/YAML (text-based and cross-language friendly). Offload the dirty work of "endianness, padding, and version evolution" to the library.
3. **Text formats**: If data volume is low and human readability is desired, write text directly (CSV / JSON / key=value). Use `from_chars` / `to_chars` discussed in the previous `charconv` article for number conversion. This is the most robust and portable method.

There is only one scenario where memory dumps are barely acceptable: **purely internal, temporary, local, single-process** cache files (e.g., intermediate results of a calculation where the same process writes and reads, and the data never leaves the machine). Even then, adding a magic number + version header is better than writing raw data.
:::

## Positioning: seekg / seekp / tellg / tellp

For reading, we use `seekg` (get, read pointer) and `tellg`; for writing, we use `seekp` (put, write pointer) and `tellp`. When an `fstream` is used for both reading and writing, these two pointers may be separate, but in most implementations, they share a single position. The usage is straightforward:

```cpp
// Standard: C++17
{ std::ofstream out("/tmp/seek.txt", std::ios::binary); out << "ABCDE"; }
std::fstream f("/tmp/seek.txt", std::ios::in | std::ios::out | std::ios::binary);
std::cout << "tellg(开头): " << f.tellg() << "\n";      // 0
char c;
f.get(c);
std::cout << "读到: " << c << " tellg: " << f.tellg() << "\n";   // A, 1
f.seekg(2);
f.get(c);
std::cout << "seekg(2) 后: " << c << "\n";               // C
f.seekp(0);
f.put('X');                                             // 覆盖偏移 0
f.seekg(0);
std::string s; std::getline(f, s);
std::cout << "put X 后: " << s << "\n";                  // XBCDE
```

```text
tellg(开头): 0
读到: A tellg: 1
seekg(2) 后: C
put X 后: XBCDE
```

`seekg` also has an overload that accepts a direction: `seekg(offset, dir)`, where `dir` can be `beg` (beginning), `cur` (current), or `end` (end). To get the file size, the classic trick is to first call `seekg(0, end)` followed by `tellg()`:

```cpp
// Standard: C++17
std::ifstream in("file.bin", std::ios::binary | std::ios::ate);
auto size = in.tellg();     // 已经 ate 了，直接读
in.seekg(0);                // 别忘了读之前挪回开头
```

Constructing with `ate` directly is a more concise approach—opening the file positions the cursor at the end, so a subsequent `tellg()` yields the file size. Note that the warning regarding `ate` mentioned above applies specifically to **writing** (`ofstream` implies `trunc`); using `ate` with `ifstream` avoids this pitfall, as the `in` mode does not truncate the file.

There is another real pitfall: **you cannot seek on "non-random-access" streams**. Devices like pipes, terminals, and sockets are "streaming" and have no concept of "position." Calling `seekg` or `tellg` on them will fail and set the `failbit`. Only random-access sources like regular files and string streams support seeking.

## Performance: fstream isn't slow, unless used incorrectly

`fstream` has a mixed reputation for performance; it is often rumored that "fstream is slower than C's `fread`/`fwrite`." This statement is partially true and partially false, so let's break it down with actual benchmarks.

First, let's look at **large buffer I/O**—reading or writing a large chunk of bytes in a single `read` or `write` operation. We write a 64 MB buffer, then compare writing with `fstream`, reading with stdio `fread`, reading with `fstream`, and reading with `mmap`:

```cpp
// Standard: C++17  （benchmark 摘要，完整代码见文末说明）
static constexpr std::size_t kBytes = 64 * 1024 * 1024;  // 64 MB
// fstream 写
{ std::ofstream out(path, std::ios::binary); out.write(buf.data(), kBytes); }
// stdio 读
{ std::FILE* f = std::fopen(path, "rb"); std::fread(r, 1, kBytes, f); std::fclose(f); }
// fstream 读
{ std::ifstream in(path, std::ios::binary); in.read(r, kBytes); }
// mmap 读
{ int fd = ::open(path, O_RDONLY);
  char* m = static_cast<char*>(::mmap(nullptr, kBytes, PROT_READ, MAP_PRIVATE, fd, 0));
  std::memcpy(r, m, kBytes); ::munmap(m, kBytes); ::close(fd); }
```

Here are the order of magnitude results obtained on the local machine (GCC 16.1.1, libstdc++) (absolute values will fluctuate depending on the machine and cache; focus on the order of magnitude):

```text
file size: 64 MB
fstream 写        :  ~43 ms
stdio fread 读    :  ~28 ms
fstream 读        :  ~26 ms
mmap 读           :  ~22 ms
```

You will find that **`fstream` is not slow at all for bulk reads and writes**; it is on par with `stdio`. This is because `libstdc++`'s `fstream` maintains an internal buffer. Bulk `read`/`write` operations essentially transfer the user buffer directly to the underlying `read(2)`/`write(2)` system calls, making the overhead negligible. `mmap` is slightly faster because it eliminates the "copy to user buffer" step (by mapping file pages directly into the address space), but the savings are limited to that one or two copies.

So where does the reputation of **"`fstream` is slow"** come from? It comes from **formatted, item-by-item I/O**. When you write `out << x << ' '` to push `int` values one by one, or read them via `in >> x`, each operation requires locale-aware formatting or parsing. This is the real bottleneck. In our tests, reading 4 million `int` values (in text format), we compared three approaches:

```text
fstream >> 逐 int  :  ~210 ms
stdio fscanf 逐 int:  ~225 ms
缓冲整块读 + 手写解析: ~59 ms
```

The conclusion is straightforward: **`fstream >>` is just as slow as `fscanf`** (both take the expensive formatting path, with locale processing and error checking overhead), so neither has an advantage over the other. The truly fast approach is the third one—**`read` the entire file into memory at once, then parse it manually**. This brings us back to what we discussed in the `charconv` article: `from_chars` has no locale, no exceptions, and no allocations, making it the fastest path for number parsing.

So, the performance advice boils down to two points:

1. **For binary bulk I/O, feel free to use `fstream`**. You don't need to switch specifically to `fread`/`fwrite`; the performance is on the same order of magnitude.
2. **For batch number reading or text parsing, `read` the whole block into a `std::string` first, then use `from_chars` or a hand-written parser to process items one by one**. This is several times faster than using `>>` or `fscanf`.

As for `mmap`, its advantage isn't absolute speed, but rather **semantics**: it maps the entire file into memory, allowing random access like an array, while the OS handles paging on demand. `mmap` is a powerful tool for handling huge files, achieving zero-copy, or sharing read-only data across multiple processes. However, it turns "read failure" from "returning an error code" into "triggering a SIGSEGV upon memory access," making debugging harder. Be aware of this trade-off before using it. `mmap` is a POSIX standard; on Windows, the equivalent is `CreateFileMapping`, so you need an abstraction layer for cross-platform code—which is why many people still stick with `fstream` for simplicity.

## Working with `std::filesystem::path` (Since C++17)

C++17 added a `std::filesystem::path` overload to file stream constructors. This means you can pass a `path` object directly to `ifstream` / `ofstream` without having to convert it to a string via `.string()` first:

```cpp
// Standard: C++17
#include <fstream>
#include <filesystem>

std::filesystem::path p =
    std::filesystem::temp_directory_path() / "fstream_path_demo.txt";
{
    std::ofstream out(p);   // 直接接 path
    out << "hello from filesystem::path overload\n";
}
std::ifstream in(p);
std::string line;
std::getline(in, line);
std::cout << "read back: " << line << "\n";
std::cout << "path: " << p << "\n";
```

```text
read back: hello from filesystem::path overload
path: "/tmp/fstream_path_demo.txt"
```

The value of this overload lies in cross-platform compatibility, especially on Windows: `std::filesystem::path` uses native encoding internally (wide characters `wchar_t` on Windows). Passing it directly to fstream correctly opens files with non-ASCII characters in their names. If you first convert it to a narrow string using `.string()`, you may fail to open files with Chinese or Japanese names on Windows. Therefore, delegating path-related operations uniformly to `<filesystem>` and passing `path` objects directly to fstream is the cleanest, most cross-platform way to write code since C++17. We leave path concatenation, traversal, and normalization to `filesystem`, which we will cover in the next article.

## Summary

The core of `<fstream>` isn't just a pile of APIs, but several design decisions and the pitfalls they bring. Let's recap the key takeaways:

- **Three stream types + open mode**: `ifstream` for reading, `ofstream` for writing (defaults to `trunc` which clears content!), and `fstream` requires specifying `in|out`; `app` for appending, `ate` to seek to the end upon opening, `binary` to disable newline translation, and C++23's `noreplace` to atomically create only new files.
- **RAII manages lifetime**: The destructor automatically closes the file, ensuring exception safety; however, **do not write after manually calling `close()`** (data will be silently swallowed). To reuse the variable, call `open()` again.
- **Always check for open failures**: Use `is_open()` or `!stream` to check immediately; otherwise, subsequent reads and writes will fail silently. For read loops, use `while (in >> x)` which places the read operation in the condition; avoid using `while (!in.eof())`.
- **Always add `binary` for binary data**: Text mode translates newlines (Windows `\n` to `\r\n`), corrupts byte offsets, and makes `seek`/`tell` unreliable; `read`/`write` only recognize `char*`, so use fixed-width integers from `<cstdint>`.
- **Directly `write`-ing structs is a pitfall**: Byte order, padding, and trivially copyable requirements are three hurdles; if any aren't met, you won't be able to read the data back. In production, use field-by-field serialization, existing serialization libraries, or text formats.
- **Performance**: Large block binary I/O with fstream is not slow (on par with stdio); what is slow is formatted item-by-item `>>`/`fscanf`. For batch scenarios, reading the whole block and parsing with `from_chars` is several times faster. For very large files, zero-copy, or multi-process sharing, consider `mmap`.
- **Since C++17, fstream directly accepts `std::filesystem::path`**, which is the most stable approach for cross-platform development (especially for Windows non-ASCII filenames). Path operation details are left to `filesystem`.

In the next article, we will turn our attention to `<filesystem>`: operations at the "filesystem" level like path concatenation, directory traversal, and file attribute queries, and see how `std::filesystem::path` integrates seamlessly with the file streams discussed here.

## References

- [cppreference: `<fstream>`](https://en.cppreference.com/w/cpp/header/fstream) — Overview of the three file stream types and open modes
- [cppreference: std::filebuf::open](https://en.cppreference.com/w/cpp/io/basic_filebuf/open) — Authoritative description of open modes (including C++23 `noreplace`)
- [cppreference: std::basic_ifstream](https://en.cppreference.com/w/cpp/io/basic_ifstream) — Constructors, including the `std::filesystem::path` overload (C++17)
- [cppreference: `std::ios_base::openmode`](https://en.cppreference.com/w/cpp/io/ios_base/openmode) — Semantics of various open mode flags
- [cppreference: `std::fstream` C++23 `noreplace`](https://en.cppreference.com/w/cpp/io/ios_base/openmode) — The `noreplace` flag introduced by P2467
