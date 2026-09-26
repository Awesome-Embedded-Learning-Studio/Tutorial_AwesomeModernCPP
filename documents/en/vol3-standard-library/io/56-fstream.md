---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: A thorough walkthrough of fstream's three file stream classes and open modes,
  the lifecycle pitfalls of RAII automatic close, the error state machine, the difference
  between text and binary mode, the cross-platform hazards of writing structs directly,
  and why large-file reading and writing is a case for mmap / stdio
difficulty: intermediate
order: 56
platform: host
prerequisites:
- 'Deep Dive into std::string: SSO, COW, and resize_and_overwrite'
- 'charconv: Zero-Overhead Number-String Conversions'
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New Tricks'
reading_time_minutes: 16
related:
- 'charconv: Zero-Overhead Number-String Conversions'
- 'Deep Dive into std::string: SSO, COW, and resize_and_overwrite'
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
  translated_at: '2026-09-26T00:41:03+00:00'
  engine: anthropic
  token_count: 13000
---

# fstream: File Stream I/O, RAII, and Its Portability Pitfalls

Persisting a chunk of data to disk, then reading a config file back—almost no C++ program escapes this chore. The standard library's answer is `<fstream>`: `ifstream` for reading, `ofstream` for writing, `fstream` for both, with RAII quietly managing the file descriptor's lifecycle underneath.

It looks like a simple "open, read/write, close" affair, but once you actually write the code you discover a pile of counterintuitive corners. Why does the same code behave on Linux, then sprout a bunch of stray `\r`s in the text file on Windows? Why does the struct I `write` out come back from `read` on another machine with every field misaligned? Why does the program carry on without a word when the open fails? Why does data written after `close()` simply vanish? The roots of these pits lie in one of three places: the "text/binary" duality inherited from C, the indeterminacy of struct memory layout, or the stream machinery's "state bits + automatic reset" design.

In this article we take `<fstream>` apart from the inside out. The focus is not on listing every API, but on making three things clear: **what the mode flags actually change, what RAII does and does not manage for you, and when you shouldn't use it at all**. Every example was actually run on local GCC 16.1.1, with the output pasted verbatim.

## The Three Stream Classes and open Modes: Know What Each Flag Does

`<fstream>` hands you three classes—essentially the same machinery pointed in different "directions":

- `std::ifstream` — read-only by default (implies `std::ios::in`);
- `std::ofstream` — write-only by default, and **opening empties the file** (implies `std::ios::out | std::ios::trunc`);
- `std::fstream` — reads and writes both, but **implies no direction on your behalf**—you have to write `in | out` yourself.

What actually determines behavior is that string of open mode flags. They click into place once you remember them as "actions performed on the file at open time":

| Flag | What it does | One-line mnemonic |
|------|--------|-----------|
| `in` | Read | Required; `ifstream` adds it automatically |
| `out` | Write | Required; `ofstream` adds it automatically |
| `trunc` | Empties the file on open | Implied by `ofstream` by default; the biggest footgun |
| `app` | Jumps to the end of the file before every write | Appending logs |
| `ate` | Positions at the end after opening (once) | You want to know how long the file is |
| `binary` | Disables platform-specific character translation | Mandatory for binary data |
| `noreplace` (C++23) | Refuses to open if the file already exists | Safely "create only new files" |

The easiest one to step on here is `trunc`. Many newcomers read the code below as "open an existing file and write into it"—and the moment it opens, the old content is gone:

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
    { std::ofstream out("/tmp/m.txt"); out << "new"; }  // trunc by default
    std::cout << "trunc (默认 ofstream): " << read_all("/tmp/m.txt") << "\n";
    return 0;
}
```

```text
trunc (默认 ofstream): new|
```

`OLD` is gone. To keep the old content and continue writing at the end, you have to add `app` explicitly:

```cpp
// Standard: C++17
{ std::ofstream out("/tmp/m.txt", std::ios::app); out << "APPENDED"; }
```

```text
after app: newAPPENDED|
```

`app`'s semantics go further than "seek to the end": it forces the write pointer to the end of the file **before every write operation**, no matter where you had `seekp`'d beforehand. Which is exactly the behavior log appending wants—multiple threads each writing their own additions, never trampling each other's data regions.

::: warning ate won't save you—trunc still empties the file
A particularly common misconception: that `std::ios::ate` (position at the end after opening) preserves the old content. **It does not.** `ate` merely "moves the pointer to the end after opening"; it does **not cancel** the `trunc` that `ofstream` implies by default. We verified this on the local machine:

```cpp
{ std::ofstream out("/tmp/a.txt"); out << "0123456789"; }   // write 10 bytes first
{ std::ofstream out("/tmp/a.txt", std::ios::ate);           // hoping to preserve? no luck
  std::cout << "ate tellp=" << out.tellp() << "\n";
  out << "XY"; }
