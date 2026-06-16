---
chapter: 1
difficulty: intermediate
order: 5
platform: host
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
title: 'Modern C++ Engineering Practice — Building a File Copier from Scratch (Part
  2): Core Implementation and Practical Testing'
description: ''
translation:
  source: documents/vol7-engineering/02-file-copier-core-implementation.md
  source_hash: f719f582313d80f8e622dd0462e7b46488d9234c67cc345803791af035132c67
  translated_at: '2026-06-16T04:08:15.048897+00:00'
  engine: anthropic
  token_count: 2801
---
# Modern C++ Engineering Practice — Building a File Copier from Scratch (Part 2): Core Implementation and Practical Testing

## Picking Up Where We Left Off

In the previous article, we set up the framework, opened the files, and prepared the buffers. All that remains is the most critical read-write loop. In this post, we will finish implementing the remaining core logic and write a test program to run it. Honestly, writing code without testing is like cooking without tasting; it just doesn't feel right.

## Core Read-Write Loop: Simple but Solid

### Design of the Main Loop

The core of file copying is a loop: read a chunk, write a chunk, and repeat until finished. It sounds simple, but there are many details to consider. Let's look at the overall structure:

```cpp
while (in_stream) {
    // Read and write...
}
```

The loop condition is `in_stream`, which uses the stream object's `operator bool`. As long as the input stream is in a good state (no errors or EOF), the loop continues. This is better than writing `!in_stream.eof()`, because the latter only checks the EOF flag and ignores other error states.

### Coordinating `read` and `gcount`

```cpp
constexpr size_t buffer_size = 1024 * 1024; // 1MB buffer
std::vector<char> buffer(buffer_size);

while (in_stream) {
    in_stream.read(buffer.data(), buffer_size);
    std::streamsize bytes_read = in_stream.gcount();
    // ...
}
```

The `read` method attempts to read a specified number of bytes, but it might not fill them all. For example, if only 1KB remains in the file and you ask it to read 8KB, it will only read 1KB. Therefore, we must immediately call `gcount()` to get the actual number of bytes read.

There is a small detail regarding type conversion here: `gcount()` returns `std::streamsize`, while `write` expects `std::size_t` (usually `size_t`). Although implicit conversion works in most cases, explicit conversion avoids compiler warnings and makes the code's intent clearer.

The `if (bytes_read > 0)` check is a safety measure. Normally, if the stream state goes bad, the `while` condition will exit the loop, but an extra layer of checking never hurts. This is how end-of-file is handled: the last `read` might read 0 bytes and set the EOF flag, then `gcount` returns 0, and we `continue` to skip it.

### `write` and Error Checking

```cpp
out_stream.write(buffer.data(), static_cast<std::streamsize>(bytes_read));

if (!out_stream) {
    std::cerr << "Failed to write to destination file.\n";
    return false;
}
```

Writing uses the actual number of bytes read, `bytes_read`, rather than the full `buffer_size`. This is crucial; otherwise, the last chunk of data would be padded with garbage bytes.

We check the stream state immediately after writing. If a write failure is detected, we return immediately. Write failures can be caused by a full disk, insufficient permissions, or device errors. Detecting it early and stopping prevents further issues from continuing to write corrupt data.

### Progress Statistics

```cpp
total_copied += static_cast<std::uint64_t>(bytes_read);
```

Every time a chunk is successfully written, we accumulate the byte count into `total_copied`. This value will be used later to calculate progress percentage and speed. The type conversion is again to match `std::uint64_t`. Although `bytes_read` won't be negative, the compiler doesn't know that, so explicit conversion keeps it happy.

## Progress Bar: Making the Wait Less Painful

### Designing the `ProgressBar` Class

The progress bar is encapsulated in its own class for single responsibility and easier maintenance:

```cpp
class ProgressBar {
public:
    explicit ProgressBar(int width = 20) : width_(width) {}

    void update(std::uint64_t copied, std::uint64_t total, double speed);
    // ...
private:
    int width_;
};
```

