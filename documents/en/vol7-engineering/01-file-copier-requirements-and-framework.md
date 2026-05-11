---
title: 'Modern C++ in Practice — Writing a File Copier from Scratch (Part 1): Requirements
  Analysis and Basic Framework'
description: ''
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
chapter: 1
order: 4
---
# Modern C++ in Practice — Building a File Copier from Scratch (Part 1): Requirements Analysis and Basic Framework

## A Few Words to Start

I'm sure everyone has used the `cp` command. This short series is a new modern C++ practice I've been planning.

File copying might be one of the first practical problems a programmer encounters in their career. When you type `cp` in a terminal or drag and drop files in a GUI, have you ever wondered what actually happens behind the scenes? I remember the first time I wrote a file copier in C — it felt magical. Just a few lines of code could move a multi-gigabyte movie from one place to another, even though the code I wrote back then was so ugly I couldn't bear to look at it.

Today, we'll build a reliable file copier using modern C++. We aren't aiming for anything flashy, but it needs to be engineering-solid with all the essential features and clean, readable code. More importantly, we'll naturally incorporate several modern C++ features along the way. Of course, there are still plenty of areas worth iterating on, so consider this blog post a starting point.

## Requirements Analysis: What Do We Actually Need?

Before writing any code, we need to figure out what this copier should look like. Diving straight into coding without a clear picture of the requirements leads to a patchwork of changes and rewrites.

### Core Features

At a minimum, we need to move a file from point A to point B, right? But there are a few details to consider:

- First, there's the issue of **chunked reading and writing**. We can't load the entire file into memory at once — I've actually seen someone stuff all the data into their RAM or VRAM and instantly trigger an OOM crash. Imagine copying a 20GB virtual machine image; your memory would simply explode. So we need to do it in batches: read a chunk, write a chunk, and repeat. The chunk size is a balancing act — too small means frequent system calls and poor efficiency, too large means memory pressure. Empirically, anything from 8KB to a few megabytes is reasonable. We'll default to 8KB to be conservative. Later, if you're interested, you can tweak and benchmark this threshold yourself.
- Second is **error handling**. File operations are full of surprises: the source file might not exist, the target path might lack write permissions, the disk might be full, or errors might occur during read/write. A reliable copier shouldn't crash on problems; it should report errors gracefully and return a failure status.
- Third is **progress feedback**. Staring at a blank screen while copying a large file is agonizing. We need a progress bar, ideally showing speed and estimated remaining time, so the user knows what to expect. This isn't a core feature, but it vastly improves user experience.
- Finally, **result verification**. How do we know the copy succeeded? The simplest approach is comparing the source and target file sizes. While not as rigorous as a checksum, it's sufficient for most scenarios.

### Interface Design

Based on the analysis above, our `FileCopier` class interface is designed to be minimal:

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

A few things are worth mentioning here. The constructor uses `explicit`, which is a good habit — it prevents the compiler from secretly performing implicit type conversions and avoids obscure bugs. The default chunk size of 8KB is an empirical value that doesn't consume too much memory while delivering decent performance.

The `copy` method returns `bool`, keeping things simple: return `true` on success and `false` on failure. Parameters use `const std::string&` to avoid unnecessary copies. Paths use `std::string` instead of `std::filesystem::path` for interface simplicity, since converting internally is straightforward anyway.

`setChunkSize` provides the ability to adjust the chunk size at runtime. While the default works for most cases, you can increase it for very large files or decrease it when memory is tight. This flexibility costs almost nothing but can be a lifesaver when it matters.

## Technology Choices: Which C++ Features to Use?

### Filesystem Library: Goodbye Manual Path Parsing

The `std::filesystem` introduced in C++17 is a treasure. In the past, manipulating file paths meant dealing with slashes, backslashes, relative paths, and absolute paths yourself. Now, a single `fs::path` handles it all. Checking file existence, getting file sizes, and creating directories all have ready-to-use APIs.

```cpp
namespace fs = std::filesystem;

```