```

```text
ate tellp=0
```

`tellp` is 0, not 10—meaning the file had already been emptied by `trunc`, and `ate` moved to the end of an empty file (that is, position 0). In the end, only `XY` is on disk; `0123456789` is gone.

To get "preserve the old content, position at the end on open, and still be free to `seekp` around", the combination has to be `in | out | ate`:

```cpp
// Standard: C++17
{ std::fstream f("/tmp/a.txt", std::ios::in | std::ios::out | std::ios::ate);
  std::cout << "in|out|ate tellp=" << f.tellp() << "\n";   // 10 — the content survived
  f << "XY"; }
```

```text
in|out|ate tellp=10
content: 0123456789XY
```

If you can't memorize all that, remember one line: **opening an existing file with `ofstream` truncates it by default**. To preserve the content, you need `app` or `in | out`.
:::

As for the C++23 newcomer `noreplace`, the name says it all—if the file already exists, the open is refused. It exists precisely to safely "create only new files, never overwrite". Our local GCC 16.1.1 already supports it:

```cpp
// Standard: C++23
#include <fstream>
#include <iostream>

int main() {
    { std::ofstream out("/tmp/np.txt"); out << "original"; }
    // file already exists -> noreplace refuses to open
    std::ofstream a("/tmp/np.txt", std::ios::out | std::ios::noreplace);
    std::cout << "已存在: is_open=" << a.is_open() << " fail=" << a.fail() << "\n";
    // file doesn't exist -> created normally
    std::ofstream b("/tmp/np_new.txt", std::ios::out | std::ios::noreplace);
    std::cout << "新文件: is_open=" << b.is_open() << " fail=" << b.fail() << "\n";
    return 0;
}
```

```text
已存在: is_open=0 fail=1
新文件: is_open=1 fail=0
```

Before this, implementing that semantics meant checking first with `std::filesystem::exists()`—but "check-then-open" has a TOCTOU (time-of-check to time-of-use) race: between your check and your open, someone else may have created the file. `noreplace` turns "create only if absent" into an atomic `open(2)` operation (under the hood it is `O_EXCL | O_CREAT`), eliminating the race at the root. For "never overwrite" scenarios—config files, PID files, lock files—using it is the safest option from C++23 onward.

## RAII: The Destructor Closes the File—Just Don't Fight the Manual close

`<fstream>` is a textbook application of RAII. When a file stream object is destroyed, the underlying file is closed automatically—you almost never need to call `close()` yourself. This replaces a stretch of classic C code:

```cpp
// Standard: C++11
std::ofstream out("config.txt");   // opens at construction
out << "key=value\n";
// scope ends -> automatic close, even if an exception was thrown along the way
```

The constructor takes the filename and opens it directly, sparing you the whole C-era incantation of "`fopen` first, check for NULL, operate, `fclose` at the end". And it is exception-safe: once the object has been constructed, whatever happens inside the scope, the destructor is the safety net that closes the file.

::: warning Writing after a manual close silently loses the data
RAII's automatic close is a good thing, but if **you yourself** call `close()` and then keep writing to that same object, things get delicate. After `close()` the stream is in a "not open" state, and subsequent writes are silently dropped:

```cpp
// Standard: C++11
std::ofstream out("/tmp/close_use.txt");
out << "first";
out.close();
out << "second";   // writing to an already-closed stream
std::cout << "fail=" << out.fail() << " bad=" << out.bad() << "\n";
```

```text
fail=1 bad=1
```

Go check `/tmp/close_use.txt` on disk: it contains **only `first`**—`second` has evaporated. Both `failbit` and `badbit` are set, yet the program raises no error and no exception; it just quietly swallows the loss.

So the rule is: **either hand the lifecycle to RAII (the object manages close), or once you have closed manually, never touch that object again**. If you insist on reusing the same variable, reopen it explicitly with `out.open("...")`, clearing the error bits first with `out.clear()` if necessary. Mixing the two regimes—closing by hand while still counting on RAII—is a breeding ground for data loss.
:::

Scenarios that genuinely need a manual `close()` are rare; the main one: you hold the stream inside a long-lived object and want to **actively** confirm the data reached disk before destruction. Destructors cannot throw (that would be `std::terminate`), so if the flush during close fails (disk full, say), the destructor can only swallow the error; with a manual `close()` you can check `fail()` and report it yourself. So "open–write–close manually–check" is a legitimate pattern for "I must know whether this write succeeded"—just remember not to use the stream again after the close.

## Error Checking: Always Report a Failed Open, Never Carry On Silently

Stream objects carry a state-bit machinery: `goodbit` / `failbit` / `badbit` / `eofbit`. For daily use, only two query paths are worth memorizing:

- Overall health via `if (!stream)` or `if (stream)`—equivalent to `!fail()`; true whenever either `failbit` or `badbit` is set.
- End of file via `eof()`—set only when "a read was attempted but nothing more could be read"; do **not** use it as a loop's sole termination condition.

The habit most worth building: **check immediately after opening a file**. A failed open is the most common runtime error (wrong path, insufficient permissions, missing file), yet by default it throws nothing and reports nothing—skip the check and the program carries on in silence, every subsequent read and write fails, and you get a pile of garbage output with no clue why. This next snippet is the counterexample:

```cpp
// Standard: C++11
std::ifstream bad("/tmp/does_not_exist_xyz.txt");
int x = 42;
bad >> x;   // open failed, so the read fails too; x stays untouched
std::cout << "没检查打开: x=" << x << " fail=" << bad.fail() << "\n";
```

```text
没检查打开: x=42 fail=1
```

`x` is still 42. It looks like "we read a 42", but in fact nothing was ever read and the initial value survived. In production, this kind of bug drives people mad. The correct approach is to check `is_open()` or `!stream` immediately after construction:

```cpp
// Standard: C++11
std::ifstream in("data.bin", std::ios::binary);
if (!in.is_open()) {                       // or: if (!in)
    std::cerr << "无法打开 data.bin\n";
    return 1;                              // bail out early, don't soldier on
}
```

`is_open()` is more precise than `!fail()`—it asks only "is the file actually open", without mixing in the other error states. At open time, it is the right tool.

`eof()` has a classic pitfall of its own: using `while (!in.eof())` as a read loop's termination condition almost always reads one time too many. `eofbit` is set only **after a read operation attempts to go past the end**—not when the last valid byte is read. So at the end of the loop you get a failed result and mistake it for valid data. The correct form puts the read operation itself into the loop condition:

```cpp
// Standard: C++11
while (in >> x) {        // loop body runs only on a successful read; EOF/failure exits automatically
    use(x);
}
```

`in >> x` returns a reference to the stream itself, which in a boolean context goes through `operator bool` (equivalent to `!fail()`); on EOF or error the loop exits—clean and tidy. The same rule holds for `std::getline`: `while (std::getline(in, line))`.

## Text Mode vs Binary Mode: The Bloody Mess One `\r` Causes

That `binary` flag among the open modes is a design left over from the C era, and it is the wellspring of fstream's cross-platform pitfalls.

Here is the crux: **in text mode, the platform translates newlines**. On Windows, a `\n` inside your program becomes `\r\n` on the way out and turns back into `\n` on the way in; on Linux/macOS, `\n` is just `\n`, no translation. For pure text files this translation is a good thing (it matches each platform's newline conventions), but for **any data that is not pure text, it is a disaster**.

We tested on the local machine (Linux): the same newline-bearing string, written once in text mode and once in binary mode, comes out with identical byte lengths:

```cpp
// Standard: C++17
const std::string data = "line1\nline2\nline3\n";
{ std::ofstream out("/tmp/text.txt");            out << data; }   // text mode
{ std::ofstream out("/tmp/bin.txt", std::ios::binary); out << data; }   // binary
std::ifstream t("/tmp/text.txt", std::ios::binary | std::ios::ate);
std::ifstream b("/tmp/bin.txt",  std::ios::binary | std::ios::ate);
std::cout << "text:   " << t.tellg() << " bytes\n";
std::cout << "binary: " << b.tellg() << " bytes\n";
```

```text
text:   18 bytes
binary: 18 bytes
```

On Linux the two are identical (18 bytes), because Linux does no translation at all. Move the same code to Windows, though, and `text.txt` becomes 21 bytes (each of the three `\n`s is translated to `\r\n`, adding 3 bytes), while `bin.txt` stays at 18. This "cross-platform behavioral divergence" is the essential risk of text mode.

The sneakier pitfall sits in binary reading and writing: text-mode translation **wrecks your carefully computed byte offsets**. You `seekg(100)` to some position; in text mode that offset may not line up with the actual bytes (translation changed the byte count), and the value `tellg()` returns is no longer the file's true byte position. So whenever `read` / `write` / `seek` work together, use `binary`, full stop. The rule of thumb in one line: **if the data is not plain text for humans to read, add `binary`**.

## Binary Reading and Writing: `read` / `write` and char Buffers

`ifstream::read` and `ofstream::write` operate on **bytes**, and their signatures accept only `char*`. To write an `int`, a `double`, or a blob of custom data, you first take its address, cast it to `char*`, and pair it with a byte count:

```cpp
// Standard: C++11
std::int32_t n = 42;
double d = 3.14;
std::ofstream out("nums.bin", std::ios::binary);
out.write(reinterpret_cast<const char*>(&n), sizeof(n));
out.write(reinterpret_cast<const char*>(&d), sizeof(d));
```

This `reinterpret_cast<const char*>` is practically the fixed trick of C++ binary I/O—it changes no bytes; it merely fibs to the type system so the data is handled byte-wise. Reading back is the mirror image:

```cpp
// Standard: C++11
std::int32_t n;
double d;
std::ifstream in("nums.bin", std::ios::binary);
in.read(reinterpret_cast<char*>(&n), sizeof(n));
in.read(reinterpret_cast<char*>(&d), sizeof(d));
```

Type safety is on you: what went in as an `int32_t` must come back out as an `int32_t`. You cannot have `int` be 4 bytes on one machine and 8 on another and still expect things to line up. So in binary formats, **always use fixed-width integers** (the `int32_t` / `uint64_t` family from `<cstdint>`), never bare `int` / `long`.

## Writing a Struct Directly with `write`: The Most Tempting, Most Treacherous Approach

Writing an `int` / `double` is one thing; the temptation is this: can a struct also be `reinterpret_cast` to `char*` and blasted out in one go? After all, it is just a stretch of contiguous memory.

```cpp
// Standard: C++17  — dangerous; do not use in production
struct Record {
    char name[8];
    std::int32_t id;
    double score;
};
out.write(reinterpret_cast<const char*>(&rec), sizeof(Record));
```

It compiles, it runs, and reading back is self-consistent **on the same compiler and the same machine**. But this code packs three independent portability bombs; let's take them one at a time.

The first is **endianness**. The `int32_t` value 42 lives in memory as `2A 00 00 00` on little-endian machines (x86, and ARM in its default little-endian setup), and as `00 00 00 2A` on big-endian ones. Writing raw memory bytes into the file means persisting the "machine's internal representation" verbatim. A file written by a little-endian machine, read back on a big-endian one, yields an `id` that is no longer 42 but `0x2A000000` = 704643072. x86-to-x86 and ARM-to-ARM reads are fine; mix in one big-endian device (certain network equipment, older PowerPC) and it falls apart.

The second is **padding**. For memory alignment, C++ inserts padding bytes between struct members and at the end, and padding is **implementation-defined**—different compilers, different platforms, possibly different arrangements. Here is what we measured on the local machine:

```cpp
// Standard: C++17
struct Record {
    char name[8];       // 8 bytes, offset 0
    std::int32_t id;    // 4 bytes
    double score;       // 8 bytes, needs 8-byte alignment
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

The members add up to `8 + 4 + 8 = 20` bytes, yet `sizeof` says 24. Where did the extra 4 bytes go? `score` is a `double` and wants 8-byte alignment, but it is preceded by `name(8) + id(4) = 12` bytes—not a multiple of 8. So the compiler **stuffs 4 padding bytes after `id`**, pushing `score` to offset 16 (a multiple of 8); the struct's overall alignment is likewise 8 (taken from the largest member, `score`), so the total length must round up to a multiple of 8, and `20 + 4 (padding inside) = 24` fits perfectly.

The problem: **what sits inside those 4 padding bytes? Undefined.** In many implementations it is uninitialized memory garbage. Write the same `Record` out twice, and the 4 padding bytes in the file may come out completely different—the same data, two different byte sequences on disk. If the reader ignores the padding slots, no harm; but one day you switch compilers or flags (say `-fpack-struct`), the padding arrangement changes, and every field lands in the wrong place.

The third is **the type's own copyability**. The `reinterpret_cast`-to-`char*`-then-`write` routine is safe only for **trivially copyable** types. Once the struct contains a `std::string`, `std::vector`, a virtual function, or a pointer, the whole thing collapses—what you write out are pointer values and piles of internal state; when another process reads it back, the memory those pointers referred to is long gone, and the first dereference is a segfault. Compilers sometimes warn on this usage, but not in every case.

::: warning Don't write this way in production
The "dump a struct's memory image to disk" routine holds only under a narrow set of conditions: same compiler, same platform, same endianness, same alignment options, a trivially copyable type, and total indifference to what fills the padding bytes. Break any one of those conditions, and the file cannot be read back.

For a serious binary file format, pick one of three directions:

1. **Serialize**: `write` each field separately at a fixed width; decide the endianness yourself (network formats use big-endian) and manage padding yourself (writing field by field leaves no padding problem). The cost is verbosity, but everything is controllable and portable.
2. **Use an existing serialization library**: protobuf, FlatBuffers, JSON/YAML (text, friendly across languages). Hand the dirty work—endianness, padding, version evolution—to the library.
3. **A text format**: if the data volume is modest and humans will read it too, just write text (CSV / JSON / key=value), using `from_chars` / `to_chars` from the earlier `charconv` article for number conversion. Steadiest and most portable of all.

Only one scenario makes a bare memory image barely acceptable: a **purely internal, temporary, single-machine, same-process** cache file (say the intermediate result of some computation, written and read by yourself, never leaving this machine). Even then, adding a magic number + version header still beats writing it raw.
:::

## Positioning: seekg / seekp / tellg / tellp

For reading: `seekg` (get, the read pointer) and `tellg`; for writing: `seekp` (put, the write pointer) and `tellp`. When a `fstream` reads and writes simultaneously the two pointers may be separate, but in most implementations they share one position. The usage is straightforward:

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
f.put('X');                                             // overwrite offset 0
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

`seekg` also has a direction-aware overload, `seekg(offset, dir)`, where `dir` is `beg` (beginning) / `cur` (current) / `end` (end). The classic trick for getting a file's size is `seekg(0, end)` followed by `tellg()`:

```cpp
// Standard: C++17
std::ifstream in("file.bin", std::ios::binary | std::ios::ate);
auto size = in.tellg();     // already at the end thanks to ate, read directly
in.seekg(0);                // don't forget to rewind before reading
```

Passing `ate` at construction is the tidier spelling—open and land at the end in one motion, and the `tellg()` right after is the file size. Note that the `ate` warning above targets **writing** (`ofstream` implies `trunc`); an `ifstream` opened with `ate` has no such pitfall—`in` never empties the file.

One more genuine pitfall: **not every stream can be seeked—the "non-random-access" ones can't**. Pipes, terminals, sockets—these "streaming" devices have no notion of a "position"; `seekg` / `tellg` on them fails and sets `failbit`. Only random-access-capable things like regular files and string streams can be seeked.

## Performance: fstream Isn't Slow—Using It Wrong Is

`fstream` has a less-than-stellar performance reputation; the street wisdom goes "fstream is slower than C's `fread`/`fwrite`". That claim is both right and wrong, so let's take it apart with measurements.

First, **large-block buffered reads and writes**—one big `read` / `write` of a fat chunk of bytes. We write a 64 MB buffer, then, respectively: write with fstream, read with stdio `fread`, read with fstream, read with `mmap`:

```cpp
// Standard: C++17  (benchmark excerpt; see the notes at the end of the article for the full code)
static constexpr std::size_t kBytes = 64 * 1024 * 1024;  // 64 MB
// fstream write
{ std::ofstream out(path, std::ios::binary); out.write(buf.data(), kBytes); }
// stdio read
{ std::FILE* f = std::fopen(path, "rb"); std::fread(r, 1, kBytes, f); std::fclose(f); }
// fstream read
{ std::ifstream in(path, std::ios::binary); in.read(r, kBytes); }
// mmap read
{ int fd = ::open(path, O_RDONLY);
  char* m = static_cast<char*>(::mmap(nullptr, kBytes, PROT_READ, MAP_PRIVATE, fd, 0));
  std::memcpy(r, m, kBytes); ::munmap(m, kBytes); ::close(fd); }
```

Orders of magnitude measured on the local machine (GCC 16.1.1, libstdc++; absolute values fluctuate with machine and cache state—watch the order of magnitude):

```text
file size: 64 MB
fstream 写        :  ~43 ms
stdio fread 读    :  ~28 ms
fstream 读        :  ~26 ms
mmap 读           :  ~22 ms
```

Notice: **for large-block reads and writes, fstream is not slow at all**—it sits in the same order of magnitude as stdio. libstdc++'s fstream carries its own internal buffer, and a big `read`/`write` essentially pours the user buffer straight into the underlying `read(2)`/`write(2)`; the overhead is negligible. `mmap` is slightly faster because it even skips the "copy into a user buffer" step (it maps the file's pages directly into the address space)—but all it saves is that one or two copies.