`width_` is the character width of the progress bar, defaulting to 20 characters. Too narrow isn't intuitive, too wide takes up space, and 20 is a compromise. The `update` method takes the number of bytes copied, total bytes, and current speed, and is responsible for drawing the progress bar in the terminal.

Note that `update` is a `const` method because it only displays information and doesn't modify object state. This const correctness is important in large projects and prevents many accidental modifications.

### Drawing Logic for the Progress Bar

```cpp
void ProgressBar::update(std::uint64_t copied, std::uint64_t total, double speed) const {
    double percent = (total == 0) ? 1.0 : static_cast<double>(copied) / total;
    int filled = static_cast<int>(percent * width_);

    std::cout << '[';
    for (int i = 0; i < filled; ++i) std::cout << '=';
    if (filled < width_) std::cout << '>';
    for (int i = filled + 1; i < width_; ++i) std::cout << ' ';
    std::cout << "] ";
    // ...
}
```

First, we calculate the completion ratio `percent`, then multiply by the width to determine how many characters to fill. This handles the division-by-zero case — an empty file is treated as 100% complete.

The progress bar style is `[===>      ]`. Completed sections use `=`, the current position uses `>`, and incomplete sections use spaces. Three loops draw these three parts respectively. While we could use `std::string` concatenation and output it all at once, direct output is more efficient for scenarios with frequent updates.

### Displaying Percentages and Sizes

```cpp
double copied_mb = copied / (1024.0 * 1024.0);
double total_mb = total / (1024.0 * 1024.0);

std::cout << std::fixed << std::setprecision(1);
std::cout << copied_mb << '/' << total_mb << "MB ";
```

Byte counts are converted to MB for display, which is more user-friendly. `std::fixed` and `std::setprecision(1)` make floating-point numbers retain one decimal place, like `10.5` instead of `10.12345`. These I/O manipulators are old friends in C++; while the syntax is verbose, they are very practical.

Speed is also divided by `1024 * 1024` to convert to MB/s. Note that we use 1024 here instead of 1000, because in computing, "mega" is binary, 1MB = 1024KB = 1024*1024 bytes. Although there is now the IEC standard (MiB vs MB) using 1000, using 1024 fits programmer habits better for internal displays.

### ETA Calculation: Estimating Remaining Time

```cpp
if (speed > 1e-6) {
    double remaining_mb = (total - copied) / (1024.0 * 1024.0);
    double seconds_left = remaining_mb / speed;
    // Format time...
}
```

ETA (Estimated Time of Arrival) is calculated by dividing the remaining bytes by the current speed. This estimate fluctuates with speed changes, but generally gives the user a psychological expectation.

We check `speed > 1e-6` to avoid division by zero. `1e-6` is a sufficiently small number; basically, as long as there is any speed, it will be greater than this.

The display format has three cases: over 1 hour shows "Xh Ym", over 1 minute shows "Xm Ys", otherwise just seconds. This tiered display is much more intuitive than a unified second count — would you rather see "2h 15m" or "8100s"?

### The Magic of the Carriage Return

```cpp
std::cout << '\r' << std::flush;
```

The entire `update` method ends by outputting a carriage return `\r` instead of a newline `\n`. The carriage return moves the cursor to the beginning of the line, so the next output will overwrite this line. This is the secret behind the progress bar's "dynamic update."

`std::flush` forces the output buffer to flush; otherwise, the output might be cached, and the user won't see real-time progress changes.

## Time and Speed Calculation

### Controlling Update Frequency

```cpp
auto now = std::chrono::steady_clock::now();
auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update_).count() / 1000.0;

if (elapsed >= 0.1 || copied == total) {
    double speed = (copied - last_copied_) / elapsed;
    // Update progress bar...
    last_update_ = now;
    last_copied_ = copied;
}
```

We don't update the progress bar for every read-write chunk; instead, we update only after at least 0.1 seconds have passed. Why? Because updating the progress bar itself has overhead. Too frequent updates can slow down the copy speed. Plus, the human eye can't distinguish such high update frequencies; 0.1 seconds (10 times per second) is smooth enough.