I'm sure everyone instantly understands this namespace alias. I always abbreviate it like this in my own code — otherwise it's just too tedious (even though IDE auto-completion is nice, it's still tiring to look at).

### File Streams: Classic but Reliable

`std::ifstream` and `std::ofstream` may be old familiars, but they're still rock-solid for reading and writing files in binary mode. The key point is that they follow the RAII principle, automatically closing files on destruction, so we don't need to worry about resource leaks from forgetting to call `close()`.

Specifying `std::ios::binary` when opening files is critical. Without this flag, Windows might perform newline conversions, corrupting binary files. While this doesn't matter much on Linux, writing cross-platform code means paying attention to these details.

### Dynamic Arrays: vector as a Buffer

```cpp
std::vector<char> buffer(chunk_size_);

```

Using `vector` as a read/write buffer is a common technique. Compared to manual `new` and `delete`, `vector` manages memory automatically and won't leak. Plus, the `data()` method gives us a pointer to the underlying contiguous memory, which we can pass directly to `read()` and `write()` for the same efficiency as raw arrays.

Note that using `chunk_size_` for initialization pre-allocates the `vector` to this size, avoiding subsequent reallocations.

### Time Measurement: The chrono Library

The progress bar needs to calculate speed and estimate remaining time, which requires precise time measurement. `std::chrono` is the time library introduced in C++11. While its syntax is a bit verbose, it's powerful and type-safe.

```cpp
auto t_start = std::chrono::steady_clock::now();

```

`steady_clock` guarantees that time only moves forward and isn't affected by system time adjustments, making it suitable for measuring time intervals. `auto` type deduction comes in handy here; otherwise, you'd have to write `std::chrono::time_point<std::chrono::steady_clock>`, which is a headache just thinking about it.

## Building the Basic Framework

### Constructor: Simple but Necessary

```cpp
FileCopier::FileCopier(std::size_t chunk_size) : chunk_size_(chunk_size) {}

```

The constructor is a single line, using a member initializer list to assign `chunk_size_`. This is more efficient than assigning in the function body, as it performs direct initialization rather than default construction followed by assignment. While the difference is negligible for a fundamental type like `std::size_t`, it's a good habit to build.

### Overall Structure of the copy Method

The entire copy logic is wrapped in a large `try-catch` block:

```cpp
bool FileCopier::copy(const std::string &src_path,
                      const std::string &dst_path) {
  try {
    // 实际拷贝逻辑
  } catch (const fs::filesystem_error &e) {
    std::cerr << "Filesystem error: " << e.what() << "\n";
    return false;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return false;
  }
}

```

We first catch `filesystem_error`, which is a specific exception thrown by the `filesystem` library containing more detailed error information. Then we catch the generic `std::exception` as a fallback. All exceptions are converted into returning `false`, along with printing the error message to `stderr`.

This error handling strategy is conservative — it won't crash the program, but it does mean the caller needs to check the return value. If you feel certain errors should be fatal, you can let the exceptions propagate further up.

### Pre-check: Confirm the Source File Exists First

```cpp
if (!fs::exists(src_path)) {
  std::cerr << "Source file does not exist: " << src_path << "\n";
  return false;
}

std::uintmax_t total_size = fs::file_size(src_path);

```

Before actually starting the copy, we use `fs::exists` to check if the source file exists. This avoids discovering the problem only when opening the file later, and the error message is more explicit.

`fs::file_size` returns a `std::uintmax_t`, which is an unsigned integer type capable of representing very large files. With files routinely reaching tens of gigabytes these days, a 32-bit `unsigned int` hasn't been adequate for a long time.

### Opening Files: Binary Mode Is Important

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

The input stream uses `std::ios::binary`, and the output stream uses `std::ios::binary | std::ios::trunc`. `trunc` means if the target file already exists, it gets truncated — this is standard behavior for a copy operation, as you definitely don't want new content appended after old content.

The check for a failed open uses `if (!in)`, which is the stream object's overloaded `operator bool()`, making it more concise than calling `is_open()`.

### Buffer Preparation: The Benefits of vector

```cpp
std::vector<char> buffer(chunk_size_);

```

We allocate a `char`-typed `vector` with a size of `chunk_size_`. This memory is automatically released when the function returns, so there's nothing to worry about.

Why use `char` instead of `uint8_t` or `std::byte`? Mainly because `ifstream::read` and `ofstream::write` accept `char*` pointers. Although C++17 has `std::byte`, `char` remains a common choice for compatibility and simplicity.

### Progress Tracking Variables

```cpp
std::uintmax_t copied = 0;
auto t_start = std::chrono::steady_clock::now();
auto last_report = t_start;

```

`copied` records how many bytes have been copied, `t_start` records the start time for calculating total elapsed time and average speed, and `last_report` records the last time the progress bar was updated.

Here we use three consecutive `auto` declarations, where type deduction keeps the code much more concise. If you're not entirely confident with `auto`, you can use your IDE to inspect the deduced types, or use `decltype` for compile-time checks.

## Summary

In this first part, we clarified the requirements, designed the interface, introduced the C++ features we'll use, and built the basic framework. As we can see, the facilities provided by modern C++ — `filesystem`, `chrono`, `vector`, RAII, and exception handling — let us write concise yet robust code without wrestling with low-level details like memory management and path parsing.

In the next part, we'll implement the core read/write loop and progress bar display — that's where things get really interesting. We'll touch on performance optimization considerations, as well as practical techniques like using `chrono` to calculate speed and estimate remaining time.
