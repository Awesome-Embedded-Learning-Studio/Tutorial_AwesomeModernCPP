# Modern C++ Engineering Practice — Building a File Copier from Scratch (Part 2): Core Implementation and Practical Testing

## Picking Up Where We Left Off

In the previous article, we set up the framework, opened the files, and prepared the buffer. All that remains is the most critical read-write loop. In this article, we will finish implementing the core logic and write a test program to try it out. Honestly, writing code without testing it is like cooking without tasting — it just doesn't feel right.

## Core Read-Write Loop: Simple but Not Simplistic

### Design of the Main Loop

The core of file copying is a loop: read a chunk, write a chunk, until everything is read. It sounds simple, but there are quite a few details. Let's look at the overall structure first:

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

The loop condition is `while (in)`, which uses the stream object's `operator bool()`. As long as the input stream is in a good state (no errors or EOF encountered), the loop continues. This is better than writing `while (!in.eof())`, because the latter only checks the EOF flag and ignores other error states.

### Coordinating `read` and `gcount`

```cpp
in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
std::streamsize read_bytes = in.gcount();

```

The `read` method attempts to read a specified number of bytes, but it might not fill the buffer. For example, if only 1KB is left in the file and you ask it to read 8KB, it can only read 1KB. Therefore, we immediately call `gcount()` to get the actual number of bytes read.

There is a minor detail regarding type conversion here: `buffer.size()` returns a `size_t`, while `read` expects a `std::streamsize` (usually `long long`). Although implicit conversion works fine in most cases, an explicit cast avoids compiler warnings and makes the code's intent clearer.

The `read_bytes <= 0` check is a safety measure. Under normal circumstances, if the stream state goes bad, `while (in)` will exit the loop, but an extra layer of checking never hurts. End-of-file handling works like this: the final `read` might read 0 bytes and set the EOF flag, then `gcount()` returns 0, and we `break` it.

### Write and Error Checking

```cpp
out.write(buffer.data(), read_bytes);
if (!out) {
  std::cerr << "Write error while writing to: " << dst_path << "\n";
  return false;
}

```

We write the actual number of bytes read, `read_bytes`, rather than `buffer.size()`. This is crucial; otherwise, the last chunk of data will have extra garbage bytes written.

We check the stream state immediately after each write. If a write failure is detected, we return right away. Reasons for write failure could include a full disk, insufficient permissions, or a device error. Detecting it early and stopping early prevents further writes from causing more issues.

### Progress Tracking

```cpp
copied += static_cast<std::uintmax_t>(read_bytes);

```

For every successfully written chunk, we accumulate the byte count into `copied`. This value will be used later to calculate the progress percentage and speed. The type conversion is again to match `std::uintmax_t`. Although `read_bytes` will never be negative, the compiler doesn't know that, and an explicit cast gives it peace of mind.

## Progress Bar: Making the Wait Less Agonizing

### Design of the `ProgressBar` Class

The progress bar is encapsulated in its own class with a single responsibility, making it easy to maintain:

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

`width` is the character width of the progress bar, defaulting to 20 characters. Too narrow is not intuitive, too wide takes up too much space, and 20 is a good compromise. The `update` method takes the number of bytes copied, total bytes, and current speed, and is responsible for drawing the progress bar on the terminal.

Note that `update` is a `const` method because it only displays information and does not modify the object's state. This kind of const correctness is very important in large projects and can prevent many accidental modifications.

### Drawing Logic of the Progress Bar

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

First, we calculate the completion ratio `fraction`, then multiply it by the width to determine how many characters should be filled. We handle the division-by-zero case here — an empty file is simply treated as 100% complete.

The progress bar style is `[=====>     ]`, using `=` for the completed portion, `>` for the current position, and spaces for the uncompleted portion. Three separate loops draw these three parts — simple and direct. Although we could use `std::string` concatenation and output it all at once, direct output is actually more efficient for scenarios with frequent updates.

### Percentage and Size Display

```cpp
double percent = fraction * 100.0;
double copied_mb = static_cast<double>(copied) / (1024.0 * 1024.0);
double total_mb = static_cast<double>(total) / (1024.0 * 1024.0);

std::cout << std::fixed << std::setprecision(1) << percent << "% | "
          << copied_mb << "MB/" << total_mb << "MB | "
          << (speed_bytes_per_s / (1024.0 * 1024.0)) << "MB/s | ETA: ";

```

