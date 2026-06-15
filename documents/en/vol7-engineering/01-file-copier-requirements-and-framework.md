---
chapter: 1
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 10
tags:
- cpp-modern
- host
- intermediate
title: 'Modern C++ in Practice — Building a File Copier from Scratch (Part 1): Requirements
  Analysis and Basic Framework'
translation:
  engine: anthropic
  source: documents/vol7-engineering/01-file-copier-requirements-and-framework.md
  source_hash: f4c115a8025bf912239a5defbdc8200dba0fb92daf51ffc490bc3eecd20a7976
  token_count: 1336
  translated_at: '2026-05-26T11:51:28.325086+00:00'
description: ''
---
# Modern C++ in Practice — Building a File Copier from Scratch (Part 1): Requirements Analysis and Basic Framework

## A Few Opening Thoughts

I'm sure everyone has used the `cp` command. This short series is a new modern C++ practice I've been planning.

File copying might be one of the first practical problems a programmer encounters in their career. When you type `cp` in the terminal or drag and drop files in a GUI, have you ever wondered what actually happens behind the scenes? I remember the first time I wrote a file copier in C—I thought it was incredibly magical. Just a few lines of code could move a multi-gigabyte movie from one place to another, even though the code I wrote back then was so ugly I couldn't bear to look at it.

Today, we'll use modern C++ to build a reliable file copier. We aren't aiming for anything flashy, but it needs to be engineering-solid: it should have all the necessary features, and the code should be pleasant to read. More importantly, we'll naturally put several modern C++ features to use along the way. Of course, there are still plenty of areas worth iterating on, so consider this blog post a starting point.

## Requirements Analysis: What Do We Actually Need?

Before writing any code, we need to figure out what this copier should look like. If you just start typing away without thinking through the requirements, you'll end up constantly revising your code, patching things up as you go.

### Core Features

At a bare minimum, we need to move a file from point A to point B, right? But there are a few details to consider:

- First is the issue of **chunked reading and writing**. You can't load the entire file into memory at once—I've actually seen someone stuff all the data into their RAM or VRAM and instantly trigger an out-of-memory (OOM) crash on their computer. Imagine copying a 20GB virtual machine image; your memory would simply explode. So, we need to do it in batches: read a chunk, write a chunk, and loop. The size of this chunk is a bit of an art form. If it's too small, frequent system calls drag down performance; if it's too large, it puts pressure on memory. Empirically, anything from 8KB to a few megabytes is reasonable. We'll default to 8KB to be conservative. Later on, if you're interested, you can tweak and benchmark this threshold yourself.
- Second is **error handling**. File operations are full of surprises: the source file might not exist, the target path might lack write permissions, the disk might be full, or errors might occur during read/write. A reliable copier shouldn't crash when it hits a problem; it needs to report the error gracefully and return a failure status.
- Third is **progress feedback**. Staring at a blank screen while copying a large file is agonizing. We need to provide a progress bar, ideally showing the speed and estimated remaining time, so the user knows what to expect. This feature isn't strictly core, but it vastly improves the user experience.
- Finally, **result verification**. How do we know the copy succeeded? The simplest approach is to compare the file sizes of the source and the target. While not as rigorous as a checksum, it's sufficient for most scenarios.

### Interface Design

Based on the analysis above, our `FileCopier` class interface is designed to be very concise:

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

There are a few things worth mentioning here. The constructor uses `explicit`, which is a good habit—it prevents the compiler from secretly performing implicit type conversions and avoids some baffling bugs. The default chunk size is 8KB, an empirical value that doesn't consume too much memory while still delivering decent performance.

The `copy` method returns `bool`, which is simple and clear: return `true` for success, `false` for failure. The parameters use `std::string_view`, avoiding unnecessary copies. The paths use `std::string_view` rather than `std::filesystem::path` to keep the interface simple, since converting internally is quite convenient anyway.

`set_chunk_size` provides the ability to adjust the chunk size at runtime. Most of the time, the default is fine, but if you know you're copying a massive file, you can increase it; if memory is tight, you can decrease it. This flexibility costs almost nothing but can come in handy when it matters.

## Technology Choices: Which C++ Features to Use?

### Filesystem Library: Saying Goodbye to Manual Path Parsing

The `std::filesystem` introduced in C++17 is a treasure. In the past, manipulating file paths meant dealing with slashes, backslashes, relative paths, and absolute paths yourself. Now, a single `std::filesystem::path` handles it all. Checking file existence, getting file sizes, and creating directories all have ready-to-use APIs.

```cpp
namespace fs = std::filesystem;

```

I believe everyone will instantly understand this namespace alias. At least, I always abbreviate it like this when I write code; otherwise, it's just too tedious (even though IDE auto-completion is pretty good, it's still tiring to look at).

### File Streams: Classic but Reliable