`std::chrono::steady_clock::now()` gets a `time_point` object, and calling `count()` converts it to seconds (double type). The type safety of the `std::chrono` library is evident here: different time points and durations have different types, preventing confusion.

Speed calculation divides the bytes copied by the total time elapsed. Note the check for `elapsed > 0`; while theoretically it shouldn't be 0, with floating-point math, defensive programming is always good.

We handle the `copied == total` case specifically to ensure the progress bar updates once when copying is complete, showing 100%.

## Wrapping Up

### Flushing and Closing

```cpp
out_stream.flush();
if (!out_stream) {
    std::cerr << "Failed to flush data to disk.\n";
    return false;
}

out_stream.close();
in_stream.close();
```

After writing all data, we explicitly call `flush` to ensure the buffer contents are written to disk. While the destructor automatically flushes, explicit calling is safer; if the flush fails, we can detect it immediately.

`close` isn't strictly necessary because the destructor automatically closes the file. However, explicit closing makes the code's intent clearer and releases file handles early, which is important on some operating systems.

### Final Progress and Verification

```cpp
bar_.update(total_copied, total_size_, average_speed);
std::cout << std::endl;

if (total_copied != total_size_) {
    std::cerr << "Copy failed: size mismatch.\n";
    return false;
}
```

We update the progress bar one last time with the average speed, then print a newline. This keeps the progress bar on the screen so the user can see the final statistics.

The verification phase is simple: checking if the target file size matches the source file. This isn't foolproof (theoretically data could be corrupted but the size remains the same), but it suffices for most error scenarios. If higher requirements are needed, calculating an MD5 or SHA-256 checksum is an option, but that significantly increases the time.

## Practical Usage

### Writing the `main` Function

We need a simple test program to call this copier:

```cpp
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <source> <destination>\n";
        return 1;
    }

    FileCopier copier(argv[1], argv[2]);
    bool success = copier.copy();

    return success ? 0 : 1;
}
```

It's that simple. Check the number of command-line arguments, create a `FileCopier` object, call the `copy` method, and determine the exit code based on the return value. Standard Unix program style: success returns 0, failure returns non-zero.

### Compilation Commands

Assuming your file structure looks like this:

```text
.
├── src/
│   ├── file_copier.cpp
│   ├── file_copier.h
│   └── main.cpp
└── build/
```

Compilation command:

```bash
g++ -std=c++17 -O2 -Wall -Wextra src/main.cpp -o build/cp_tool
```

Here are a few compiler options: `-std=c++17` specifies the C++17 standard (because we used `std::filesystem`), `-O2` enables optimization, `-Wall -Wextra` turns on warnings (helping you find potential issues), and `-o` specifies the output filename.

If you are using an older GCC version (before 9.0), you may need to link `stdc++fs` explicitly:

```bash
g++ -std=c++17 -O2 -Wall -Wextra src/main.cpp -lstdc++fs -o build/cp_tool
```

Clang users just need to swap `g++` for `clang++`; everything else is the same.

### Basic Testing

Let's test copying a small file first:

```bash
./build/cp_tool test.txt test_copy.txt
```

You should see the progress bar flash by (the file is too small), then display "Copy succeeded!". Use `ls -l` to compare sizes, or the `diff` command to verify content consistency:

```bash
diff test.txt test_copy.txt
```

No output means they are identical. Perfect.

### Testing Large Files

Small files don't really test the limits. We need a larger file. If you don't have one, you can generate one with the `dd` command:

```bash
dd if=/dev/urandom of=large_file.bin bs=1M count=1024
```

This creates a 1GB random data file. Then copy it:

```bash
./build/cp_tool large_file.bin large_copy.bin
```

Now you can watch the progress bar move slowly, displaying speed and ETA countdown, much like a download manager. After copying, verify it:

```bash
md5sum large_file.bin large_copy.bin
```

The two MD5 values should be completely identical.

### Edge Case Testing

Good testing covers edge cases:

**Empty file:**

```bash
touch empty.txt
./build/cp_tool empty.txt empty_copy.txt
```

It should handle this normally, with the progress bar directly showing 100%.

**Non-existent source file:**

