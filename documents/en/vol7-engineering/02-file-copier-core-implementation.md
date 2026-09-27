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
title: 'Modern C++ in Practice — Building a File Copier from Scratch (Part 2): Core Implementation and Practical Testing'
description: ''
translation:
  source: documents/vol7-engineering/02-file-copier-core-implementation.md
  source_hash: 176618fe345c1af6325c50718efa24271dc56ac27db362223760dbb494f72252
  translated_at: '2026-09-27T02:31:25+00:00'
  engine: anthropic
  token_count: 8000
---
# Modern C++ in Practice — Building a File Copier from Scratch (Part 2): Core Implementation and Practical Testing

## Picking Up Where We Left Off

In the previous article we got the framework standing: files open, buffers ready — everything except the most critical piece, the read/write loop. This time we finish the remaining core logic and then write a test program to see it run. Honestly, writing code without testing it is like cooking without tasting the dish — it never feels quite settled.

## The Core Read/Write Loop: Simple but Not Simplistic

### Designing the Main Loop

The heart of file copying is a loop: read a chunk, write a chunk, repeat until everything has been read. It sounds simple, but there are quite a few details. Let's look at the overall structure first:

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

  // Progress update logic...
}

```

The loop condition is `while (in)`, which puts the stream object's `operator bool()` to work. As long as the input stream is still in a good state (no error, no EOF), the loop keeps going. This is better than writing `while (!in.eof())`, because the latter only checks the EOF flag and none of the other error states.

### Using read and gcount Together

```cpp
in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
std::streamsize read_bytes = in.gcount();

```

The `read` method attempts to read the requested number of bytes, but it may come up short. If only 1 KB is left in the file and you ask for 8 KB, you get 1 KB. That is why the very next call is `gcount()`, which reports how many bytes were actually read.

There is a small type-conversion detail here: `buffer.size()` returns `size_t`, while `read` wants a `std::streamsize` (usually `long long`). Implicit conversion is fine most of the time, but the explicit cast avoids compiler warnings and makes the intent clearer.

The `read_bytes <= 0` check is a safety net. Under normal circumstances a bad stream state would already have pulled `while (in)` out of the loop, but one more layer of checking never hurts. End-of-file is handled exactly this way: the final `read` may come back with 0 bytes and set the EOF flag, `gcount()` then returns 0, and we `break` out.

### write and Error Checking

```cpp
out.write(buffer.data(), read_bytes);
if (!out) {
  std::cerr << "Write error while writing to: " << dst_path << "\n";
  return false;
}

```

What gets written is `read_bytes` — the number of bytes actually read — not `buffer.size()`. That detail is critical: otherwise the last chunk would be padded with extra garbage bytes.

After every write we immediately check the stream state and return the moment a write fails. Writes fail for all sorts of reasons: disk full, insufficient permissions, a device error. Catch it early, stop early — do not keep writing and dig a deeper hole.

### Progress Accounting

```cpp
copied += static_cast<std::uintmax_t>(read_bytes);

```

After each successfully written chunk, the byte count is accumulated into `copied`. This value later feeds the progress percentage and the speed display. The cast is again there to match `std::uintmax_t`; `read_bytes` can never be negative, but the compiler does not know that, and the explicit conversion keeps it at ease.

## The Progress Bar: Making the Wait Less Painful

### Designing the ProgressBar Class

The progress bar is wrapped in a class of its own — one responsibility, easy to maintain:

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

`width` is the character width of the bar, 20 by default. Narrower is hard to read, wider hogs the screen — 20 is a workable middle ground. The `update` method takes the bytes copied so far, the total number of bytes, and the current speed, and draws the bar in the terminal.

Note that `update` is a `const` method: it only displays information and never modifies object state. This kind of const correctness matters a lot in larger projects, where it rules out plenty of accidental modifications.

### How the Bar Is Drawn

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

First we compute the completion ratio `fraction`, then multiply it by the width to see how many characters to fill. The divide-by-zero case is handled here — an empty file simply counts as 100% done.

The bar looks like `[=====>     ]`: `=` for the finished part, `>` for the current position, spaces for the rest. Three loops draw those three parts — plain and direct. You could assemble a `std::string` and print it in one shot, but for something refreshed this often, writing directly is actually more efficient.

### Percentage and Size Display

```cpp
double percent = fraction * 100.0;
double copied_mb = static_cast<double>(copied) / (1024.0 * 1024.0);
double total_mb = static_cast<double>(total) / (1024.0 * 1024.0);