`std::ifstream` and `std::ofstream` might be old faces, but they are still very reliable for reading and writing files in binary mode. The key is that they follow the RAII principle, automatically closing files upon destruction, so we don't need to worry about resource leaks caused by forgetting to call `close()`.

When opening a file, specifying `std::ios::binary` is crucial. Without this flag, Windows might perform newline conversions, which can corrupt binary files. While this doesn't have much impact on Linux, you need to pay attention to these details when writing cross-platform code.

### Dynamic Arrays: Using vector as a Buffer

```cpp
std::vector<char> buffer(chunk_size_);

```

Using `std::vector` as a read/write buffer is a common technique. Compared to manually calling `new` and `delete`, `std::vector` manages memory automatically and won't leak. Moreover, the `data()` method gives you access to the underlying contiguous memory pointer, which can be passed directly to `read()` and `write()`, offering the same efficiency as raw arrays.

Note that initializing directly with a size pre-allocates the `vector` to that capacity, avoiding subsequent reallocations.

### Time Measurement: The chrono Library

The progress bar requires calculating speed and estimating time, which calls for precise time measurement. `std::chrono` is the time library introduced in C++11. Although its syntax is a bit verbose, it is powerful and type-safe.

```cpp
auto t_start = std::chrono::steady_clock::now();

```

`std::chrono::steady_clock` guarantees that time only moves forward and isn't affected by system time adjustments, making it suitable for measuring time intervals. Type deduction with `auto` really shines here; otherwise, you'd have to write `std::chrono::time_point<std::chrono::steady_clock>`, which is a headache just thinking about it.

## Building the Basic Framework

### Constructor: Simple but Necessary

```cpp
FileCopier::FileCopier(std::size_t chunk_size) : chunk_size_(chunk_size) {}

```

The constructor is just one line, using a member initializer list to assign `chunk_size_`. This is more efficient than assigning inside the function body, as it performs direct initialization rather than default construction followed by assignment. Although the difference is negligible for fundamental types like `size_t`, it's always a good habit to form.

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

We first catch `std::filesystem::filesystem_error`, which is a specific exception thrown by the `std::filesystem` library that contains more detailed error information. Then, we catch the generic `std::exception` as a fallback. All exceptions are converted into returning `false`, along with printing the error message to `std::cerr`.

This error handling strategy is quite conservative; it won't crash the program, but it also means the caller needs to check the return value. If you feel that certain errors should be fatal, you can also let the exceptions continue propagating up the call stack.

### Pre-checks: Confirm the Source File Exists First

```cpp
if (!fs::exists(src_path)) {
  std::cerr << "Source file does not exist: " << src_path << "\n";
  return false;
}

std::uintmax_t total_size = fs::file_size(src_path);

```

Before actually starting the copy, we use `std::filesystem::exists` to check if the source file exists. This prevents discovering the problem only when opening the file later, and it provides a clearer error message.

`file_size` returns a `std::uintmax_t`, which is an unsigned integer type capable of representing very large files. With files routinely hitting tens of gigabytes these days, a 32-bit `size_t` hasn't been enough for a long time.

### Opening Files: Binary Mode is Important

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

The input stream uses `std::ios::in`, and the output stream uses `std::ios::out`. `std::ios::trunc` means if the target file already exists, it gets truncated. This is common behavior for a copy operation—you definitely don't want new content appended after old content.

The check for a failed open uses the `!` operator, which is an overloaded `bool` conversion on the stream object, making it more concise than calling `is_open()`.

### Buffer Preparation: The Benefits of vector

```cpp
std::vector<char> buffer(chunk_size_);

```

We allocate a `std::vector` of type `char`, with a size of `chunk_size_`. This block of memory is automatically released when the function returns, so we don't need to worry about it.

Why use `char` instead of `std::byte` or `unsigned char`? Mainly because `read()` and `write()` accept `char*` pointers. Although C++17 introduced `std::byte`, for the sake of compatibility and simplicity, `char` remains a common choice.

### Progress Tracking Variables

```cpp
std::uintmax_t copied = 0;
auto t_start = std::chrono::steady_clock::now();
auto last_report = t_start;

```

`bytes_copied` records how many bytes have been copied so far, `start_time` records the start time for calculating total elapsed time and average speed, and `last_update_time` records when the progress bar was last updated.

Here we use `auto` three times in a row, as type deduction makes the code much more concise. If you're still not entirely comfortable with `auto`, you can use your IDE to check the deduced types, or use concepts to perform compile-time checks.

## Summary

In this first part, we clarified the requirements, designed the interface, introduced all the C++ features we'll be using, and set up the basic framework. As we can see, the facilities provided by modern C++—`std::filesystem`, `std::vector`, `std::chrono`, RAII, and exception handling—allow us to write concise yet robust code without having to wrestle with low-level details like memory management and path parsing.

In the next part, we will implement the core read/write loop and the progress bar display, which is the really interesting part. It will involve some performance optimization considerations, as well as practical techniques like using `std::chrono` to calculate speed and estimate remaining time.
