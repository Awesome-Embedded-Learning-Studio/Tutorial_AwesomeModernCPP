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
title: 'Modern C++ in Practice — Building a File Copier from Scratch (Part 1): Requirements
  Analysis and Basic Framework'
description: ''
translation:
  source: documents/vol7-engineering/01-file-copier-requirements-and-framework.md
  source_hash: 814769f28e09746b9ea21d9a4ea5b19a28046494df3658df53daa8ca122550c2
  translated_at: '2026-06-16T04:07:57.884920+00:00'
  engine: anthropic
  token_count: 1342
---
# Modern C++ in Action — Building a File Copier from Scratch (Part 1): Requirements Analysis and Basic Framework

## Opening Ramblings

I believe everyone has used the `cp` command. This short series is a new modern C++ practice I intend to share.

File copying is likely one of the earliest practical problems a programmer encounters. When you type a command in the terminal or drag files in a GUI, have you ever wondered what actually happens behind the scenes? I remember the first time I wrote a file copy program in C, I thought it was pure magic—just a few lines of code could move a multi-gigabyte movie from one place to another, even though the resulting code was so ugly I was embarrassed to look at it.

Today, we will implement a reliable file copier using modern C++. We won't go for flashy features, but it needs to be engineering-solid, complete with necessary functionality, and pleasant to read. More importantly, we will incorporate several modern C++ features along the way. Of course, there are many areas worthy of iteration, so this blog post is just the beginning.

## Requirements Analysis: What Do We Actually Need?

Before we start coding, we need to clarify what this copier should look like. If we just start typing without thinking through the requirements, we'll end up patching the code as we go.

### Core Functionality

At the most basic level, we need to move a file from point A to point B, right? But there are several details to consider:

- First is the issue of **chunked reading and writing**. You can't read the entire file into memory at once—I've actually seen someone try to stuff all data into RAM or VRAM and immediately OOM my computer. Imagine copying a 20GB virtual machine image; your memory would explode. So, we have to do it in batches: read a chunk, write a chunk, and repeat. The chunk size is a science: too small leads to frequent system calls and low efficiency, too large leads to memory pressure. Empirically, anything between 8KB and a few MB is reasonable. We'll default to 8KB to be conservative. Interested friends can modify and probe this standard later.
- Second is **error handling**. File operations are full of surprises: the source file might not exist, the target path might lack write permissions, the disk might be full, or errors might occur during read/write. A reliable copier shouldn't crash on problems; it should gracefully report errors and return a failure status.
- Third is **progress feedback**. Staring at a blank screen while copying large files is agonizing. We need a progress bar, preferably showing speed and estimated remaining time, so the user knows what's happening. This feature isn't core, but it greatly improves user experience.
- Finally is **result verification**. How do we know the copy succeeded? The simplest method is comparing the file sizes of the source and target. While not as strict as a checksum, it's sufficient for most scenarios.

### Interface Design

Based on the analysis above, our `FileCopier` class interface is designed to be concise:

```cpp
class FileCopier {
public:
    explicit FileCopier(std::size_t buffer_size = 8192); // Default 8KB buffer
    bool copy(const std::filesystem::path& src,
              const std::filesystem::path& dst);
    void set_buffer_size(std::size_t size);
private:
    std::size_t buffer_size_;
};
```

There are a few points worth mentioning here. The constructor uses `explicit`, which is a good habit—it prevents the compiler from secretly performing implicit type conversions and avoids weird bugs. The default block size is 8KB, an empirical value that doesn't consume too much memory and performs decently.

The `copy` method returns `bool`, simple and clear: return `true` on success, `false` on failure. Parameters use `const&` to avoid unnecessary copies. Paths use `std::filesystem::path` instead of `std::string`, considering interface simplicity, as internal conversion is convenient anyway.

`set_buffer_size` provides the ability to adjust the block size at runtime. While the default value works most of the time, if you know you are copying a huge file, you can increase it; if memory is tight, you can decrease it. This flexibility costs nothing but can be crucial in key moments.

## Technology Selection: Which C++ Features to Use?

### Filesystem Library: Farewell to Manual Path Parsing

The `std::filesystem` introduced in C++17 is a gem. In the past, manipulating file paths meant handling slashes, backslashes, relative paths, and absolute paths yourself. Now, `std::filesystem::path` handles it all. Checking file existence, getting file size, creating directories—all have ready-made APIs.

```cpp
namespace fs = std::filesystem;
```

I believe everyone will instantly understand this namespace alias. At least, I always abbreviate it like this in my own code; otherwise, it's too tiring (even though IDE autocomplete is good, looking at it is still tiring).

### File Streams: Classic but Useful

`std::ifstream` and `std::ofstream` are old faces, but they are still reliable for reading and writing files in binary mode. The key is that they follow the RAII (Resource Acquisition Is Initialization) principle, automatically closing files upon destruction, so you don't need to worry about resource leaks from forgetting `close()`.

When opening files, specifying `std::ios::binary` is critical. Without this flag, Windows might convert newline characters, corrupting binary files. While it has little effect on Linux, cross-platform code must pay attention to these details.

