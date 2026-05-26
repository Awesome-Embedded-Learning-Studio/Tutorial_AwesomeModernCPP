---
title: 'Lab 0: Thread Lifecycle Lab'
description: Build practical skills in thread creation, RAII (Resource Acquisition
  Is Initialization) wrappers, parameter lifetimes, and `thread_local` statistics
  through a parallel file scanner.
chapter: 10
order: 0
difficulty: intermediate
tags:
- host
- cpp-modern
- atomic
- beginner
cpp_standard:
- 17
- 20
reading_time_minutes: 45
prerequisites:
- '卷五 ch00: 并发思维与基础'
- '卷五 ch01: 线程生命周期与 RAII'
translation:
  source: documents/vol5-concurrency/exercises/00-thread-lifecycle.md
  source_hash: f23eb737442de2a38066d1df35a38e169fc1e094005d858fc082e02607f3aaac
  translated_at: '2026-05-26T11:46:56.934720+00:00'
  engine: anthropic
  token_count: 5741
---
# Lab 0: Thread Lifecycle Lab

## Objectives

After reading the four articles in ch01, we now know `std::thread` how to create threads, how to pass parameters, `JoiningThread` how to write them, and `thread_local` how to use them. But the gap between "knowing" and "having written" is, frankly, larger than many people imagine. A typical experience goes like this: you read some RAII wrapper code and think "I get it," then you write a multithreaded program yourself, run it under TSan, and find data races everywhere, or discover that some exception path leaves threads dangling.

The goal of this Lab is straightforward: we are going to build a **parallel file scanner** — the main thread shards the files in a directory and dispatches them to N worker threads for scanning. Each worker collects stats for its assigned files (size, extension distribution, etc.), and finally, the main thread aggregates the results from all workers. The project isn't large, but it will force you to confront four core problems: how to create and manage multiple threads, how to use RAII to ensure no thread leaks on exception paths, how to safely pass parameters to threads, and how to use `thread_local` for thread-safe statistics.

After completing this Lab, you should have a reusable `JoiningThread` wrapper and a `thread_local` statistics pattern that you can directly drop into subsequent Labs.

## Prerequisites

Before starting, make sure you have read the following chapters:

- **ch00-01**: Why we need concurrency — concurrency vs. parallelism, Amdahl's law
- **ch00-02**: Fundamental concurrency problems — data race, race condition, dead lock
- **ch00-03**: CPU cache and OS threads — cache line, false sharing
- **ch01-01**: std::thread basics — creation, join/detach, hardware_concurrency
- **ch01-02**: Thread parameters and lifecycle — decay-copy, dangling references, move-only
- **ch01-03**: Thread ownership and RAII — thread_guard, joining_thread, exception safety
- **ch01-04**: thread_local and call_once — thread-local storage, one-time initialization

This Lab has no prerequisite Lab dependencies.

## Environment Setup

We need C++17 (because we will use `<filesystem>`), a reasonably modern compiler, and Catch2 v3 to run tests. The specific version requirements are as follows:

- **Compiler**: GCC 12+ or Clang 15+ (requires full `<filesystem>` support); the author used GCC 16.1 when designing this, if
- **CMake**: 3.14+ (required by FetchContent)
- **Catch2**: v3.x, header-only mode, fetched via FetchContent

TSan is our primary diagnostic tool in this Lab. After implementing each milestone, you should run the tests under TSan to confirm there are no data races. The compiler flag is `-fsanitize=thread -g`.

