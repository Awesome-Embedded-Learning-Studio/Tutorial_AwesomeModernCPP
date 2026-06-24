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
title: 'Modern C++ Engineering Practice: Building a File Copier from Scratch (Part
  2) – Core Implementation and Practical Testing'
description: ''
translation:
  source: documents/vol7-engineering/02-file-copier-core-implementation.md
  source_hash: 176618fe345c1af6325c50718efa24271dc56ac27db362223760dbb494f72252
  translated_at: '2026-06-24T01:10:39.352103+00:00'
  engine: anthropic
  token_count: 2801
---
# Modern C++ Engineering Practices — Building a File Copier from Scratch (Part 2): Core Implementation and Practical Testing

## Picking Up Where We Left Off

In the previous post, we set up the framework, opened the files, and prepared the buffers. All that remains is the most critical read-write loop. In this post, we will finish implementing the remaining core logic and write a test program to run it. Honestly, writing code without testing feels like cooking without tasting the food—it just doesn't feel right.

## Core Read-Write Loop: Simple but Robust

### Design Philosophy for the Main Loop

The core of file copying is just a loop: read a chunk, write a chunk, and repeat until finished. It sounds simple, but there are many details to consider. Let's first look at the overall structure:

```cpp
while (in) {
  in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  std::streamsize read_bytes = in.gcount();
  if (read_bytes <= 0)
    break;

  out.write(buffer.data(), read_bytes);
  if (!out) {
    std::cerr << "Write error while writing to: " << dst_path << "\n";
    return false;
  }

  copied += static_cast<std::uintmax_t>(read_bytes);

  // 进度更新逻辑...
}

```

The loop condition is `while (in)`, which utilizes the stream object's `operator bool()`. As long as the input stream remains in a good state (no errors or EOF encountered), the loop continues. This is preferable to writing `while (!in.eof())`, as the latter only checks the EOF flag and ignores other error states.

### Using `read` and `gcount` together

```cpp
in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
std::streamsize read_bytes = in.gcount();

```

The `read` method attempts to read the specified number of bytes, but it doesn't guarantee that the buffer will be filled. For example, if only 1 KB remains in the file and you request 8 KB, it will only read 1 KB. Therefore, we must immediately call `gcount()` to obtain the actual number of bytes read.

There is a minor detail regarding type conversion here: `buffer.size()` returns a `size_t`, while `read` expects a `std::streamsize` (typically `long long`). Although implicit conversion works in most cases, an explicit cast avoids compiler warnings and makes the code's intent clearer.

The check `read_bytes <= 0` serves as a safety measure. Normally, if the stream state becomes bad, the `while (in)` loop will exit, but having an extra layer of validation never hurts. This logic handles end-of-file scenarios: the final `read` operation might read zero bytes and set the EOF flag, causing `gcount()` to return zero, at which point we `break` the loop.

### write and error checking

```cpp
out.write(buffer.data(), read_bytes);
if (!out) {
  std::cerr << "Write error while writing to: " << dst_path << "\n";
  return false;
}

```

We use the actual number of bytes read, `read_bytes`, rather than `buffer.size()`. This is critical; otherwise, the last chunk of data would be padded with extraneous garbage bytes.

We check the stream status immediately after each write. If a failure is detected, we return right away. Write failures can result from a full disk, insufficient permissions, or device errors. Detecting issues early allows us to stop promptly and prevents further complications from continued writes.

### Progress Statistics

```cpp
copied += static_cast<std::uintmax_t>(read_bytes);

```

For every block written successfully, we accumulate the byte count to `copied`. We use this value later to calculate the progress percentage and speed. The type cast matches `std::uintmax_t`; although `read_bytes` cannot be negative, the compiler does not know this, so the explicit cast keeps it happy.

## Progress Bar: Making the Wait Less Painful

### Design of the `ProgressBar` Class