```bash
./build/cp_tool nonexistent.txt out.txt
```

It should output "Source file does not exist" and return failure.

**Destination without write permissions:**

```bash
./build/cp_tool test.txt /root/copy_test.txt
```

It should output "Failed to open destination file for writing" (assuming you are not root).

**Insufficient disk space:** This is hard to simulate, but if encountered, the write phase will fail and return an error.

### Performance Testing

Want to know how this copier performs? You can compare it with the system's `cp` command:

```bash
time cp large_file.bin cp_copy.bin
time ./build/cp_tool large_file.bin tool_copy.bin
```

On my machine, both speeds are similar, around 1-2GB/s (depending on disk performance). This shows our implementation is reasonably efficient with no obvious performance loss.

If you want to optimize, try increasing `buffer_size`:

```cpp
constexpr size_t buffer_size = 4 * 1024 * 1024; // 4MB
```

In some scenarios, larger chunks reduce system call overhead and improve performance. But bigger isn't always better; too large increases memory pressure, and if interrupted mid-way, the written data is "rougher."

### A Complete Test Script

Write a shell script to automate these tests:

```bash
#!/bin/bash
# test_copy.sh

echo "=== Testing File Copier ==="

# Test 1: Small file
echo "Test 1: Small file..."
./build/cp_tool test.txt test_copy.txt && diff -q test.txt test_copy.txt && echo "PASS" || echo "FAIL"

# Test 2: Empty file
echo "Test 2: Empty file..."
touch empty.txt
./build/cp_tool empty.txt empty_copy.txt && diff -q empty.txt empty_copy.txt && echo "PASS" || echo "FAIL"

# Test 3: Large file
echo "Test 3: Large file (100MB)..."
dd if=/dev/zero of=large.dat bs=1M count=100 2>/dev/null
./build/cp_tool large.dat large_copy.dat && diff -q large.dat large_copy.dat && echo "PASS" || echo "FAIL"

# Clean up
rm -f test_copy.txt empty.txt empty_copy.txt large.dat large_copy.dat

echo "=== All Tests Completed ==="
```

Save it as `test_copy.sh`, add execute permissions: `chmod +x test_copy.sh`, and run: `./test_copy.sh`. Within a few seconds, you'll know if all functions work correctly.

## Potential Directions for Improvement

While this copier is quite practical, if we wanted to continue optimizing, we could consider:

**Multithreading:** One thread reads, one writes, passing buffers via a queue. Theoretically, this improves performance, but synchronization overhead means it isn't always faster.

**Memory Mapping:** Use `mmap` (or Windows equivalent APIs) to map files into memory, letting the OS optimize reads and writes. However, this can be problematic for huge files, and cross-platform compatibility is worse than standard streams.

**Checksums:** Calculate MD5/SHA-256 to ensure data integrity. This can be done concurrently while reading and writing without adding much time.

**Resumable Copying:** Record the copied position so that if interrupted, it can resume from the breakpoint. Very useful for huge files, but implementation is complex.

**Batch Copying:** Support copying multiple files at once, or entire directory trees. This requires recursive directory traversal and creating corresponding directory structures.

However, for a teaching example, our current implementation is sufficient. It is concise, robust, reasonably performant, and the code size isn't large. It is perfect for understanding file I/O and modern C++ features.

## Summary

Over two articles, we went from requirement analysis to interface design, from core implementation to testing and verification, completely implementing a file copier. Although it's only a couple of hundred lines of code, it's small but complete: error handling, progress feedback, performance optimization, and edge cases were all considered.

More importantly, we utilized many modern C++ features: `std::filesystem` simplifies path operations, `std::chrono` precisely measures time, `std::vector` manages buffers, RAII automatically releases resources, and exception handling gracefully reports errors. These features make writing C++ less "hardcore," significantly improving code readability and safety.

Next time you encounter a similar file operation requirement, you'll know how to approach it. Remember: think clearly about requirements, design the interface, choose the right tools, implement step-by-step, and test thoroughly. This is how engineering mindset comes about — not by pursuing flashy technology, but by solidifying every step of the process.