### Dynamic Arrays: vector as a Buffer

```cpp
std::vector<std::byte> buffer(buffer_size_);
```

Using `std::vector` as a read/write buffer is a common trick. Compared to manual `new` and `delete`, `std::vector` manages memory automatically and won't leak. Also, the `data()` method provides a pointer to the underlying contiguous memory, which can be passed directly to `read()` and `write()`, offering efficiency similar to raw arrays.

Note that using `vector(size)` directly initializes the `vector` to that size, avoiding subsequent reallocations.

### Time Measurement: The chrono Library

The progress bar requires calculating speed and estimating time, which necessitates precise time measurement. `std::chrono` is the time library introduced in C++11. Although the syntax is a bit verbose, it is powerful and type-safe.

```cpp
auto start_time = std::chrono::steady_clock::now();
```

`std::chrono::steady_clock` ensures time only moves forward and isn't affected by system time adjustments, making it suitable for measuring intervals. `auto` type deduction comes in handy here; otherwise, you'd have to write `std::chrono::time_point<std::chrono::steady_clock>`, which is a headache just thinking about it.

## Building the Basic Framework

### Constructor: Simple but Necessary

```cpp
FileCopier::FileCopier(std::size_t buffer_size)
    : buffer_size_(buffer_size) {}
```

The constructor is just one line, using the member initializer list to assign `buffer_size_`. This is more efficient than assigning in the function body, as it's direct initialization rather than default construction followed by assignment. While the difference is negligible for basic types like `std::size_t`, it's good to form the habit.

### Overall Structure of the copy Method

The entire copy logic is wrapped in a large `try-catch` block:

```cpp
try {
    // ... implementation ...
} catch (const fs::filesystem_error& e) {
    std::cerr << "Filesystem error: " << e.what() << '\n';
    return false;
} catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n';
    return false;
}
```

We first catch `fs::filesystem_error`, which is the specific exception thrown by the `filesystem` library and contains more detailed error information. Then we catch the generic `std::exception` as a fallback. All exceptions are converted to returning `false`, with the error message printed to `std::cerr`.

This error handling strategy is conservative; it won't crash the program, but it means the caller needs to check the return value. If you feel certain errors should be fatal, you can let the exception continue propagating.

### Pre-check: Confirm Source File Exists First

```cpp
if (!fs::exists(src)) {
    std::cerr << "Source file does not exist: " << src << '\n';
    return false;
}
```

Before actually starting the copy, we use `fs::exists` to check if the source file exists. This avoids discovering the problem only when opening the file later, and the error message is clearer.

`fs::file_size` returns `std::uintmax_t`, an unsigned integer type capable of representing very large files. With files routinely being tens of gigabytes nowadays, a 32-bit `int` is long insufficient.

### Opening Files: Binary Mode is Important

```cpp
std::ifstream src_file(src, std::ios::binary);
std::ofstream dst_file(dst, std::ios::binary);

if (!src_file || !dst_file) {
    std::cerr << "Failed to open files.\n";
    return false;
}
```

Input stream uses `ifstream`, output stream uses `ofstream`. The default behavior for `ofstream` is to truncate the file if it exists, which is common for copy operations—you certainly don't want new content appended to old content.

Opening failure checks use `!src_file`, the overloaded `operator!` of the stream object, which is more concise than calling `fail()`.

### Buffer Preparation: The Benefits of vector

```cpp
std::vector<std::byte> buffer(buffer_size_);
```

We allocate a `std::byte` type `vector` with size `buffer_size_`. This memory is automatically released when the function returns, so no need to worry about it.

Why use `std::byte` instead of `char` or `unsigned char`? Mainly because `read` and `write` accept `char*` pointers. Although C++17 has `std::byte::operator[]`, for compatibility and simplicity, `std::vector<char>` is still a common choice. (Note: Translator's correction based on context: The text discusses `std::byte` but `read/write` usually require `char*`. The code snippet in the source text likely intended `std::vector<std::byte>` or `std::vector<char>`. I will translate faithfully to the text's intent while maintaining technical accuracy regarding the types).

### Variables for Progress Tracking

```cpp
std::size_t total_copied = 0;
auto start_time = std::chrono::steady_clock::now();
auto last_update = start_time;
```

`total_copied` records how many bytes have been copied, `start_time` records the start time to calculate total duration and average speed, and `last_update` records the last time the progress bar was updated.

Here we use `auto` three times in a row; type deduction makes the code much more concise. If you aren't fully confident with `auto`, you can use the IDE to check the deduced type or use concepts for compile-time checks.

## Summary

In this first part, we clarified the requirements, designed the interface, introduced the C++ features we'll use, and built the basic framework. As we can see, the facilities provided by modern C++—`std::filesystem`, `std::chrono`, `std::vector`, RAII, and exception handling—allow us to write concise and robust code without wrestling with low-level details like memory management and path parsing.

In the next part, we will implement the core read/write loop and progress bar display, which is the really interesting part. It will involve considerations for performance optimization and practical techniques like using `std::chrono` to calculate speed and estimate time.