We encapsulate the progress bar into a separate class. This follows the single responsibility principle and makes maintenance easier:

```cpp
class ProgressBar {
public:
  explicit ProgressBar(int width = 20) : bar_width_(width) {}

  void update(std::uintmax_t copied, std::uintmax_t total,
              double speed_bytes_per_s) const;

private:
  int bar_width_;
};

```

`width` represents the character width of the progress bar, defaulting to 20 characters. Too narrow, and it lacks clarity; too wide, and it consumes screen space; 20 is a balanced compromise. The `update` method accepts the number of bytes copied, the total bytes, and the current speed, and is responsible for rendering the progress bar in the terminal.

Note that `update` is a `const` method, as it merely displays information without modifying the object's state. This `const` correctness is crucial in large projects, as it prevents many unintended modifications.

### Progress Bar Rendering Logic

```cpp
void update(std::uintmax_t copied, std::uintmax_t total,
            double speed_bytes_per_s) const {
  double fraction = (total == 0) ? 1.0 : static_cast<double>(copied) / total;
  int filled = static_cast<int>(fraction * bar_width_);

  std::cout << "[";
  for (int i = 0; i < filled; ++i)
    std::cout << "=";
  if (filled < bar_width_)
    std::cout << ">";
  for (int i = filled + 1; i < bar_width_; ++i)
    std::cout << " ";
  std::cout << "] ";

  // ...
}

```

First, we calculate the completion ratio `fraction`, then multiply it by the width to determine how many characters should be filled. We handle the division-by-zero case here—if the file is empty, we treat it as 100% complete.

The progress bar style is `[=====>     ]`. We use `=` for completed parts, `>` for the current position, and spaces for the unfinished parts. Three loops draw these three parts respectively; this is simple and direct. Although we could use `std::string` concatenation and output it all at once, direct output is actually more efficient for scenarios with frequent updates like this.

### Percentage and Size Display

```cpp
double percent = fraction * 100.0;
double copied_mb = static_cast<double>(copied) / (1024.0 * 1024.0);
double total_mb = static_cast<double>(total) / (1024.0 * 1024.0);

std::cout << std::fixed << std::setprecision(1) << percent << "% | "
          << copied_mb << "MB/" << total_mb << "MB | "
          << (speed_bytes_per_s / (1024.0 * 1024.0)) << "MB/s | ETA: ";

```

Convert bytes to megabytes for a more human-readable display. `std::fixed` and `std::setprecision(1)` ensure floating-point numbers retain one decimal place, displaying `45.3%` instead of `45.283746%`. These I/O manipulators are old friends in C++; while the syntax is somewhat verbose, they are quite practical.

We also divide speed by `1024.0 * 1024.0` to convert it to MB/s. Note that we use 1024 rather than 1000 here, because "mega" in computing is binary-based: 1 MB = 1024 KB = 1024 * 1024 bytes. Although the IEC standard (using 1000, distinguishing MiB from MB) exists, using 1024 aligns better with programmer habits for internal displays.

### ETA Calculation: Estimating Remaining Time

```cpp
double eta_seconds = 0.0;
if (speed_bytes_per_s > 1e-6 && copied < total)
  eta_seconds = static_cast<double>(total - copied) / speed_bytes_per_s;

if (copied >= total) {
  std::cout << "0s";
} else if (eta_seconds >= 3600) {
  int h = static_cast<int>(eta_seconds) / 3600;
  int m = (static_cast<int>(eta_seconds) % 3600) / 60;
  std::cout << h << "h " << m << "m";
} else if (eta_seconds >= 60) {
  int m = static_cast<int>(eta_seconds) / 60;
  int s = static_cast<int>(eta_seconds) % 60;
  std::cout << m << "m " << s << "s";
} else {
  int s = static_cast<int>(eta_seconds + 0.5);
  std::cout << s << "s";
}

```