Converting bytes to MB for display is more user-friendly. `std::fixed` and `std::setprecision(1)` make the floating-point number keep one decimal place, displaying `45.3%` instead of `45.283746%`. These I/O manipulators are old friends in C++; although the syntax is a bit verbose, they are very practical.

Speed is also divided by `1024.0 * 1024.0` to convert to MB/s. Note that we use 1024 here instead of 1000, because "mega" in computing is binary: 1MB = 1024KB = 1024*1024 bytes. Although there is now the IEC standard using 1000 (MiB vs MB), using 1024 for internal display better aligns with programmer habits.

### ETA Calculation: Remaining Time Estimation

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

ETA (Estimated Time of Arrival) is calculated by dividing the remaining bytes by the current speed. This estimate will fluctuate with speed variations, but overall it gives users a psychological expectation.

We check `speed_bytes_per_s > 1e-6` to avoid division-by-zero errors. `1e-6` is a small enough number that basically any non-zero speed will be greater than it.

The display format is divided into three cases: over one hour displays "Xh Ym", over one minute displays "Xm Ys", otherwise it only displays seconds. This tiered display is much more intuitive than uniformly using seconds — would you rather see "2h 15m" or "8100s"?

### The Magic of the Carriage Return

```cpp
std::cout << '\r' << std::flush;

```

At the very end of the `update` method, a carriage return `\r` is output instead of a newline `\n`. The carriage return moves the cursor back to the beginning of the line, so the next output will overwrite this line. This is the secret behind the progress bar's "dynamic update."

`std::flush` forces the output buffer to flush; otherwise, the output might be cached, and users wouldn't see real-time progress changes.

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

We don't update the progress bar for every read-write chunk; instead, we update it at intervals of at least 0.1 seconds. Why? Because updating the progress bar itself has overhead, and doing it too frequently will actually slow down the copy speed. Moreover, the human eye can't distinguish such high update frequencies anyway. 0.1 seconds (10 times per second) is already smooth enough.

`now - last_report` yields a `duration` object, and calling `count()` gives the duration in seconds (a `double`). The type safety of the `chrono` library shines here: different time points and durations have different types, so they can't be mixed up.

Speed is calculated by dividing the number of bytes copied by the total elapsed time. Note that we check `elapsed.count() > 1e-9`; although theoretically it shouldn't be zero, with floating-point math, defensive programming is always good.

We specially handle the `copied == total` case to ensure the progress bar is updated at least once when copying finishes, displaying 100%.

## Wrapping Up

### Flushing and Closing

```cpp
out.flush();
out.close();
in.close();

```

After writing all data, we explicitly call `flush()` to ensure the buffer contents are written to disk. Although `close()` will automatically flush, an explicit call is safer, and if the flush fails, we can detect it in time.

`close()` isn't strictly necessary because the destructor will automatically close the file. However, explicitly closing makes the code's intent clearer and can release the file handle earlier, which is important on some operating systems.

### Final Progress and Verification

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

We update the progress bar one last time using the average speed, then output a newline. This way, the progress bar remains on the screen, and users can see the final statistics.

The verification phase is quite simple: we just check whether the destination file's size matches the source file's. This isn't foolproof (theoretically, data could be corrupted but retain the same size), but it's sufficient for most error scenarios. If you have higher requirements, you could calculate an MD5 or SHA-256 checksum, but that would significantly increase the time.

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

It's that simple. We check the number of command-line arguments, create a `FileCopier` object, call the `copy` method, and determine the exit code based on the return value. Standard Unix program style: return 0 for success, non-zero for failure.

### Compilation Command

Assuming your file structure looks like this:

```cpp

fcopy.h        // FileCopier类声明
fcopy.cpp      // FileCopier实现(包括ProgressBar)
main.cpp       // 测试程序

```

The compilation command is:

```bash
g++ -std=c++17 -O2 -Wall -Wextra main.cpp fcopy.cpp -o fcopy

```

Let's explain a few compiler flags: `-std=c++17` specifies the C++17 standard (because we use `filesystem`), -O2 enables optimization, -Wall -Wextra turn on warnings (to help you spot potential issues), and -o specifies the output file name.

If you are using an older GCC version (before 9.0), you might need to link `stdc++fs` additionally:

```bash
g++ -std=c++17 -O2 -Wall -Wextra main.cpp fcopy.cpp -o fcopy -lstdc++fs

```

Clang users just need to replace `g++` with `clang++`; everything else is the same.

