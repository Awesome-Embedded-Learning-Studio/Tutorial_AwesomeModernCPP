---
chapter: 10
cpp_standard:
- 17
- 20
description: We practice creating threads, wrapping with RAII, managing parameter
  lifetimes, and using `thread_local` statistics by implementing a parallel file scanner.
difficulty: intermediate
order: 0
prerequisites:
- '卷五 ch00: 并发思维与基础'
- '卷五 ch01: 线程生命周期与 RAII'
reading_time_minutes: 23
tags:
- host
- cpp-modern
- atomic
- beginner
title: 'Lab 0: Thread Lifecycle Lab'
translation:
  source: documents/vol5-concurrency/exercises/00-thread-lifecycle.md
  source_hash: d0e02146b033b3d6609c8248b077c8a32afc7dc01966f920ed364488b4ddcae6
  translated_at: '2026-06-16T04:07:19.645611+00:00'
  engine: anthropic
  token_count: 5737
---
# Lab 0: Thread Lifecycle Lab

## Objectives

After reading the four articles in ch01, we now know how to create threads, pass parameters, write lambdas, and use `std::thread`. However, the gap between "knowing" and "having written" is, frankly, larger than many friends imagine. A typical experience is: you read the code for an RAII wrapper and think, "I get this," then you write a multi-threaded program yourself, run it under TSan, and find data races all over the place, or some exception path causes you to forget about a thread.

The goal of this Lab is straightforward: we will write a **parallel file scanner** — the main thread shards files in a directory and distributes them to N worker threads to scan, each worker counts information for the files it is responsible for (size, extension distribution, etc.), and finally the main thread aggregates the statistical results from all workers. The project isn't huge, but it will force you to face four core problems: how to create and manage multiple threads, how to use RAII to ensure exception paths don't leak threads, how to safely pass parameters to threads, and how to use `std::atomic` for thread-safe statistics.

After completing this Lab, you should be able to produce a set of reusable RAII thread wrappers and `thread_local` statistics patterns that you can directly use in subsequent Labs.

## Prerequisites

Before starting, make sure you have finished the following chapters:

- **ch00-01**: Why we need concurrency — concurrency vs. parallelism, Amdahl's Law
- **ch00-02**: Basic concurrency problems — data race, race condition, dead lock
- **ch00-03**: CPU cache and OS threads — cache line, false sharing
- **ch01-01**: `std::thread` basics — creation, join/detach, hardware_concurrency
- **ch01-02**: Thread arguments and lifecycle — decay-copy, dangling references, move-only
- **ch01-03**: Thread ownership and RAII — thread_guard, joining_thread, exception safety
- **ch01-04**: `thread_local` and `call_once` — thread-local storage, one-time initialization

This Lab has no dependencies on previous Labs.

## Environment Setup

We need C++17 (because we use `std::filesystem`), a reasonably modern compiler, and Catch2 v3 to run tests. Specific version requirements are as follows:

- **Compiler**: GCC 12+ or Clang 15+ (requires complete C++17 support), I used GCC 16.1 when designing this.
- **CMake**: 3.14+ (FetchContent requires it)
- **Catch2**: v3.x, header-only mode, pulled via FetchContent

TSan is our primary diagnostic tool in this Lab. After implementing each milestone, you should run the tests under TSan to confirm there are no data races. The compiler option is `-fsanitize=thread`.

Here is a minimal usable `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.14)
project(Lab0_ThreadLifecycle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch Catch2
include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(catch2)

add_executable(lab0_tests main.cpp)

# Link against Catch2
target_link_libraries(lab0_tests PRIVATE Catch2::Catch2WithMain)
```

The skeleton of the test file looks like this:

```cpp
#include <catch2/catch_all.hpp>
#include <thread>
#include <vector>
#include <iostream>

TEST_CASE("Environment check") {
    // Just a sanity check to ensure build system works
    REQUIRE(true);
}
```

Compile and run:

```bash
mkdir build && cd build
cmake ..
make
./lab0_tests
```

If everything is normal, you should see a green test pass output.

## Final Interface

Before writing code, let's clarify the shape of the final product. Don't rush to write the implementation; first, see the goal clearly.

### `FileInfo` — Single file scan result

| Type | Member | Semantics |
|------|--------|-----------|
| `std::string` | `path` | Full file path |
| `std::uint64_t` | `size` | File size (bytes) |
| `std::string` | `extension` | Extension (including dot, e.g., `.txt`) |

