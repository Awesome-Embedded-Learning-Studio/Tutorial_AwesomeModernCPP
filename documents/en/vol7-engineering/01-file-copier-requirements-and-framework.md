---
chapter: 1
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 9
tags:
- cpp-modern
- host
- intermediate
title: 'Modern C++ in Practice — Building a File Copier from Scratch (Part 1): Requirements Analysis and Basic Framework'
description: File-copier practice, part 1 - nail down the requirements and stand up the skeleton, turning last chapter's engineering habits into a real small project
translation:
  source: documents/vol7-engineering/01-file-copier-requirements-and-framework.md
  source_hash: 814769f28e09746b9ea21d9a4ea5b19a28046494df3658df53daa8ca122550c2
  translated_at: '2026-09-27T02:28:02+00:00'
  engine: anthropic
  token_count: 5000
---
# Modern C++ in Practice — Building a File Copier from Scratch (Part 1): Requirements Analysis and Basic Framework

## Opening Ramblings

I'm sure everyone has used the `cp` command. This little series is a new hands-on modern C++ exercise I've set out to do.

File copying is probably one of the earliest practical problems you meet in a programmer's career. When you type `cp` in a terminal or drag files around in a GUI, have you ever wondered what actually happens behind the scenes? I still remember how magical it felt the first time I wrote a file copier in C—a few lines of code could move a multi-gigabyte movie from one place to another, even though the code I produced back then was so ugly I could hardly bear to look at it.

Today, let's build a dependable file copier in modern C++. Nothing flashy—it just has to hold up as engineering: all the features it should have, and code that's comfortable to read. More importantly, we'll put a fair number of modern C++ features to work along the way. Of course, there's still plenty worth iterating on; this post just gets the ball rolling.

## Requirements Analysis: What Do We Actually Need

Before writing any code, we need to think through what this copier should look like. If you start hammering the keyboard right away, you'll discover halfway through that the requirements were never settled, and the code ends up reworked over and over like a stack of patches.

### Core Functionality

At the most basic level, we need to move a file from point A to point B, right? But several details deserve consideration:

- First, there's **chunked reading and writing**. You can't read the entire file into memory at once—I've genuinely seen a program shovel all of its data into RAM or even the graphics card's memory and promptly OOM my machine. Imagine copying a 20 GB virtual machine image: memory would blow up on the spot. So we work in batches: read a chunk, write a chunk, and repeat. The chunk size itself is a craft question—too small and the frequent system calls drag efficiency down; too large and memory pressure builds up. As a rule of thumb, anything between 8 KB and a few MB is reasonable; we'll default to 8 KB to stay conservative. If you're interested, you can tweak it yourself later and probe how best to settle this benchmark.
- Second, there's **error handling**. File operations are a field full of surprises: the source file might not exist, the destination path might lack write permission, the disk might be full, or a read or write might fail midway. A dependable copier must not crash the moment something goes wrong; it should report the error gracefully and return a failure status.
- Third, there's **progress feedback**. Staring at a blank screen while a large file copies is agony. We should provide a progress bar—ideally one that also shows speed and estimated time remaining, so the user knows where things stand. Not a core feature, but it makes for a much better experience.
- Finally, there's **result verification**. How do you know the copy succeeded once it's done? The simplest approach is comparing the sizes of the source and destination files—less rigorous than a checksum, but good enough for most scenarios.

### Interface Design

Based on the analysis above, the interface of our `FileCopier` class comes out very lean:

```cpp
class FileCopier {
public:
  explicit FileCopier(std::size_t chunk_size = 8 * 1024);
  bool copy(const std::string &src_path, const std::string &dst_path);
  void setChunkSize(std::size_t size) { chunk_size_ = size; }
private:
  std::size_t chunk_size_;
};

```

A few things here are worth calling out. The constructor is marked `explicit`—a good habit that keeps the compiler from sneaking in implicit conversions and spares you some baffling bugs. The default chunk size of 8 KB is an empirical value: it doesn't hog memory, and performance is respectable.

The `copy` method returns `bool`—plain and clear: `true` on success, `false` on failure. The parameters are `const std::string&` to avoid unnecessary copies. Paths are `std::string` rather than `std::filesystem::path` for the sake of a simpler interface; converting internally is easy anyway.

`setChunkSize` provides the ability to adjust the chunk size at runtime. Most of the time the default is fine, but if you know you're copying enormous files you can bump it up, and if memory is tight you can dial it down. This kind of flexibility costs almost nothing, yet it pays off at the crucial moment.

## Technology Choices: Which C++ Features to Use

### The Filesystem Library: Farewell to Hand-Rolled Path Parsing

`std::filesystem`, introduced in C++17, is a gem. In the old days, working with file paths meant personally handling slashes, backslashes, relative paths, absolute paths, and all that drudgery; now a single `fs::path` takes care of everything. Checking file existence, querying file size, creating directories—ready-made APIs for all of them.

```cpp
namespace fs = std::filesystem;

```

I'm sure this namespace alias is self-explanatory at a glance. At the very least, I always write it in this shorthand form myself—spelling it out in full is just too tiring (even though IDE autocompletion is quite good, reading it all is tiring too).

### File Streams: Classic but Dependable

`std::ifstream` and `std::ofstream` are familiar old faces, yet in binary mode they remain a reliable way to read and write files. The key point is that they follow the RAII principle: files close automatically on destruction, so you never leak a handle by forgetting `close()`.