ETA (Estimated Time of Arrival) is calculated by dividing the remaining bytes by the current speed. This estimate fluctuates with speed variations, but generally provides the user with a helpful expectation.

We check `speed_bytes_per_s > 1e-6` to prevent division by zero. `1e-6` is a sufficiently small threshold; essentially, any non-zero speed will exceed it.

The display format falls into three cases: if over one hour, show "Xh Ym"; if over one minute, show "Xm Ys"; otherwise, show only seconds. This tiered display is far more intuitive than a uniform seconds count—would you rather see "2h 15m" or "8100s"?

### The Utility of the Carriage Return

```cpp
std::cout << '\r' << std::flush;

```

The entire `update` method outputs a carriage return `\r` at the end, rather than a newline character `\n`. The carriage return moves the cursor back to the beginning of the line, so the next output will overwrite the current line. This is the secret behind the progress bar's "dynamic update."

`std::flush` forces the output buffer to refresh; otherwise, the output might be cached, and the user would not see real-time progress updates.

## Time and Speed Calculation

### Controlling the Update Frequency

```cpp
auto now = std::chrono::steady_clock::now();
std::chrono::duration<double> since_last = now - last_report;
if (since_last.count() >= 0.1 || copied == total) {
  std::chrono::duration<double> elapsed = now - t_start;
  double speed = (elapsed.count() > 1e-9)
                     ? (static_cast<double>(copied) / elapsed.count())
                     : 0.0;
  bar.update(copied, total_size, speed);
  last_report = now;
}

```

We do not update the progress bar after every read or write block. Instead, we update it at intervals of at least 0.1 seconds. Why? Because updating the progress bar itself incurs overhead. Doing it too frequently can actually slow down the copy speed. Furthermore, the human eye cannot distinguish such high update frequencies; 0.1 seconds (10 times per second) is sufficiently smooth.

`now - last_report` yields a `duration` object, and calling `count()` returns the number of seconds (as a `double`). The type safety of the `chrono` library is evident here: different time points and durations have distinct types, preventing confusion.

We calculate the speed by dividing the number of bytes copied by the total elapsed time. Note the check for `elapsed.count() > 1e-9`. While it theoretically shouldn't be zero, with floating-point arithmetic, defensive programming is always good practice.

We specifically handle the `copied == total` case to ensure the progress bar updates once when the copy is complete, displaying 100%.

## Cleanup

### Flushing and Closing

```cpp
out.flush();
out.close();
in.close();

```

After writing all the data, we explicitly call `flush()` to ensure the buffer contents are written to disk. Although `close()` flushes automatically, calling it explicitly is safer, allowing us to detect failures immediately.

`close()` is not strictly necessary, as the destructor closes the file automatically. However, explicitly closing makes the intent clearer and releases the file handle earlier, which is important on some operating systems.

### Final Progress and Validation

```cpp
auto t_end = std::chrono::steady_clock::now();
std::chrono::duration<double> total_elapsed = t_end - t_start;
double avg_speed = (total_elapsed.count() > 1e-9)
                      ? (static_cast<double>(copied) / total_elapsed.count())
                      : 0.0;
bar.update(copied, total_size, avg_speed);
std::cout << "\n";

std::uintmax_t dst_size = fs::file_size(dst_path);
if (dst_size != total_size) {
  std::cerr << "Size mismatch after copy. src=" << total_size
            << " dst=" << dst_size << "\n";
  return false;
}

```

Finally, we update the progress bar one last time using the average speed, followed by a newline. This keeps the progress bar on the screen, allowing the user to see the final statistics.

The verification phase is straightforward: we simply check if the target file size matches the source file size. This isn't foolproof (theoretically, data corruption could occur without a size change), but it is sufficient for most error scenarios. If stricter verification is required, we could calculate an MD5 or SHA-256 checksum, but that would significantly increase the processing time.

## Practical Usage

### Writing the `main` Function

We need a simple test program to call this copier:

```cpp
// --- File: main.cpp ---
#include "fcopy.h"
#include <iostream>

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <source> <destination>\n";
    return 1;
  }

  FileCopier copier;

  std::cout << "Copying " << argv[1] << " to " << argv[2] << "...\n";

  if (copier.copy(argv[1], argv[2])) {
    std::cout << "Copy succeeded!\n";
    return 0;
  } else {
    std::cerr << "Copy failed!\n";
    return 1;
  }
}

```

That's all there is to it. We check the number of command-line arguments, create a `FileCopier` object, call the `copy` method, and determine the exit code based on the return value. This follows standard Unix program style: return 0 for success, and non-zero for failure.

### Compilation Command

Assume your file structure looks like this:

```cpp

fcopy.h        // FileCopier类声明
fcopy.cpp      // FileCopier实现(包括ProgressBar)
main.cpp       // 测试程序

```

Build command:

```bash
g++ -std=c++17 -O2 -Wall -Wextra main.cpp fcopy.cpp -o fcopy

```

Here is an explanation of a few compiler options: `-std=c++17` specifies the C++17 standard (because we use `filesystem`), `-O2` enables optimization, `-Wall -Wextra` turns on warnings (to help us spot potential issues), and `-o` specifies the output filename.

If you are using an older GCC version (prior to 9.0), we may need to link `stdc++fs` explicitly:

```bash
g++ -std=c++17 -O2 -Wall -Wextra main.cpp fcopy.cpp -o fcopy -lstdc++fs

```

For Clang users, simply replace `g++` with `clang++`; everything else remains the same.

### Basic Testing

First, let's test copying a small file:

```bash
./fcopy /etc/hosts hosts_backup

```

You should see the progress bar flash by (because the file is too small), followed by "Copy succeeded!". Let's use `ls -lh` to compare the sizes, or the `diff` command to verify that the content is identical:

```bash
diff /etc/hosts hosts_backup

```

No output means they are identical, which is perfect.

### Testing Large Files

Small files don't reveal much, so we need to find a larger one. If you don't have one handy, we can generate one using the `dd` command:

```bash
dd if=/dev/urandom of=test_1gb.dat bs=1M count=1024

```

This creates a 1 GB random data file. Then, we copy it:

```bash
./fcopy test_1gb.dat test_1gb_copy.dat

```

Now we can see the progress bar advancing slowly, the speed display, and the ETA countdown, making the experience feel just like a download manager. Once the copy is complete, let's verify it:

```bash
md5sum test_1gb.dat test_1gb_copy.dat

```

The two MD5 values should be identical.

### Boundary Case Testing

Good tests should cover boundary cases:

**Empty file:**

```bash
touch empty.txt
./fcopy empty.txt empty_copy.txt

```

It should process correctly, with the progress bar directly showing 100%.

**Non-existent source file:**

```bash
./fcopy nonexistent.txt output.txt

```

It should output "Source file does not exist" and return a failure.

**Target without write permission:**

```bash
./fcopy /etc/hosts /root/cannot_write.txt

```

It should output "Failed to open destination file for writing" (assuming you are not root).

**Out of disk space:** This is difficult to simulate, but if it actually occurs, the write phase will fail and return an error.

### Performance Testing

Want to know how this copier performs? We can compare it with the system's `cp` command:

```bash
time ./fcopy test_1gb.dat copy1.dat
time cp test_1gb.dat copy2.dat

```

On my machine, the performance of both is similar, hovering around 1-2 GB/s (depending on disk performance). This indicates that our implementation is reasonably efficient, without significant performance overhead.

If you want to optimize, try increasing `chunk_size`:

```cpp
FileCopier copier(1024 * 1024);  // 1MB chunk

```

In certain scenarios, larger blocks can reduce the number of system calls and improve performance. However, bigger isn't always better; if the block size is too large, it puts pressure on memory, and if the process is interrupted midway, the data already written will be relatively "coarse".