### `WorkerStats` — Single worker aggregation (maintained by `thread_local` in Milestone 4, aggregated by main thread)

| Type | Member | Semantics |
|------|--------|-----------|
| `std::size_t` | `file_count` | Number of files scanned |
| `std::uint64_t` | `total_bytes` | Total bytes scanned |
| `std::unordered_map<std::string, std::size_t>` | `extension_counts` | Extension → occurrence count |

### `joining_thread` — RAII thread wrapper (Milestone 2, move-only, non-copyable)

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::thread` | `t` | Underlying managed thread object |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Construct (callable) | `template<typename Callable, typename... Args> joining_thread(Callable&& func, Args&&... args)` | Accepts any callable object and arguments | MS2 |
| Construct (take over thread) | `joining_thread(std::thread&& t) noexcept` | Move construct from `std::thread` | MS2 |
| move constructor/assignment | `joining_thread(joining_thread&&) noexcept; joining_thread& operator=(joining_thread&&) noexcept` | Transfer thread ownership | MS2 |
| Destructor | `~joining_thread()` | Automatically join if `joinable()` | MS2 |
| join | `void join()` | Wait for thread to finish | MS2 |
| joinable | `bool joinable() const noexcept` | Whether holding an active thread | MS2 |

### `FileScanner` — File scanner

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::filesystem::path` | `root_` | Root directory to scan |
| `std::size_t` | `num_workers_` | Number of worker threads |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `FileScanner(const std::filesystem::path& root, std::size_t num_workers)` | Specify scan directory and worker count | MS1 |
| scan | `WorkerStats scan() const` | Start scan and return aggregated result | MS1–4 |

Next, we break it down by milestone and implement step by step.

## Milestone 1: Parallel Task Distribution

### Objective

Use `std::thread` to start a fixed number of workers, each responsible for scanning a portion of files. The main thread waits for all workers to finish and outputs summary information. Don't aim for perfection in this milestone — manual `join`, no RAII, just use global `std::atomic` for simple statistics. Let's just get the multi-threaded skeleton working first.

### Why do this step first

In the overall design, this is the most basic layer: first get "multiple threads working at the same time" running. Later milestones will gradually improve on this foundation — RAII wrapping, parameter safety, `thread_local` statistics, each step introducing only one new engineering problem. If you aim for a perfect architecture from the start, it's easy to fall into the trap of "struggling with interface design before anything runs."

### Implementation Guide

The overall idea is divided into three steps: first use `std::filesystem::recursive_directory_iterator` to collect all file paths in the root directory into a `std::vector<std::string>`; then shard by the number of workers, each worker gets a slice of the file list; finally create N `std::thread`s, each thread iterates over its own list of files, counting file numbers and total size.