So where does the "fstream is slow" reputation come from? From **formatted item-by-item reads and writes**. When you push `int`s out one at a time with `out << x << ' '`, or read them one at a time with `in >> x`, every single operation passes through a locale-aware format/parse—that is the real bottleneck. Our measurement: reading 4 million `int`s (text format), three approaches compared:

```text
fstream >> 逐 int  :  ~210 ms
stdio fscanf 逐 int:  ~225 ms
缓冲整块读 + 手写解析: ~59 ms
```

The conclusion is blunt: **`fstream >>` is exactly as slow as `fscanf`** (both walk the expensive formatting path, with locale and error checking fully loaded)—neither gets to point fingers. The genuinely fast one is the third: **`read` the entire file into memory in one shot, then parse by hand**. Which loops back to what the `charconv` article covered: `from_chars` carries no locale, no exceptions, no allocations—it is the fastest path for parsing numbers.

So the performance advice boils down to two rules:

1. **For large-block binary I/O, use fstream with confidence**—no need to switch to `fread`/`fwrite`; the order of magnitude is the same.
2. **For batch number reading / text parsing, first `read` the whole block into a `std::string`, then process the items one by one with `from_chars` or hand-written parsing**—several times faster than `>>` / `fscanf`.

As for `mmap`, its edge lies not in absolute speed but in **semantics**: map the whole file into memory, access it randomly as if it were an array, and let the OS page in data on demand. When handling enormous files, wanting zero-copy, or sharing one read-only dataset across multiple processes, `mmap` is a sharp tool. But it turns "read failure" from "the function returns an error code" into "touching the memory raises SIGSEGV"—harder to debug—so know what you are signing up for. `mmap` is POSIX; on Windows the counterpart is `CreateFileMapping`, so cross-platform code needs an abstraction layer—which is exactly why many people still use fstream for convenience.