Specifying `std::ios::binary` when opening files is critical. Without this flag, Windows may translate newline characters and corrupt binary files. Linux is largely unaffected, but cross-platform code has to mind details like these.

### Dynamic Arrays: vector as the Buffer

```cpp
std::vector<char> buffer(chunk_size_);

```

Using a `vector` as the read/write buffer is a common technique. Compared with manual `new` and `delete`, a `vector` manages its memory automatically and never leaks. What's more, the `data()` method hands you a pointer to the underlying contiguous memory, which you can pass straight to `read()` and `write()`—as efficient as a raw array.

Note that initializing it directly with `chunk_size_` pre-allocates the `vector` to that size, avoiding any reallocation later.

### Time Measurement: the chrono Library

A progress bar needs to compute speed and estimate remaining time, which calls for precise time measurement. `std::chrono` is the time library introduced in C++11; its syntax is a bit wordy, but it's powerful and type-safe.

```cpp
auto t_start = std::chrono::steady_clock::now();

```

`steady_clock` guarantees that time only moves forward, unaffected by system clock adjustments—well suited to measuring intervals. `auto` type deduction earns its keep here; otherwise you'd be writing `std::chrono::time_point<std::chrono::steady_clock>`, which is headache-inducing even to think about.

## Building the Basic Framework

### The Constructor: Simple but Necessary

```cpp
FileCopier::FileCopier(std::size_t chunk_size) : chunk_size_(chunk_size) {}

```

The constructor is a single line, assigning `chunk_size_` in the member initializer list. That's more efficient than assigning inside the body, because it directly initializes rather than default-constructing first and assigning afterward. For a fundamental type like `std::size_t` the difference is negligible, but it's always good to build the habit.

### Overall Structure of the copy Method

The entire copy logic is wrapped in one big `try-catch` block:

```cpp
bool FileCopier::copy(const std::string &src_path,
                      const std::string &dst_path) {
  try {
    // actual copy logic
  } catch (const fs::filesystem_error &e) {
    std::cerr << "Filesystem error: " << e.what() << "\n";
    return false;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return false;
  }
}

```

`filesystem_error` is caught first—it's the specific exception the `filesystem` library throws, carrying more detailed error information. Then the generic `std::exception` is caught as a fallback. All exceptions are converted into a `false` return, plus the error message printed to `stderr`.

This error-handling strategy is on the conservative side: the program won't crash, but it also means callers need to check the return value. If you feel certain errors should be fatal, you can also let the exceptions keep propagating upward.

### Precondition Check: Confirm the Source File Exists First

```cpp
if (!fs::exists(src_path)) {
  std::cerr << "Source file does not exist: " << src_path << "\n";
  return false;
}

std::uintmax_t total_size = fs::file_size(src_path);

```

Before the copying actually begins, `fs::exists` checks whether the source file is there. This way you don't discover the problem only when opening the file later, and the error message is more explicit.

`fs::file_size` returns a `std::uintmax_t`, an unsigned integer type capable of representing very large files. With files routinely running to tens of GB these days, a 32-bit `unsigned int` stopped being enough long ago.

### Opening the Files: Binary Mode Matters

```cpp
std::ifstream in(src_path, std::ios::binary);
if (!in) {
  std::cerr << "Failed to open source file for reading: " << src_path << "\n";
  return false;
}

std::ofstream out(dst_path, std::ios::binary | std::ios::trunc);
if (!out) {
  std::cerr << "Failed to open destination file for writing: " << dst_path << "\n";
  return false;
}

```

The input stream uses `std::ios::binary`, and the output stream uses `std::ios::binary | std::ios::trunc`. `trunc` means that if the destination file already exists, it gets wiped—standard behavior for a copy operation. You certainly don't want the new content appended after the old.

Failed opens are detected with `if (!in)`, which uses the stream object's overloaded `operator bool()`—more concise than calling `is_open()`.

### Buffer Preparation: the Benefits of vector

```cpp
std::vector<char> buffer(chunk_size_);

```

This allocates a `vector` of `char` with the size of `chunk_size_`. The memory is released automatically when the function returns—nothing to worry about.

Why `char` rather than `uint8_t` or `std::byte`? Mainly because `ifstream::read` and `ofstream::write` accept `char*` pointers. C++17 does offer `std::byte`, but for compatibility and simplicity, `char` remains the common choice.

### Variables for Progress Tracking

```cpp
std::uintmax_t copied = 0;
auto t_start = std::chrono::steady_clock::now();
auto last_report = t_start;

```

`copied` records how many bytes have been copied so far; `t_start` records the starting time, used to compute total elapsed time and average speed; `last_report` records when the progress bar was last updated.

Three `auto`s in a row here—type deduction keeps the code much tidier. If you're not yet fully comfortable with `auto`, you can have your IDE show the deduced concrete types, or use `decltype` for a compile-time check.

## Summary

In this first part we sorted out the requirements, designed the interface, introduced every C++ feature involved, and stood up the basic framework. As you can see, the facilities modern C++ provides—`filesystem`, `chrono`, `vector`, RAII, exception handling—let us write code that is concise yet robust, without grinding away at low-level details like memory management and path parsing.

In the next part we'll implement the core read/write loop and progress bar display—that's where it gets truly interesting. It will touch on some performance considerations, plus practical techniques like using `chrono` to compute speed and estimate remaining time.