Here is a minimal working CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.14)
project(lab0_thread_lifecycle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Catch2 v3
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

# 你的源文件
add_executable(lab0_tests
    tests/main.cpp
)
target_link_libraries(lab0_tests PRIVATE Catch2::Catch2WithMain)

# TSan 配置（Debug 模式下自动启用）
target_compile_options(lab0_tests PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=thread -g>
)
target_link_options(lab0_tests PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=thread>
)
```

The test file skeleton looks like this:

```cpp
// tests/main.cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Lab 0 sanity check", "[lab0]")
{
    REQUIRE(1 + 1 == 2);
}
```

Build and run:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/lab0_tests
```

If everything is working, you should see a green test-passing output.

## Final Interfaces

Before writing any code, let's clarify the shape of our final deliverables. Don't rush into the implementation — take a moment to understand the target.

### `FileInfo` — Single File Scan Result

| Type | Member | Semantics |
|------|--------|-----------|
| `std::filesystem::path` | `path` | Full file path |
| `std::uintmax_t` | `file_size` | File size (in bytes) |
| `std::string` | `extension` | Extension (including the dot, e.g., `.cpp`) |

### `WorkerStats` — Single Worker Statistics Aggregate (maintained with `thread_local` in Milestone 4, aggregated by the main thread)

| Type | Member | Semantics |
|------|--------|-----------|
| `std::size_t` | `files_scanned` | Number of files scanned |
| `std::uintmax_t` | `total_bytes` | Total bytes scanned |
| `std::unordered_map<std::string, std::size_t>` | `ext_counts` | Extension → occurrence count |

### `JoiningThread` — RAII Thread Wrapper (Milestone 2, move-only, non-copyable)

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::thread` | `thread_` | The managed underlying thread object |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor (callable) | `JoiningThread(Callable&&, Args&&...)` | Accepts any callable object and arguments | MS2 |
| Constructor (take over thread) | `JoiningThread(std::thread) noexcept` | Move-constructs from a `std::thread` | MS2 |
| Move construct/assign | `JoiningThread(JoiningThread&&)` | Transfers thread ownership | MS2 |
| Destructor | `~JoiningThread()` | Automatically joins if `joinable()` | MS2 |
| join | `void join()` | Waits for the thread to finish | MS2 |
| joinable | `bool joinable() const noexcept` | Whether it holds an active thread | MS2 |

### `FileScanner` — File Scanner

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::filesystem::path` | `root_path_` | Root directory to scan |
| `std::size_t` | `num_workers_` | Number of worker threads |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `FileScanner(path, size_t num_workers)` | Specifies the scan directory and worker count | MS1 |
| scan | `WorkerStats scan()` | Starts the scan and returns the aggregated result | MS1–4 |

Next, we will break this down by milestone and implement it step by step.

## Milestone 1: Parallel Task Dispatch

### Objective

Use `std::thread` to launch a fixed number of workers, where each worker is responsible for scanning a subset of files. The main thread waits for all workers to finish, then prints the aggregated information. Don't pursue perfection in this milestone — manual `join()`, no RAII, and a simple global `std::atomic` for statistics will do. We just need to get the multithreading skeleton up and running first.

### Why this step first

In the overall design, this is the most fundamental layer: getting "multiple threads working simultaneously" to actually run. Subsequent milestones will incrementally improve upon this foundation — RAII wrapping, parameter safety, thread_local statistics — with each step introducing only one new engineering problem. If you chase a perfect architecture from the start, it's easy to fall into the trap of "agonizing over interface design before anything even runs."

### Implementation Guide

The overall approach has three steps: first, use `std::filesystem::recursive_directory_iterator` to collect all file paths under the root directory into a `std::vector`; then, shard them by the number of workers so that each worker gets a slice of the file list; finally, create N `std::thread` objects, where each thread iterates over its own file list and counts the files and total size.

For the sharding strategy, simple equal division is fine — assuming 100 files and 4 workers, each worker handles 25 files. The last worker might get a few extra (since division might not be exact). The core pseudocode looks like this:

```text
// 1. 收集所有文件路径
all_files = []
for (entry in recursive_directory_iterator(root)):
    if (entry.is_regular_file()):
        all_files.push(entry.path())

// 2. 分片
chunk_size = all_files.size() / num_workers
for i in [0, num_workers):
    start = i * chunk_size
    end = (i == num_workers - 1) ? all_files.size() : start + chunk_size
    worker_files = all_files[start..end]  // 这是一个切片视图

// 3. 启动 worker
for i in [0, num_workers):
    threads[i] = thread(worker_function, worker_files[i])
    // 注意：这里直接把分片的 vector 传给线程

// 4. 等待完成
for t in threads:
    t.join()
```

For collecting the statistics, this milestone uses the simplest approach — a set of global `std::atomic<std::size_t>` variables to accumulate the file count and total bytes. Each worker increments the atomics once per file scanned. This approach has a performance cost (all workers contend on the same atomics), but it's sufficient for understanding the basic multithreading skeleton. Milestone 4 will replace this with `thread_local`.

There are a few pitfalls to watch out for. First, `std::filesystem::recursive_directory_iterator` itself is not thread-safe — you cannot increment the same iterator from multiple threads simultaneously. Therefore, the file path collection step must be completed in the main thread; workers are only responsible for processing the already-collected path list. Second, parameters passed to `std::thread` are decay-copied — if you pass a reference to a slice of a `std::vector<std::filesystem::path>`, it will be copied. This is perfectly acceptable for this milestone, but in later milestones we will consider how to avoid unnecessary copies. Third, if your test directory has very few files (e.g., only three files but you spawned eight workers), some workers will receive an empty list — your `worker_function` needs to handle this case correctly.

### Verification

Here is the Catch2 test code. We create some temporary files, then verify that the scan results are correct.

```cpp
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>

// 测试辅助：在临时目录下创建 N 个文件
std::filesystem::path create_test_files(
    const std::filesystem::path& dir, int count,
    const std::string& ext = ".txt")
{
    std::filesystem::create_directories(dir);
    for (int i = 0; i < count; ++i) {
        std::ofstream(dir / (std::string("file_") + std::to_string(i) + ext))
            << std::string(100, 'x');  // 每个 100 字节
    }
    return dir;
}

TEST_CASE("Milestone 1: parallel scan collects all files",
          "[lab0][milestone1]")
{
    namespace fs = std::filesystem;
    fs::path test_dir = fs::temp_directory_path() / "lab0_test_ms1";

    // 清理可能残留的旧测试数据
    fs::remove_all(test_dir);
    const int kFileCount = 20;
    create_test_files(test_dir, kFileCount);

    // 收集所有文件路径
    std::vector<fs::path> all_files;
    for (const auto& entry :
         fs::recursive_directory_iterator(test_dir)) {
        if (entry.is_regular_file()) {
            all_files.push_back(entry.path());
        }
    }

    // 分片并启动 4 个 worker
    const std::size_t kWorkers = 4;
    std::atomic<std::size_t> total_scanned{0};

    auto worker = [&](std::vector<fs::path> files) {
        for (const auto& f : files) {
            // 简单统计：计数
            total_scanned.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    std::size_t chunk = all_files.size() / kWorkers;
    for (std::size_t i = 0; i < kWorkers; ++i) {
        auto start = all_files.begin() + i * chunk;
        auto end = (i == kWorkers - 1)
                       ? all_files.end()
                       : start + chunk;
        threads.emplace_back(worker,
                             std::vector<fs::path>(start, end));
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(total_scanned.load() == kFileCount);

    // 清理
    fs::remove_all(test_dir);
}

TEST_CASE("Milestone 1: handles empty directory",
          "[lab0][milestone1]")
{
    namespace fs = std::filesystem;
    fs::path empty_dir = fs::temp_directory_path() / "lab0_test_empty";
    fs::remove_all(empty_dir);
    fs::create_directories(empty_dir);

    std::vector<fs::path> all_files;
    for (const auto& entry :
         fs::recursive_directory_iterator(empty_dir)) {
        if (entry.is_regular_file()) {
            all_files.push_back(entry.path());
        }
    }

    REQUIRE(all_files.empty());

    // 即使文件列表为空，worker 也不应该崩溃
    std::atomic<std::size_t> total{0};
    auto worker = [&](std::vector<fs::path> files) {
        for (const auto& f : files) {
            total.fetch_add(1);
        }
    };

    std::thread t(worker, std::vector<fs::path>{});
    t.join();

    REQUIRE(total.load() == 0);
    fs::remove_all(empty_dir);
}
```

These two tests cover the basic scenarios: file collection under normal conditions and the edge case of an empty directory. Run it under TSan to confirm there are no data races:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/lab0_tests "[lab0][milestone1]"
```

## Milestone 2: RAII Wrapping

### Objective

Implement `JoiningThread` — an RAII wrapper that automatically `join()` on destruction. Replace the bare `std::thread` in Milestone 1 with `JoiningThread`, then verify that threads are still correctly reclaimed on exception paths.

### Why

The Milestone 1 code has an obvious engineering problem: manual `join()`. We wrote a `for` loop to join threads one by one, which looks fine — but what if an exception is thrown somewhere before the join loop? Or what if one of the `join()` calls itself throws an exception (rare, but permitted by the standard)? The remaining threads become orphaned, and their destructors call `std::terminate()`. ch01-03 already covered the root cause of this problem and the RAII solution; this milestone is about moving it from "understanding" to "implementing and using in practice."

### Implementation Guide

The core idea of `JoiningThread` is to take ownership of a `std::thread` and automatically call `join()` in its destructor. ch01-03 already provided the complete implementation code, so we won't repeat it here — but there are a few key design points you need to think through yourself:

First, in the move assignment operator, you must handle the currently held thread before taking on the new one. If the current thread is still `joinable()`, you must join it first, otherwise it's UB. This "clean up the old before taking on the new" pattern follows the same logic as the assignment operator of `std::unique_ptr`.

Second, `join()` in the destructor can throw an exception (`std::system_error`). Throwing an exception in a destructor triggers `std::terminate()`. The pragmatic approach is to wrap it in a `try-catch`, swallow the exception, and log it. Don't skip this step thinking "join can't possibly fail" — the difference in production-grade code often lies in these seemingly redundant defenses.

Third, the constructor needs to support move-constructing from a `std::thread`, move-constructing from another `JoiningThread`, and directly accepting a callable object with arguments. The first two involve move semantics, and the third is a templated constructor that requires `std::forward` for perfect forwarding.

Refactoring the Milestone 1 code with `JoiningThread` is very simple — replace `std::vector<std::thread>` with `std::vector<JoiningThread>`, and delete the manual join loop. That's it. When `vector` is destroyed, the destructor of each `JoiningThread` is automatically invoked.

### Verification

```cpp
TEST_CASE("Milestone 2: JoiningThread auto-joins on destruction",
          "[lab0][milestone2]")
{
    std::atomic<bool> thread_ran{false};

    {
        // 在作用域内创建 JoiningThread
        JoiningThread t([&]() {
            thread_ran.store(true, std::memory_order_relaxed);
        });
        // 离开作用域时，t 的析构函数应该自动 join
    }

    // 如果析构函数正确 join 了，thread_ran 一定是 true
    REQUIRE(thread_ran.load());
}

TEST_CASE("Milestone 2: JoiningThread handles exception path",
          "[lab0][milestone2]")
{
    std::atomic<int> counter{0};

    auto make_scanner = [&]() {
        // 用 JoiningThread 管理 worker
        std::vector<JoiningThread> workers;
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        // 模拟一个异常
        throw std::runtime_error("simulated failure");
        // workers 在这里析构，应该自动 join
    };

    REQUIRE_THROWS_AS(make_scanner(), std::runtime_error);
    // 即使抛了异常，所有 worker 都应该已经完成
    REQUIRE(counter.load() == 4);
}

TEST_CASE("Milestone 2: move semantics transfer ownership",
          "[lab0][milestone2]")
{
    std::atomic<bool> ran{false};

    JoiningThread t1([&]() { ran.store(true); });
    REQUIRE(t1.joinable());

    JoiningThread t2 = std::move(t1);
    REQUIRE(!t1.joinable());
    REQUIRE(t2.joinable());

    // t2 析构时 join
}

TEST_CASE("Milestone 2: vector of JoiningThread",
          "[lab0][milestone2]")
{
    std::atomic<int> counter{0};
    {
        std::vector<JoiningThread> workers;
        for (int i = 0; i < 8; ++i) {
            workers.emplace_back([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        // 离开作用域，vector 析构 → 所有 JoiningThread 析构 → 自动 join
    }
    REQUIRE(counter.load() == 8);
}
```

This set of tests covers four key scenarios: automatic join on normal destruction, automatic join on exception paths, move semantics transferring ownership, and using `JoiningThread` in a `vector`. Pay special attention to the second test — it simulates a scenario where an exception is thrown after thread creation but before a manual join. Without RAII, this situation would directly lead to `std::terminate()`.

## Milestone 3: Parameter Lifetime Fixes

### Objective

Review the parameter passing approach in Milestone 1, and identify and fix all potential dangling references and lifetime issues. Specifically, we need to change reference captures in lambdas to safe value captures or moves, ensuring that threads do not access destroyed variables.

### Why

ch01-02 covered the decay-copy semantics of `std::thread` and the risks of dangling references, but in small examples these problems often don't surface — because the variable lifetimes in small examples happen to be long enough. In a real parallel file scanner, the situation is more complex: the main thread might start cleaning up temporary data before the workers have finished, or a lambda might capture a reference to a local `vector`. Bugs of this kind might not trigger during development, but will manifest in unpredictable ways under the high-concurrency pressure of a production environment.

### Implementation Guide

In the Milestone 1 code, we passed the file path list by value to `worker` — this is actually safe, because the constructor of `std::thread` decay-copies the parameters, so the worker gets an independent copy of the path list. But the problems often hide in more subtle places. Consider the following error-prone scenarios.

The first: the lambda captures a reference to a local variable. Suppose you changed `worker` to this:

```cpp
auto worker = [&all_files, start_idx, end_idx]() {
    for (size_t i = start_idx; i < end_idx; ++i) {
        process(all_files[i]);  // 引用捕获，有风险
    }
};
```

If `all_files` is destroyed or modified while the worker is still executing, this is a dangling reference. In our code, the lifetime of `all_files` is long enough (it's on the stack of `main`), but this coding style makes correctness depend on the caller's implicit understanding of lifetimes — which is a bad habit.

The second: passing parameters via `std::ref`. If you think copying the entire `vector` is too wasteful and want to use a reference to avoid the copy:

```cpp
threads.emplace_back(worker, std::ref(chunk_files));
```

This passes a reference to `chunk_files` to the thread. If `chunk_files` is a local variable declared inside the loop body, and it gets modified during the next loop iteration, the previous worker will read the modified data — this is a data race. The fix is to use value capture (letting decay-copy give each worker an independent copy) or use `std::move` to transfer ownership to the thread.

The third: implicit capture of a `this` pointer. If you turn `FileScanner` into a class and the lambda uses member variables, then the `[this]` capture implicitly depends on the lifetime of the `FileScanner` object — if the `FileScanner` object is destroyed before the worker finishes, `this` dangles. This bug is particularly easy to trigger in Lab 3 (thread pools), because the thread pool's lifetime is often longer than the caller expects.

The core task of this milestone is: audit your Milestone 1 and 2 code, find all reference captures and uses of `std::ref`, and determine whether they are safe. For unsafe captures, change them to value captures or `std::move`. The verification method is TSan — a correct implementation should not produce any data race reports under TSan.

### Verification

```cpp
TEST_CASE("Milestone 3: no dangling reference in value capture",
          "[lab0][milestone3]")
{
    namespace fs = std::filesystem;
    fs::path test_dir = fs::temp_directory_path() / "lab0_test_ms3";
    fs::remove_all(test_dir);
    create_test_files(test_dir, 10);

    // 收集文件路径
    std::vector<fs::path> all_files;
    for (const auto& entry :
         fs::recursive_directory_iterator(test_dir)) {
        if (entry.is_regular_file()) {
            all_files.push_back(entry.path());
        }
    }

    std::atomic<std::size_t> total{0};

    // 关键：用值捕获，确保每个 worker 拿到独立副本
    {
        std::vector<JoiningThread> workers;
        const std::size_t kWorkers = 4;
        std::size_t chunk = all_files.size() / kWorkers;

        for (std::size_t i = 0; i < kWorkers; ++i) {
            auto start = all_files.begin() + i * chunk;
            auto end = (i == kWorkers - 1)
                           ? all_files.end()
                           : start + chunk;

            // 每个 worker 拿到自己的文件列表副本
            std::vector<fs::path> worker_files(start, end);

            workers.emplace_back(
                [&total, files = std::move(worker_files)]() {
                    for (const auto& f : files) {
                        total.fetch_add(1,
                            std::memory_order_relaxed);
                    }
                });
        }
        // workers 析构 → 自动 join
    }

    REQUIRE(total.load() == 10);
    fs::remove_all(test_dir);
}

TEST_CASE("Milestone 3: move-only parameter passing",
          "[lab0][milestone3]")
{
    // 验证 move-only 类型（如 unique_ptr）可以安全地传入线程
    std::atomic<bool> processed{false};

    auto ptr = std::make_unique<int>(42);
    JoiningThread t([&processed, p = std::move(ptr)]() {
        // p 在线程内部持有，生命周期安全
        if (p && *p == 42) {
            processed.store(true);
        }
    });
    // t 析构 → join
    REQUIRE(processed.load());
}
```

Run it under TSan to confirm:

```bash
./build/lab0_tests "[lab0][milestone3]" --tsan
```

If everything is normal, TSan should not output any data race reports.

## Milestone 4: thread_local Statistics and Aggregation

### Objective

Replace the global `std::atomic` statistics approach from Milestone 1 with `thread_local` statistics. Each worker maintains its own `WorkerStats` object, and after scanning, the results are aggregated in the main thread.

### Why

Milestone 1 used a global `std::atomic<std::size_t>` to accumulate statistics — this approach has two problems. First, all workers contend on the same atomic variable, causing unnecessary cache line invalidations (a close relative of false sharing). Second, it can only count simple values; once you want to track distribution data like "how many times each extension appeared," a global atomic is no longer sufficient — you can't use a single atomic to protect a `unordered_map` (unless you add a mutex, but that falls under the scope of ch02).

`thread_local` offers a cleaner solution: each worker thread has its own `WorkerStats` instance, calculates independently, and operates completely contention-free. After the calculation, the main thread collects the results from all workers and aggregates them. This pattern is not only the core design of this Lab, but also the foundation for subsequent Labs — Lab 2's atomic metrics and Lab 3's thread pool will both use a similar "thread-local statistics → aggregation" structure.

### Implementation Guide

The core idea is: declare a `thread_local WorkerStats stats;` inside `worker_function`, where each worker accumulates data into its own `stats` during scanning, and after scanning, returns the `stats` to the main thread by some means.

There are several options for returning the statistics. The simplest is to have `worker_function` return `WorkerStats`, and then the main thread collects them via `std::future`. But `std::future` is ch05 material, and we shouldn't introduce it prematurely in this Lab. A more appropriate approach is to give each worker a pointer to an output area — the main thread pre-allocates a `std::vector<WorkerStats>`, and each worker writes to its own position by index.

```cpp
// 主线程预分配
std::vector<WorkerStats> results(num_workers);

// worker 函数
auto worker = [&results, worker_id](std::vector<fs::path> files) {
    thread_local WorkerStats local_stats;

    for (const auto& f : files) {
        local_stats.files_scanned++;
        local_stats.total_bytes += fs::file_size(f);
        local_stats.ext_counts[f.extension().string()]++;
    }

    // 把本地统计写入自己的位置
    results[worker_id] = local_stats;
};
```

There is a subtle point worth noting: `thread_local WorkerStats local_stats` will **reuse** the same instance across multiple calls to the same worker function. In our scenario, each worker is called only once, so this isn't an issue. But if you accidentally let the same thread enter the worker function multiple times, you would need to manually reset `local_stats` at the beginning of the function.

The aggregation logic is straightforward — iterate over `results` and sum up all the `WorkerStats` values:

```cpp
WorkerStats final;
for (const auto& s : results) {
    final.files_scanned += s.files_scanned;
    final.total_bytes += s.total_bytes;
    for (const auto& [ext, count] : s.ext_counts) {
        final.ext_counts[ext] += count;
    }
}
```

Pitfall warning: in the line `results[worker_id] = local_stats`, `worker_id` must be unique to each worker, with no duplicates. If you use a reference to the loop variable `i` to pass `worker_id`, and the lambda captures the reference to `i` — congratulations, the problem you just fixed in Milestone 3 is back. Use value capture of `[&results, worker_id = i]` to avoid this issue.

Another thing to watch out for is the copy cost of `WorkerStats`. If there are many distinct extension types, copying this `unordered_map` `ext_counts` might not be cheap. For the scale of this Lab, it's a complete non-issue, but if you were writing production code, you could consider `std::move(results[worker_id])` to avoid unnecessary copies.

### Verification

```cpp
TEST_CASE("Milestone 4: thread_local stats match single-threaded result",
          "[lab0][milestone4]")
{
    namespace fs = std::filesystem;
    fs::path test_dir =
        fs::temp_directory_path() / "lab0_test_ms4";
    fs::remove_all(test_dir);

    // 创建多种类型的文件
    create_test_files(test_dir, 10, ".cpp");
    create_test_files(test_dir, 5, ".h");
    create_test_files(test_dir, 3, ".txt");

    // 先用单线程统计"正确答案"
    WorkerStats expected;
    for (const auto& entry :
         fs::recursive_directory_iterator(test_dir)) {
        if (entry.is_regular_file()) {
            expected.files_scanned++;
            expected.total_bytes += entry.file_size();
            expected.ext_counts[entry.path().extension().string()]++;
        }
    }

    // 多线程扫描
    std::vector<fs::path> all_files;
    for (const auto& entry :
         fs::recursive_directory_iterator(test_dir)) {
        if (entry.is_regular_file()) {
            all_files.push_back(entry.path());
        }
    }

    const std::size_t kWorkers = 4;
    std::vector<WorkerStats> results(kWorkers);

    {
        std::vector<JoiningThread> workers;
        std::size_t chunk = all_files.size() / kWorkers;

        for (std::size_t i = 0; i < kWorkers; ++i) {
            auto start = all_files.begin() + i * chunk;
            auto end = (i == kWorkers - 1)
                           ? all_files.end()
                           : start + chunk;

            workers.emplace_back(
                [&results, worker_id = i,
                 files = std::vector<fs::path>(start, end)]() {
                    WorkerStats local_stats;

                    for (const auto& f : files) {
                        local_stats.files_scanned++;
                        local_stats.total_bytes +=
                            fs::file_size(f);
                        local_stats
                            .ext_counts[f.extension().string()]++;
                    }

                    results[worker_id] = local_stats;
                });
        }
    }

    // 汇总
    WorkerStats actual;
    for (const auto& s : results) {
        actual.files_scanned += s.files_scanned;
        actual.total_bytes += s.total_bytes;
        for (const auto& [ext, count] : s.ext_counts) {
            actual.ext_counts[ext] += count;
        }
    }

    REQUIRE(actual.files_scanned == expected.files_scanned);
    REQUIRE(actual.total_bytes == expected.total_bytes);
    REQUIRE(actual.ext_counts[".cpp"] == 10);
    REQUIRE(actual.ext_counts[".h"] == 5);
    REQUIRE(actual.ext_counts[".txt"] == 3);

    fs::remove_all(test_dir);
}

TEST_CASE("Milestone 4: thread_local avoids data race on stats",
          "[lab0][milestone4]")
{
    // 压力测试：大量 worker 并发统计，不应出现 data race
    namespace fs = std::filesystem;
    fs::path test_dir =
        fs::temp_directory_path() / "lab0_test_ms4_stress";
    fs::remove_all(test_dir);
    create_test_files(test_dir, 100);

    std::vector<fs::path> all_files;
    for (const auto& entry :
         fs::recursive_directory_iterator(test_dir)) {
        if (entry.is_regular_file()) {
            all_files.push_back(entry.path());
        }
    }

    const std::size_t kWorkers = 8;
    std::vector<WorkerStats> results(kWorkers);

    {
        std::vector<JoiningThread> workers;
        std::size_t chunk = all_files.size() / kWorkers;

        for (std::size_t i = 0; i < kWorkers; ++i) {
            auto start = all_files.begin() + i * chunk;
            auto end = (i == kWorkers - 1)
                           ? all_files.end()
                           : start + chunk;

            workers.emplace_back(
                [&results, worker_id = i,
                 files = std::vector<fs::path>(start, end)]() {
                    WorkerStats local_stats;
                    for (const auto& f : files) {
                        local_stats.files_scanned++;
                        local_stats.total_bytes +=
                            fs::file_size(f);
                    }
                    results[worker_id] = local_stats;
                });
        }
    }

    std::size_t total = 0;
    for (const auto& s : results) {
        total += s.files_scanned;
    }

    REQUIRE(total == 100);
    // 这个测试在 TSan 下应该没有任何报告

    fs::remove_all(test_dir);
}
```

Run all tests under TSan to confirm there are no data races from Milestone 1 through 4:

```bash
./build/lab0_tests "[lab0]" --tsan
```

## Self-Check Checklist

Before submitting, go through the following items one by one:

- [ ] All Milestone 1 tests pass — parallel scanning misses no files
- [ ] All Milestone 2 tests pass — `JoiningThread` automatically joins on both normal and exception paths
- [ ] All Milestone 3 tests pass — no dangling references, move-only parameters passed correctly
- [ ] All Milestone 4 tests pass — `thread_local` statistics match single-threaded results
- [ ] All tests produce no data race reports under TSan
- [ ] There are no cases where a `std::thread` with `joinable()` true is destroyed
- [ ] No use of `detach()` to dodge lifetime management
- [ ] Can verbally explain the necessity of `try-catch` in the `JoiningThread` destructor
- [ ] Can explain the difference between lambda captures of `[&]` vs. `[=]` vs. `[x = std::move(y)]` in multithreaded scenarios
- [ ] Can explain the two advantages of the `thread_local` statistics pattern over global atomics (contention-free + support for complex structures)