std::cout << std::fixed << std::setprecision(1) << percent << "% | "
          << copied_mb << "MB/" << total_mb << "MB | "
          << (speed_bytes_per_s / (1024.0 * 1024.0)) << "MB/s | ETA: ";

```

Byte counts are converted to MB for display — friendlier for humans. `std::fixed` and `std::setprecision(1)` keep one decimal place, so you see `45.3%` instead of `45.283746%`. These I/O manipulators are old friends in C++; the syntax is a bit wordy, but they are genuinely useful.

Speed is likewise divided by `1024.0 * 1024.0` to become MB/s. Note the 1024 rather than 1000: a computer's "mega" is binary — 1MB = 1024KB = 1024*1024 bytes. There is an IEC standard that uses 1000 (MiB vs MB), but for internal display like this, 1024 matches programmer habits better.

### ETA Calculation: Estimating the Remaining Time

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

The ETA (Estimated Time of Arrival) is just the remaining bytes divided by the current speed. The estimate wobbles as the speed wobbles, but overall it gives the user a sense of what to expect.

The `speed_bytes_per_s > 1e-6` check avoids a division by zero. `1e-6` is small enough that essentially any real speed clears it.

The display falls into three tiers: above an hour shows "Xh Ym", above a minute shows "Xm Ys", otherwise just the seconds. This kind of tiered display is far more intuitive than a flat seconds count — would you rather see "2h 15m" or "8100s"?

### The Carriage Return Trick

```cpp
std::cout << '\r' << std::flush;

```

At the very end, the whole `update` method emits a carriage return `\r` rather than a newline `\n`. The carriage return moves the cursor back to the start of the line, so the next output overwrites it — that is the entire secret of the bar's "dynamic updates".

`std::flush` forces the output buffer out; otherwise the output might sit in a buffer somewhere and the user would never see the progress move in real time.

## Time and Speed Calculation

### Throttling the Update Frequency

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

The bar is not refreshed on every chunk read or written; updates wait at least 0.1 seconds apart. Why? Because redrawing the bar costs something in itself, and doing it too often actually drags the copy down. Besides, the human eye cannot tell the difference at higher frequencies — 0.1 seconds (10 times per second) is already perfectly smooth.

`now - last_report` yields a `duration` object; calling `count()` on it gives the seconds as a `double`. This is where `chrono`'s type safety shows itself: time points and durations are distinct types, so they cannot get mixed up.

Speed is the bytes copied so far divided by the total elapsed time. Note the `elapsed.count() > 1e-9` check — in theory it cannot be zero, but with floating-point arithmetic, defensive programming always pays off.

The `copied == total` case gets special handling so that when the copy finishes, the bar is guaranteed one final refresh showing 100%.

## Wrapping Up

### Flushing and Closing

```cpp
out.flush();
out.close();
in.close();

```

Once all the data is written, `flush()` is called explicitly to make sure everything buffered reaches the disk. `close()` would flush automatically, but doing it explicitly is safer — if the flush fails, we find out immediately.

`close()` is not strictly required, since the destructor closes the file anyway. But closing explicitly makes the intent clearer and releases the file handle early, which matters on some operating systems.

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

One last bar update with the average speed, then a newline. That way the bar stays on screen and the user can see the final statistics.

The verification stage is deliberately simple: check that the destination file's size matches the source. This is not bulletproof (the data could in theory be corrupted while keeping the same size), but it covers most failure scenarios. If you need stronger guarantees, compute an MD5 or SHA-256 checksum — at a noticeable cost in runtime.

## Putting It to Work

### Writing the main Function

We need a small test program to drive the copier:

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

That is all there is to it: check the argument count, create a `FileCopier` object, call `copy`, and let the return value decide the exit code. Classic Unix program style — 0 for success, non-zero for failure.

### The Build Command

Suppose your files are laid out like this:

```cpp

fcopy.h        // FileCopier class declaration
fcopy.cpp      // FileCopier implementation (including ProgressBar)
main.cpp       // test program

```

The build command:

```bash
g++ -std=c++17 -O2 -Wall -Wextra main.cpp fcopy.cpp -o fcopy

```

A quick word on the flags: `-std=c++17` selects the C++17 standard (we use `filesystem`), -O2 turns on optimization, -Wall -Wextra enable warnings (they help you spot latent problems), and -o names the output file.

On older GCC versions (before 9.0) you may additionally need to link `stdc++fs`:

```bash
g++ -std=c++17 -O2 -Wall -Wextra main.cpp fcopy.cpp -o fcopy -lstdc++fs