## Working with `std::filesystem::path` (C++17 Onward)

C++17 added a `std::filesystem::path` overload to the file stream constructors. That means you can feed a `path` object directly to `ifstream` / `ofstream`, without first converting it to a string with `.string()`:

```cpp
// Standard: C++17
#include <fstream>
#include <filesystem>

std::filesystem::path p =
    std::filesystem::temp_directory_path() / "fstream_path_demo.txt";
{
    std::ofstream out(p);   // takes a path directly
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

This overload's value is cross-platform, especially on Windows: `std::filesystem::path` internally uses the native encoding (on Windows, wide-character `wchar_t` paths), so handing it straight to fstream correctly opens files whose names contain non-ASCII characters. If you first convert with `.string()` into a narrow string, a Chinese/Japanese filename on Windows may simply fail to open. So: route all path-related operations through `<filesystem>` and pass the `path` object directly to fstream—since C++17, that is the cleanest and most cross-platform way to write it. The operations on paths themselves—joining, iterating, normalizing—belong to `filesystem`, and we will devote the next article to them.

## Summary

The heart of `<fstream>` is not a pile of APIs; it is a handful of design decisions and the pitfalls they bring. Let's collect the key conclusions:

- **Three stream classes + open modes**: `ifstream` reads, `ofstream` writes (default `trunc` empties the file!), `fstream` needs you to write `in|out` yourself; `app` appends, `ate` positions at the end after opening, `binary` turns off newline translation, and C++23's `noreplace` creates new files atomically.
- **RAII owns the lifecycle**: the destructor closes automatically and is exception-safe; but **after a manual `close()`, don't write again** (the data gets silently swallowed)—to reuse the variable, `open()` it again.
- **Always check for failed opens**: test `is_open()` / `!stream` immediately; otherwise every subsequent read and write fails while the program carries on silently. Write read loops with the read operation in the condition, like `while (in >> x)`—not `while (!in.eof())`.
- **Binary always gets `binary`**: text mode translates newlines (Windows `\n`→`\r\n`), breaks byte offsets, and makes `seek`/`tell` unreliable; `read`/`write` accept only `char*`, and fixed-width numbers come from `<cstdint>`.
- **Writing a struct directly is a trap**: endianness, padding, and trivial copyability are three hurdles—fail any one and the file can't be read back; in production, use field-by-field serialization, an existing serialization library, or a text format.
- **Performance**: for large-block binary I/O, fstream isn't slow (same order of magnitude as stdio); what's slow is formatted item-by-item `>>`/`fscanf`—in batch scenarios, read the whole block first and parse with `from_chars` for a several-fold speedup; consider `mmap` for huge files, zero-copy, or multi-process sharing.
- **Since C++17, fstream takes `std::filesystem::path` directly**—the sturdiest option cross-platform (especially non-ASCII filenames on Windows); the details of path operations belong to `filesystem`.

In the next article we turn the camera to `<filesystem>`: the "filesystem-level" operations such as path joining, directory iteration, and file attribute queries—and how `std::filesystem::path` dovetails with this article's file streams.

## References

- [cppreference: `<fstream>`](https://en.cppreference.com/w/cpp/header/fstream) — an overview of the three file stream classes and open modes
- [cppreference: std::filebuf::open](https://en.cppreference.com/w/cpp/io/basic_filebuf/open) — the authoritative description of open modes (including C++23 `noreplace`)
- [cppreference: std::basic_ifstream](https://en.cppreference.com/w/cpp/io/basic_ifstream) — constructors, including the `std::filesystem::path` overload (C++17)
- [cppreference: `std::ios_base::openmode`](https://en.cppreference.com/w/cpp/io/ios_base/openmode) — the semantics of each open mode flag
- [cppreference: `std::fstream` C++23 `noreplace`](https://en.cppreference.com/w/cpp/io/ios_base/openmode) — the `noreplace` flag introduced by P2467