### A Complete Test Script

Let's write a shell script to automate these tests:

```bash
#!/bin/bash

echo "=== File Copier Test Suite ==="

# Create test files
echo "Creating test files..."
dd if=/dev/zero of=test_small.dat bs=1K count=100 2>/dev/null
dd if=/dev/urandom of=test_medium.dat bs=1M count=100 2>/dev/null

# Test 1: Small file
echo -e "\n[Test 1] Small file (100KB)"
./fcopy test_small.dat test_small_copy.dat
if diff test_small.dat test_small_copy.dat > /dev/null; then
  echo "✓ Small file test passed"
else
  echo "✗ Small file test failed"
fi

# Test 2: Medium file
echo -e "\n[Test 2] Medium file (100MB)"
./fcopy test_medium.dat test_medium_copy.dat
md5_orig=$(md5sum test_medium.dat | awk '{print $1}')
md5_copy=$(md5sum test_medium_copy.dat | awk '{print $1}')
if [ "$md5_orig" = "$md5_copy" ]; then
  echo "✓ Medium file test passed"
else
  echo "✗ Medium file test failed"
fi

# Test 3: Empty file
echo -e "\n[Test 3] Empty file"
touch test_empty.dat
./fcopy test_empty.dat test_empty_copy.dat
if [ -f test_empty_copy.dat ] && [ ! -s test_empty_copy.dat ]; then
  echo "✓ Empty file test passed"
else
  echo "✗ Empty file test failed"
fi

# Test 4: Non-existent source
echo -e "\n[Test 4] Non-existent source"
if ! ./fcopy nonexistent.dat output.dat 2>/dev/null; then
  echo "✓ Error handling test passed"
else
  echo "✗ Error handling test failed"
fi

# Cleanup
echo -e "\n Cleaning up..."
rm -f test_*.dat test_*_copy.dat

echo -e "\n=== All tests completed ==="

```

Save this as `test_fcopy.sh`, grant execute permissions with `chmod +x test_fcopy.sh`, and run it using `./test_fcopy.sh`. Within seconds, we will know if all features are working correctly.

## Potential Improvements

While this copier is quite practical, we could consider the following optimizations:

**Multithreading**: We could use one thread for reading and another for writing, passing buffers via a queue. Theoretically, this improves performance, but synchronization overhead means it isn't always faster.

**Memory Mapping**: We could use `mmap` (or its Windows equivalent API) to map files into memory, letting the operating system optimize reads and writes. However, this can be problematic for very large files and is less portable than `fstream`.

**Checksums**: We could calculate MD5 or SHA-256 to ensure data integrity. This can be done concurrently with reading and writing without adding significant time.

**Resumable Copying**: We could record the copied position to allow resuming from a breakpoint after interruption. This is useful for very large files, but implementation is more complex.

**Batch Copying**: We could support copying multiple files at once or entire directory trees. This requires recursively traversating directories and creating the corresponding directory structure.

However, for a teaching example, our current implementation is sufficient. It is concise, robust, and reasonably performant, with a small codebase, making it perfect for understanding file I/O and modern C++ features.

## Summary

Over these two articles, we have implemented a fully functional file copier, covering everything from requirements analysis and interface design to core implementation and testing verification. Although it is only about two hundred lines of code, it is complete in its own right: error handling, progress feedback, performance optimization, and edge cases have all been considered.

More importantly, we utilized many modern C++ features: `std::filesystem` for path manipulation, `std::chrono` for precise timing, `std::vector` for buffer management, RAII for automatic resource release, and exception handling for graceful error reporting. These features make C++ less "hardcore," significantly improving code readability and safety.

Next time you encounter a similar file operation requirement, you will know exactly how to approach it. Remember: clarify requirements, design interfaces, select the right tools, implement step-by-step, and test thoroughly. This is how engineering mindset is developed—not by pursuing flashy technologies, but by solidly executing every step of the process.