```

Clang users can simply swap `g++` for `clang++`; everything else stays the same.

### Basic Testing

Start by copying a small file:

```bash
./fcopy /etc/hosts hosts_backup

```

You should see the progress bar flash by (the file is tiny), followed by "Copy succeeded!". Compare the sizes with `ls -lh`, or use `diff` to verify the contents match:

```bash
diff /etc/hosts hosts_backup

```

No output means they are identical, byte for byte — perfect.

### Testing a Large File

Small files do not prove much; we need something bigger. If you do not have one at hand, `dd` can conjure one up:

```bash
dd if=/dev/urandom of=test_1gb.dat bs=1M count=1024

```

This creates a 1GB file of random data. Now copy it:

```bash
./fcopy test_1gb.dat test_1gb_copy.dat

```

This time you can watch the bar creep forward, the speed readout, and the ETA counting down — the whole experience feels like a download manager. Once the copy finishes, verify it:

```bash
md5sum test_1gb.dat test_1gb_copy.dat

```

The two MD5 values should match exactly.

### Edge-Case Testing

Good tests cover the edge cases:

**Empty file:**

```bash
touch empty.txt
./fcopy empty.txt empty_copy.txt

```

It should be handled gracefully, with the bar jumping straight to 100%.

**Non-existent source file:**

```bash
./fcopy nonexistent.txt output.txt

```

It should print "Source file does not exist" and return a failure.

**Destination without write permission:**

```bash
./fcopy /etc/hosts /root/cannot_write.txt

```

It should print "Failed to open destination file for writing" (assuming you are not root).

**Out of disk space:** this one is hard to simulate, but if it ever happens for real, the write phase fails and returns an error.

### Performance Testing

Curious how this copier performs? Compare it against the system's `cp` command:

```bash
time ./fcopy test_1gb.dat copy1.dat
time cp test_1gb.dat copy2.dat

```

On my machine the two land in the same ballpark, both around 1-2GB/s (depending on the disk). That tells us our implementation is reasonably efficient, with no obvious performance penalty.

If you want to tune it, try a larger `chunk_size`:

```cpp
FileCopier copier(1024 * 1024);  // 1MB chunk

```

In some scenarios a bigger chunk means fewer system calls and better performance. But bigger is not automatically better: large chunks put pressure on memory, and if the copy is interrupted partway, the already-written data ends at a coarser boundary.

### A Complete Test Script

Let's wrap these tests in a shell script and automate them:

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

Save it as `test_fcopy.sh`, make it executable with `chmod +x test_fcopy.sh`, and run `./test_fcopy.sh`. Within seconds you will know whether everything works.

## Possible Directions for Improvement

The copier is already quite usable, but if you want to keep optimizing, here is what to consider:

**Multithreading**: one thread reads while another writes, with buffers handed over through a queue; in theory this can raise performance. Watch out for synchronization overhead, though — it is not always a net win.

**Memory mapping**: map the file into memory with `mmap` (or the Windows equivalent API) and let the operating system optimize the reads and writes. This can be troublesome for extremely large files, though, and it is less portable than `fstream`.

**Checksums**: compute MD5/SHA-256 to guarantee data integrity. It can be done alongside the reading and writing without adding much time.

**Resumable copies**: record how much has been copied so that an interrupted job can resume from where it stopped. Very useful for huge files, but more involved to implement.

**Batch copying**: copy several files at once, or an entire directory tree. That calls for recursive directory traversal and recreating the corresponding directory structure.

For a teaching example, though, what we have now is enough. It is concise, robust, reasonably fast, and not much code — exactly right for understanding file I/O and a handful of modern C++ features.

## Summary

Across the two articles we built a file copier end to end — from requirements analysis to interface design, from core implementation to test verification. It is only a couple hundred lines of code, but small as it is, it has all the organs: error handling, progress feedback, performance tuning, edge cases — everything that deserved consideration got considered.

More importantly, we put a good number of modern C++ features to work: `std::filesystem` simplifies path handling, `std::chrono` measures time precisely, `std::vector` manages the buffer, RAII releases resources automatically, and exception handling reports errors gracefully. Features like these make C++ feel far less "hardcore" — both readability and safety move up a notch.

Next time a similar file-operation requirement lands on your desk, you will know where to start. Remember: think the requirements through first, design the interface well, pick the right tools, implement step by step, and then test properly. That is where engineering discipline comes from — not chasing flashy techniques, but making every stage solid.