### Basic Testing

Let's test copying a small file first:

```bash
./fcopy /etc/hosts hosts_backup

```

You should see the progress bar flash by (the file is too small), and then display "Copy succeeded!". Use `ls -lh` to compare the sizes, or the `diff` command to verify the contents are identical:

```bash
diff /etc/hosts hosts_backup

```

No output means they are exactly the same — perfect.

### Testing with a Large File

Small files don't really test anything, so we need to find a larger one. If you don't have one handy, you can generate one using the `dd` command:

```bash
dd if=/dev/urandom of=test_1gb.dat bs=1M count=1024

```

This creates a 1GB file of random data. Then copy it:

```bash
./fcopy test_1gb.dat test_1gb_copy.dat

```

Now you can see the progress bar slowly advancing, the speed display, and the ETA countdown. The whole experience feels just like a download manager. After copying is complete, verify it:

```bash
md5sum test_1gb.dat test_1gb_copy.dat

```

The two MD5 values should be completely identical.

### Edge Case Testing

Good testing should cover edge cases:

**Empty file:**

```bash
touch empty.txt
./fcopy empty.txt empty_copy.txt

```

It should be handled normally, with the progress bar directly showing 100%.

**Non-existent source file:**

```bash
./fcopy nonexistent.txt output.txt

```

It should output "Source file does not exist" and return a failure.

**Destination without write permission:**

```bash
./fcopy /etc/hosts /root/cannot_write.txt

```

It should output "Failed to open destination file for writing" (assuming you are not root).

**Insufficient disk space:** This is a bit hard to simulate, but if you actually encounter it, the write phase will fail and return an error.

### Performance Testing

Want to know how this copier performs? You can compare it with the system's `cp` command:

```bash
time ./fcopy test_1gb.dat copy1.dat
time cp test_1gb.dat copy2.dat

```

On my machine, the speeds of the two are similar, both around 1-2GB/s (depending on disk performance). This shows that our implementation has decent efficiency with no obvious performance penalty.

If you want to optimize, you can try increasing `chunk_size`:

```cpp
FileCopier copier(1024 * 1024);  // 1MB chunk

```

In certain scenarios, larger chunks can reduce the number of system calls and improve performance. But bigger isn't always better; too large puts pressure on memory, and if interrupted midway, the already-written data will be rather "coarse."

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

Save it as `test_fcopy.sh`, add execute permissions: `chmod +x test_fcopy.sh`, and then run it: `./test_fcopy.sh`. Within a few seconds, you'll know if all features are working properly.

## Potential Areas for Improvement

Although this copier is already quite practical, if you want to continue optimizing, you could consider:

**Multithreading:** You could have one thread reading and another writing, passing buffers via a queue, which could theoretically improve performance. But be mindful of synchronization overhead — it won't always be faster.

**Memory mapping:** Use `mmap` (or the Windows equivalent API) to map the file into memory, letting the operating system optimize the reads and writes. However, this can be problematic for extremely large files, and its cross-platform compatibility isn't as good as `fstream`.

**Checksums:** Calculate MD5/SHA-256 to ensure data integrity. This can be done concurrently with reading and writing without adding much time.

**Resumable copying:** Record the copied position so that if interrupted, it can resume from the breakpoint. This is very useful for extremely large files, but the implementation is more complex.

**Batch copying:** Support copying multiple files at once, or an entire directory tree. This would require recursively traversing directories and creating the corresponding directory structure.

But for a teaching example, our current implementation is already sufficient. It is concise, robust, and reasonably performant, with a small codebase, making it perfect for understanding file I/O and modern C++ features.

## Summary

Over these two articles, we went from requirements analysis to interface design, from core implementation to test verification, completely building a file copier. Although it's only a little over two hundred lines of code, it's small but complete: error handling, progress feedback, performance optimization, and edge cases — we considered everything we needed to.

More importantly, we utilized quite a few modern C++ features: `std::filesystem` to simplify path operations, `std::chrono` for precise time measurement, `std::vector` to manage the buffer, RAII to automatically release resources, and exception handling for elegant error reporting. These features make writing C++ no longer so "hardcore," elevating both code readability and safety to a new level.

Next time you encounter a similar file operation requirement, you'll know how to approach it. Remember: think through the requirements first, design the interface, choose the right tools, implement it step by step, and finally, test it thoroughly. That's how engineering thinking develops — not by pursuing flashy techniques, but by solidly executing every step of the process.