For the sharding strategy, simple equal division is fine — assuming 100 files and 4 workers, each worker is responsible for 25 files. The last worker might get a few more (because division isn't always even). The core pseudocode is as follows:

```cpp
// 1. Collect files
std::vector<std::string> all_files;
for (auto& entry : std::filesystem::recursive_directory_iterator(root_)) {
    if (entry.is_regular_file()) {
        all_files.push_back(entry.path().string());
    }
}

// 2. Calculate shard size
std::size_t total = all_files.size();
std::size_t worker_count = std::thread::hardware_concurrency();
std::size_t chunk_size = total / worker_count;

// 3. Launch workers
std::vector<std::thread> workers;
for (std::size_t i = 0; i < worker_count; ++i) {
    std::size_t start = i * chunk_size;
    std::size_t end = (i == worker_count - 1) ? total : (i + 1) * chunk_size;

    workers.emplace_back([start, end, &all_files] {
        for (std::size_t j = start; j < end; ++j) {
            // Scan file all_files[j] and update atomics
        }
    });
}

// 4. Join all
for (auto& w : workers) {
    w.join();
}
```

For result collection, this milestone uses the simplest method — a set of global `std::atomic<std::size_t>` to accumulate file count and total bytes. Each worker increments once after scanning a file. This approach has performance overhead (all workers contending for the same atomic), but it's sufficient for understanding the basic multi-threaded skeleton; later Milestone 4 will replace it with `thread_local`.

**Pitfall Warning**: There are a few places to watch out for. First, `recursive_directory_iterator` itself is not thread-safe — you cannot have multiple threads incrementing the same iterator simultaneously. So the file path collection step must be completed in the main thread; workers are only responsible for processing the already collected path list. Second, parameters passed to `std::thread`'s constructor undergo decay-copy — if you pass a reference to a slice of a `std::vector`, it will be copied. For this milestone, this is perfectly acceptable, but in later milestones we will consider how to avoid unnecessary copies. Third, if your test directory has very few files (e.g., only 3 files but you spawned 8 workers), some workers will get an empty list — your lambda needs to handle this correctly.

### Verification

Below is the Catch2 test code. First create some temporary files, then verify that the scan results are correct.

```cpp
TEST_CASE("Milestone 1: Basic parallel scan") {
    // Create temporary files
    std::string test_dir = "test_files";
    std::filesystem::create_directories(test_dir);
    std::ofstream(test_dir + "/a.txt") << "hello";
    std::ofstream(test_dir + "/b.cpp") << "world";

    FileScanner scanner(test_dir, 2);
    WorkerStats stats = scanner.scan();

    REQUIRE(stats.file_count == 2);
    REQUIRE(stats.total_bytes == 10); // "hello" (5) + "world" (5)

    // Cleanup
    std::filesystem::remove_all(test_dir);
}

TEST_CASE("Milestone 1: Empty directory") {
    std::string test_dir = "empty_dir";
    std::filesystem::create_directories(test_dir);

    FileScanner scanner(test_dir, 2);
    WorkerStats stats = scanner.scan();

    REQUIRE(stats.file_count == 0);
    REQUIRE(stats.total_bytes == 0);

    std::filesystem::remove_all(test_dir);
}
```

These two tests cover basic scenarios: file collection in normal situations and the edge case of an empty directory. Run it with TSan to confirm there are no data races:

```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
make
./lab0_tests
```

## Milestone 2: RAII Wrapper

### Objective

Implement `joining_thread` — an RAII wrapper that automatically `join`s upon destruction. Replace the bare `std::thread` in Milestone 1 with `joining_thread`, then verify that threads are still correctly reclaimed on exception paths.

### Why

The code in Milestone 1 has a very obvious engineering problem: manual `join`. We wrote a `for` loop to join threads one by one, which looks fine — but what if an exception is thrown somewhere before the join loop? Or if one of the `join`s itself throws an exception (rare but allowed by the standard)? The remaining threads become orphaned, calling `std::terminate` on destruction. ch01-03 has already covered the root cause of this problem and the RAII solution; this milestone is about moving it from "understanding" to "implementing and using in practice."

### Implementation Guide

The core idea of `joining_thread` is to take ownership of `std::thread` and automatically call `join()` in the destructor. ch01-03 has already provided the complete implementation code, so we won't repeat it here — but there are a few key design points you need to think through clearly yourself:

First, in the move assignment operator, you must handle the currently held thread before receiving the new one. If the current thread is still `joinable`, you must join it first, otherwise it's UB. This pattern of "clean up the old before taking over the new" is the same logic as the assignment operator of `std::unique_ptr`.

Second, `join()` in the destructor can throw exceptions (e.g., resource deadlock). Throwing in a destructor triggers `std::terminate`. A pragmatic approach is to wrap it with `try-catch`, swallow the exception, and log it. Don't skip this step thinking "join can't fail" — the difference between industrial-grade code often lies in these seemingly redundant defenses.

Third, the constructor needs to support move construction from `std::thread`, move construction from another `joining_thread`, and directly accepting callable objects and arguments. The first two are move semantics, the third is a template constructor requiring `std::forward` for perfect forwarding.

Retrofitting Milestone 1 code with `joining_thread` is very simple — replace `std::thread` with `joining_thread`, delete the manual join loop, and you're done. When the `workers` vector is destroyed, each `joining_thread`'s destructor is automatically called.

### Verification

```cpp
TEST_CASE("Milestone 2: RAII thread wrapper") {
    SECTION("Auto-join on destruction") {
        bool executed = false;
        {
            joining_thread t([&executed] { executed = true; });
        } // t goes out of scope here
        REQUIRE(executed == true);
    }

    SECTION("Exception safety") {
        bool executed = false;
        try {
            joining_thread t([&executed] {
                executed = true;
            });
            throw std::runtime_error("test");
        } catch (...) {
            // Thread t should have been joined despite the exception
        }
        REQUIRE(executed == true);
    }

    SECTION("Move semantics") {
        joining_thread t1([] { /* do nothing */ });
        joining_thread t2 = std::move(t1);
        REQUIRE(t1.joinable() == false);
        REQUIRE(t2.joinable() == true);
    }

    SECTION("Use in container") {
        std::vector<joining_thread> workers;
        workers.emplace_back([] { /* worker 1 */ });
        workers.emplace_back([] { /* worker 2 */ });
        // Auto-join when workers is destroyed
    }
}
```

This set of tests covers four key scenarios: normal destruction auto-join, auto-join on exception paths, move semantics transferring ownership, and using `joining_thread` in `std::vector`. Pay special attention to the second test — it simulates a scenario where an exception is thrown after creating threads but before manually joining. Without RAII, this situation would directly lead to `std::terminate`.

## Milestone 3: Fixing Parameter Lifetimes

### Objective

Review the parameter passing method in Milestone 1, identify and fix all potential dangling references and lifetime issues. Specifically, we want to change reference captures in lambdas to safe value captures or moves, ensuring threads don't access destroyed variables.

### Why

ch01-02 covered the decay-copy semantics of `std::thread` and the risks of dangling references, but in small examples these problems often don't appear — because variable lifetimes in small examples happen to be long enough. In a real parallel file scanner, the situation is more complex: the main thread might start cleaning up temporary data before workers finish, or a lambda captures a reference to a local `std::vector`. These bugs might not trigger during development but will appear in unpredictable ways under high concurrency pressure in production.

### Implementation Guide

In Milestone 1's code, we passed the file path list by value to `std::thread` — this is actually safe because `std::thread`'s constructor performs decay-copy on parameters, so each worker gets an independent copy of the path list. But problems often hide in more subtle places. Consider the following scenarios that are easy to mess up.

**First**: lambda captures a reference to a local variable. Suppose you changed the worker lambda to this:

```cpp
std::vector<std::string> local_files = /* ... */;
workers.emplace_back([&local_files] {
    // Access local_files by reference
});
```

If `local_files` is destroyed or modified while the worker is still executing, this is a dangling reference. In our code, `local_files`'s lifetime is long enough (on the stack in `scan()`), but this style makes correctness depend on the caller's implicit understanding of lifetimes — not a good habit.

**Second**: passing parameters via `std::ref`. If you think copying the entire `std::vector` is wasteful and want to use a reference to avoid copying:

```cpp
workers.emplace_back(std::ref(local_files));
```

This passes a reference to `local_files` to the thread. If `local_files` is a local variable declared inside the loop body, and it gets modified in the next iteration, the previous worker will read the modified data — this is a data race. The fix is to use value capture (let decay-copy give each worker an independent copy) or use `std::move` to transfer ownership to the thread.

**Third**: implicit capture of `this` pointer. If you make `FileScanner` a class and use member variables in the lambda, then `[=]` capture implicitly depends on the lifetime of the `FileScanner` object — if the `FileScanner` object is destroyed before the worker finishes, the `this` pointer dangles. This bug is particularly easy to hit in Lab 3 (Thread Pool), because the thread pool's lifetime is often longer than the caller expects.

The core task of this milestone is: review your code from Milestone 1 and 2, find all reference captures and uses of `std::ref`, and judge if they are safe. For unsafe captures, change to value captures or `std::move`. The way to verify is TSan — a correct implementation should not report any data races under TSan.

### Verification

```cpp
TEST_CASE("Milestone 3: Parameter lifetime safety") {
    SECTION("Value capture is safe") {
        std::string data = "test";
        std::vector<std::string> files = { "a.txt", "b.cpp" };

        joining_thread t([files, data] {
            // Safe: files and data are copied
            REQUIRE(files.size() == 2);
        });
    }

    SECTION("Reference capture is unsafe (detected by TSan)") {
        std::vector<std::string> files = { "a.txt" };
        {
            joining_thread t([&files] {
                // Unsafe: files might be destroyed
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                // Accessing files here is UB if destroyed
            });
        } // t joins here, but if we detached instead...
        // If files went out of scope before thread finished, UB occurs
    }
}
```

Run TSan to confirm:

```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" ..
make
./lab0_tests
```

If everything is normal, TSan should not output any data race reports.

## Milestone 4: thread_local Statistics and Aggregation

### Objective

Replace the global `std::atomic` statistics method from Milestone 1 with `thread_local` statistics. Each worker maintains its own `WorkerStats` object, and after scanning, the results are aggregated in the main thread.

### Why

Milestone 1 used a global `std::atomic` to accumulate statistics — this approach has two problems. First, all workers contend for the same atomic variable, causing unnecessary cache line invalidations (a close relative of false sharing). Second, it can only count simple counts; once you want to统计 distribution data like "how many times each extension appeared," global atomics aren't enough — you can't protect a `std::map` with one atomic (unless you add a lock, but that brings us back to ch02 territory).

`thread_local` provides a cleaner solution: each worker thread has its own `WorkerStats` instance, calculating separately, completely contention-free. After calculation, the main thread collects all worker results for aggregation. This pattern is not only the core design of this Lab but also the foundation for subsequent Labs — Lab 2's atomic metrics and Lab 3's thread pool will use similar "thread-local statistics → aggregation" structures.

### Implementation Guide

The core idea is: declare a `thread_local WorkerStats` inside the worker function, each worker accumulates data into its own `WorkerStats` during scanning, and after scanning, passes the `WorkerStats` back to the main thread somehow.

There are several choices for how to return statistical results. The simplest is to let the worker function return `WorkerStats`, then the main thread collects it via `std::future`. But `std::future` is ch05 content, and we shouldn't introduce it prematurely in this Lab. So a better approach is to give each worker a pointer to an output area — the main thread pre-allocates a `std::vector<WorkerStats>`, and each worker writes to its own position via index.

```cpp
void worker(const std::vector<std::string>& files,
            std::size_t start,
            std::size_t end,
            WorkerStats* output)
{
    thread_local WorkerStats local_stats;

    for (std::size_t i = start; i < end; ++i) {
        // Scan file and update local_stats
    }

    *output = local_stats; // Copy result to output slot
}
```

There is a subtle point worth noting: `thread_local` variables will **reuse** the same instance across multiple calls to the same worker function. In our scenario, each worker is called only once, so this isn't an issue. But if you accidentally let the same thread enter the worker function multiple times, you need to manually reset `local_stats` at the beginning of the function.

The aggregation logic is simple — iterate over `std::vector<WorkerStats>`, summing all `WorkerStats`:

```cpp
WorkerStats total;
for (const auto& stats : worker_results) {
    total.file_count += stats.file_count;
    total.total_bytes += stats.total_bytes;
    for (const auto& [ext, count] : stats.extension_counts) {
        total.extension_counts[ext] += count;
    }
}
```

**Pitfall Warning**: In the line `workers.emplace_back(...)`, the `output` pointer must be unique to each worker, with no duplicates. If you use a reference to the loop variable `i` to pass `&results[i]`, and the lambda captures `i` by reference — congratulations, the problem you just fixed in Milestone 3 is back. Use value capture `[i]` to avoid this.

Another thing to watch is the copy overhead of `WorkerStats`. If there are many extension types, copying the `std::unordered_map` in `WorkerStats` might not be cheap. For the scale of this Lab, it's not a problem at all, but if you are writing production code, consider `std::move` to avoid unnecessary copies.

### Verification

```cpp
TEST_CASE("Milestone 4: thread_local aggregation") {
    std::string test_dir = "test_milestone4";
    std::filesystem::create_directories(test_dir);
    std::ofstream(test_dir + "/1.txt") << "a";
    std::ofstream(test_dir + "/2.txt") << "b";
    std::ofstream(test_dir + "/3.cpp") << "c";

    FileScanner scanner(test_dir, 2);
    WorkerStats stats = scanner.scan();

    REQUIRE(stats.file_count == 3);
    REQUIRE(stats.extension_counts[".txt"] == 2);
    REQUIRE(stats.extension_counts[".cpp"] == 1);

    std::filesystem::remove_all(test_dir);
}
```

Run all tests with TSan to confirm there are no data races from Milestone 1 to 4:

```bash
./lab0_tests
```

## Self-Check List

Before submitting, confirm the following items one by one:

- [ ] Milestone 1 tests all pass — parallel scanning doesn't miss files
- [ ] Milestone 2 tests all pass — `joining_thread` auto-joins on both normal and exception paths
- [ ] Milestone 3 tests all pass — no dangling references, move-only parameters passed correctly
- [ ] Milestone 4 tests all pass — `thread_local` statistics match single-threaded results
- [ ] All tests run under TSan with no data race reports
- [ ] No situation where a `std::thread` with `joinable() == true` is destroyed
- [ ] No use of `detach` to escape lifetime management
- [ ] Can verbally explain the necessity of `try-catch` in the `joining_thread` destructor
- [ ] Can explain the difference between lambda capture `[&]`, `[=]`, and `[this]` in multi-threaded scenarios
- [ ] Can explain the two advantages of the `thread_local` statistics pattern over global atomics (contention-free + supports complex structures)
