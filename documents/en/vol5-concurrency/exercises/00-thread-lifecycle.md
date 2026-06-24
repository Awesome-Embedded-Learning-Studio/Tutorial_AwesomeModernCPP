---
title: 'Lab 0: Thread Lifecycle Lab'
description: We train practical skills in thread creation, RAII wrappers, parameter
  lifetimes, and thread-local statistics using a parallel file scanner.
chapter: 10
order: 0
tags:
- host
- cpp-modern
- intermediate
- atomic
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 17
prerequisites:
- '卷五 ch00: 并发思维与基础'
- '卷五 ch01: 线程生命周期与 RAII'
related:
- 并发基本问题
- std::thread 基础
- 线程所有权与 RAII
translation:
  source: documents/vol5-concurrency/exercises/00-thread-lifecycle.md
  source_hash: ff4f57476dec5b5d89b2ce4d45333b7aa37f6a7714a8be66b5ffee096c7fea97
  translated_at: '2026-06-24T01:09:29.393389+00:00'
  engine: anthropic
  token_count: 3750
---
# Lab 0: Thread Lifecycle Lab

> The runnable project for this Lab is available at [`code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/`](../../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/). Estimated hands-on time is **4–6 hours** (note that `reading_time_minutes` refers to reading time only, not implementation time).

## Objectives

Having read the four articles in ch01, we now know how to create `std::thread`, how to pass arguments, how to write `JoiningThread`, and how to use `thread_local`. However, the gap between "knowing" and "having written" is, frankly, larger than many imagine. A typical experience goes like this: you read the RAII wrapper code and think, "I got this," but when you write a multithreaded program yourself, you run it under TSan and find data races everywhere, or some exception path causes you to forget about a thread.

The goal of this Lab is straightforward: we will write a **parallel file scanner**. The main thread shards files in a directory and distributes them to N worker threads for scanning. Each worker counts statistics for the files it is responsible for (size, extension distribution), and finally, the main thread aggregates the results. The project isn't huge, but it will force you to face four core problems: how to create and manage multiple threads, how to use RAII to ensure threads don't leak on exception paths, how to safely pass arguments to threads, and how to use thread-local statistics for lock-free aggregation.

After completing this Lab, you should have a reusable `JoiningThread` wrapper and a "per-worker local stats + main thread aggregation" pattern that you can use directly in subsequent Labs.

## Prerequisites

Before starting, ensure you have read the following chapters:

- **ch00-01** Why We Need Concurrency — Concurrency vs. Parallelism, Amdahl's Law
- **ch00-02** Basic Concurrency Problems — data race, race condition, deadlock
- **ch00-03** CPU Cache & OS Threads — cache line, false sharing
- **ch01-01** std::thread Basics — creation, join/detach, hardware_concurrency
- **ch01-02** Thread Arguments & Lifecycle — decay-copy, dangling references, move-only
- **ch01-03** Thread Ownership & RAII — thread_guard, joining_thread, exception safety
- **ch01-04** thread_local & call_once — thread-local storage

This Lab has no dependencies on previous Labs.

## Project Scaffold (Get This Running First)

This section marks the biggest difference between this Lab and the old version: **we will not paste a bunch of scattered code snippets for you to assemble**. Instead, we provide a directly buildable project. All tests are written; you only need to complete the implementation.

Each Lab has two versions under [vol5-labs/]: **`templates/lab0_thread_lifecycle/`** is the empty implementation skeleton (copy this one to work on), and **`examples/lab0_thread_lifecycle/`** is the reference implementation (consult this if you get stuck, but don't copy it first). Both are standalone projects. You will be working on the `templates` version, which is structured as follows:

```text
templates/lab0_thread_lifecycle/
├── CMakeLists.txt       # standalone: FetchContent 拉 Catch2 + INTERFACE 库 + test
├── include/lab0/        ← 你在这里补全实现
│   ├── file_info.h      #   数据结构（已给全，不用改）
│   ├── worker_stats.h   #   数据结构（已给全，不用改）
│   ├── joining_thread.h  #   Milestone 2 实现
│   └── file_scanner.h   #   Milestone 1/3/4 实现
└── test/                # 教程提供的测试（不用改，可选补边界测试）
    ├── test_helpers.h
    └── test_milestone1.cpp … test_milestone4.cpp
```

For build instructions and the dogfooding feedback process for the entire `vol5-labs/` directory, see [`vol5-labs/README.md`](../../../../code/volumn_codes/vol5-labs/README.md). Please read it first.

First build (requires internet connection; FetchContent will pull Catch2 v3):

```bash
cd code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # Debug 默认开 ThreadSanitizer
cmake --build build
```

**Expected: The build stops at the linking stage, reporting `undefined reference to lab0::FileScanner::scan()`** — this is intentional. `file_scanner.h` and `joining_thread.h` currently contain declarations without implementations, and the linker is reminding you that "it's time to get to work." This is the starting point of the TDD-style exercise: the tests are already written and waiting for you. As you gradually fill in the implementations, the tests for the corresponding milestones will turn from red to green.

> Why use the Debug configuration? Because the correctness of concurrent code cannot rely solely on "it runs" — TSan is our primary diagnostic tool, and it is automatically enabled in Debug builds via `-fsanitize=thread`. Note: **Catch2 does not have a runtime parameter like `--tsan`**; TSan is enabled at compile time, so running the tests directly places them under TSan supervision. To run a single milestone:

```bash
./build/test/test_milestone1                          # 跑 milestone 1
./build/test/test_milestone2 "[lab0][milestone2]"     # Catch2 标签过滤
ctest --test-dir build --output-on-failure                                  # 跑全部
```

## Final Interface

Before we start, let's clarify the target shape. These interfaces match the headers in `include/lab0/` exactly—feel free to open the headers for reference.

### `FileInfo` — Single File Scan Result (Provided, Data Structure)

| Type | Member | Semantics |
|------|--------|-----------|
| `std::filesystem::path` | `path` | Full file path |
| `std::uintmax_t` | `file_size` | File size (bytes) |
| `std::string` | `extension` | Extension including the dot (e.g., `.cpp`) |

### `WorkerStats` — Per-Worker Statistics Summary (Provided, Data Structure)

| Type | Member | Semantics |
|------|--------|-----------|
| `std::size_t` | `files_scanned` | Number of files scanned |
| `std::uintmax_t` | `total_bytes` | Total bytes scanned |
| `std::unordered_map<std::string, std::size_t>` | `ext_counts` | Extension → Occurrence count |

`worker_stats.h` also provides `operator+=`, which the main thread can use directly to aggregate results from each worker.

### `JoiningThread` — RAII Thread Wrapper (You will implement this in Milestone 2)

Move-only, non-copyable. Interface (see `include/lab0/joining_thread.h`):

| Method | Signature | Milestone |
|------|------|-----------|
| Template Constructor | `JoiningThread(Callable&&, Args&&...)` | MS2 (Implementation provided) |
| Adopt thread | `JoiningThread(std::thread) noexcept` | MS2 |
| move ctor/assign | `JoiningThread(JoiningThread&&)` / `operator=(JoiningThread&&)` | MS2 |
| Destructor | `~JoiningThread()` — joins if joinable | MS2 |
| join / joinable | `void join()` / `bool joinable() const noexcept` | MS2 |

### `FileScanner` — File Scanner (Main Component, Evolves in Milestone 1/3/4)

| Method | Signature | Milestone |
|------|------|-----------|
| Constructor | `FileScanner(path root, size_t num_workers)` | MS1 |
| scan | `WorkerStats scan()` | MS1→MS4 (Interface stable, internal implementation evolves) |

Next, we break it down by milestone and implement step by step.

## Milestone 1: Parallel Task Dispatch

### Goal

Implement the first version of `FileScanner::scan()`: use raw `std::thread` to launch a fixed number of workers, where each worker scans a segment of files, and use a set of global `std::atomic` variables to accumulate file counts and total bytes. Let's get "multiple threads working simultaneously" working first, without chasing perfection.

### Why start here

This is the most basic layer. Subsequent milestones will gradually improve upon this foundation—RAII wrapping, parameter safety, thread-local statistics—introducing only one new engineering problem at a time. If we aim for a perfect architecture from the start, it's easy to get stuck in the trap of "fussing over interface design before anything runs."

### Implementation Guide

The overall idea is divided into four steps:

1. Use `std::filesystem::recursive_directory_iterator` in the **main thread** to collect all `regular_file` paths into a `std::vector`;
2. Divide equally by the number of workers (the last worker takes the remainder);
3. Create N `std::thread`s, where each thread iterates over its segment, counting files and total size;
4. Manually `join()` all threads and return the aggregated result.

The **"recursive"** in the name of the `recursive_directory_iterator` in Step 1 is **key**: it **recursively enters all subdirectories depth-first**, so you receive regular files from the entire `root` directory tree, not just the current directory level. `is_regular_file()` is only responsible for filtering out "subdirectories, symbolic links, and special files" from the entries encountered; it has nothing to do with recursion—recursion is an attribute of the **iterator**. To scan only the top-level directory and not enter subdirectories, you would need to switch to `std::filesystem::directory_iterator` (without the `recursive_` prefix). Additionally, `recursive_directory_iterator` defaults to `directory_options::none`, meaning it **does not follow symbolic links to directories**, only real subdirectories. For this lab, which scans the whole tree, using recursive with the defaults is fine.

Pseudocode:

```text
// 1. 主线程收集（iterator 非线程安全，不能并发递增）
all_files = [p for p in recursive_directory_iterator(root) if p.is_regular_file()]

// 2. 等分
chunk = all_files.size() / num_workers
for i in [0, num_workers):
    start = i * chunk
    end   = (i == num_workers-1) ? all_files.size() : start + chunk

// 3. 启动 worker
threads[i] = thread(worker, all_files[start:end])   // 按值传，decay-copy 给 worker 一份副本

// 4. join
for t in threads: t.join()
return 汇总
```

We start with the simplest approach using global `std::atomic<std::size_t>` and `std::atomic<std::uintmax_t>`. Each worker calls `fetch_add` when it finds a file. This method incurs contention overhead (all workers competing for the same atomic), but it is sufficient to get the skeleton working. We will replace it in Milestone 4.

> **Warning**: `recursive_directory_iterator` is **not thread-safe**—we cannot increment the same iterator from multiple threads simultaneously. Therefore, the path collection step must be completed in the main thread, and workers should only process the already collected `vector`. Additionally, arguments passed to `std::thread` are decay-copied, so passing `vector` slices by value is safe (each worker gets its own independent copy). Doing it this way is perfectly fine for this milestone; we will examine capture methods in detail in Milestone 3. One more thing: if the test directory has very few files (e.g., opening 8 workers for 3 files), some workers will receive empty lists—your worker function must handle empty input correctly.

### Verification

The corresponding tests are in [`test/test_milestone1.cpp`](../../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone1.cpp), covering three scenarios: successfully scanning and collecting all files, not crashing on an empty directory, and ensuring the total byte count is correct. Key assertions:

```cpp
TEST_CASE("MS1: scan collects all files", "[lab0][milestone1]") {
    // ... 创建 20 个测试文件 ...
    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats stats = scanner.scan();
    REQUIRE(stats.files_scanned == 20);
}
```

After completing the MS1 implementation of `scan()`, run:

```bash
./build/test/test_milestone1
```

Tests turn green when they pass. **Remember to run with TSan** (the Debug build runs directly under TSan), and confirm there are no data races.

## Milestone 2: RAII Wrapper

### Objective

Implement `JoiningThread`—an RAII wrapper that automatically calls `join()` upon destruction. Then, use it to replace the raw `std::thread` in Milestone 1's `scan()` function, remove the manual join loop, and verify that threads are still correctly cleaned up in exception paths.

### Why

The manual `join()` in Milestone 1 has an obvious flaw: if an exception is thrown before the join loop, the remaining threads become orphaned. When their destructors run, `std::terminate()` is called. ch01-03 covered the root cause and the RAII solution; this milestone moves us from "understanding" to "implementing and practically using" it.

### Implementation Guide

The core of `JoiningThread` is to take ownership of a `std::thread` and automatically `join()` in the destructor. The templated constructor (accepting any Callable + arguments) is already provided in the project (using `std::forward` for perfect forwarding); you need to implement the remaining members. There are three design points you must clarify:

**First, in move assignment, we must handle the currently held thread before accepting the new one.** If the current `thread_` is still `joinable()`, we must join it first. Otherwise, the old thread is overwritten and discarded, triggering `std::terminate` upon its destruction. This pattern of "clean up the old before taking over the new" is the same logic as `std::unique_ptr` assignment.

**Second, the `join()` in the destructor can throw `std::system_error`.** Throwing an exception in a destructor triggers `std::terminate`. The pragmatic approach is to wrap it in `try/catch` and swallow the exception. Don't skip this just because you think "join can't fail"—the difference in production-grade code often lies in these seemingly redundant defenses.

**Third, `joinable()` simply returns `thread_.joinable()`.**

> **On defining in headers**: `JoiningThread` is not a template class (only the constructor is a template), so the other members can be defined inside the class (implicitly `inline`, including it in multiple translation units won't cause ODR violations). You can simply change the declarations to definitions `{ ... }` inside the class body in `joining_thread.h`; no separate `.cpp` is needed.

After implementing `JoiningThread`, go back to `file_scanner.h` and replace `std::vector<std::thread>` in `scan()` with `std::vector<lab0::JoiningThread>`. Delete the manual join loop—when the `vector` is destroyed, each `JoiningThread` will automatically join.

### Verification

> **Don't be fooled by the tests**: `test_milestone2` only tests the `JoiningThread` class itself (decoupled from `FileScanner`), and **does not check if `scan()` actually uses it**. So, even if you implement `JoiningThread` and all tests pass, but `scan()` still uses raw `std::thread` + a manual `join()` loop—this milestone isn't truly complete. **The real acceptance criteria: no manual `join()` loop is visible in `scan()`, and the thread container is `std::vector<lab0::JoiningThread>`.**

[`test/test_milestone2.cpp`](../../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone2.cpp) only tests `JoiningThread` itself (decoupled from `FileScanner`), covering four scenarios: automatic join on scope exit, joining all workers even in exception paths, move transfer of ownership, and `vector` destruction joining all. Focus on the exception path scenario:

```cpp
TEST_CASE("MS2: exception path still joins all workers", "[lab0][milestone2]") {
    std::atomic<int> counter{0};
    auto make_workers = [&]() {
        std::vector<lab0::JoiningThread> workers;
        for (int i = 0; i < 4; ++i)
            workers.emplace_back([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        throw std::runtime_error("simulated failure");  // workers 在栈展开时析构 → 自动 join
    };
    REQUIRE_THROWS_AS(make_workers(), std::runtime_error);
    REQUIRE(counter.load() == 4);   // 异常后 4 个 worker 都已完成
}
```

Without RAII, this scenario would result in a direct call to `std::terminate`.

## Milestone 3: Fixing Parameter Lifetimes

### Goal

Review the parameter passing methods within `scan()`, identify and fix all potential dangling references and lifetime issues. The core objective is to ensure each worker receives an independent copy of the file list (via value capture or move), avoiding the capture of potentially dangling references.

### Why

Chapter 01-02 discussed the decay-copy semantics of `std::thread` and the risks of dangling references. However, these issues often remain hidden in small examples because variable lifetimes happen to be sufficient. In a real-world scanner, the situation is more complex: the main thread might start cleaning up temporary data before workers have finished, or a lambda might capture a reference to a local `vector`. These bugs might not trigger during development but can manifest unpredictably under high concurrency stress.

### Implementation Guide

In Milestone 1 (MS1), we passed the file path list to workers by value—which is actually safe because decay-copy provides an independent copy. However, the problem lies in more subtle areas. You need to be able to identify three error-prone patterns:

**Reference capturing local variables**. If you try to save effort by writing `[&all_files, start, end]`, and `all_files` is destroyed or modified while the worker is running, you get a dangling reference. In this lab, the lifetime of `all_files` is long enough, but this style makes correctness depend on the caller's implicit understanding of lifetimes—which is a bad habit.

**Passing arguments with `std::ref`**. If you try to avoid copying by using references: `threads.emplace_back(worker, std::ref(chunk_files))`. If `chunk_files` is a local variable within the loop body and gets modified in the next iteration, the previous worker reads modified data—a data race. The fix is to capture by value or use `std::move`.

**Implicit `this` capture**. If you put scanning logic into a `FileScanner` member function and use member variables in a lambda, `[this]` implicitly creates a dependency on the lifetime of the `FileScanner` object. This pitfall is particularly easy to fall into in Lab 3 (Thread Pool)—where the thread pool's lifetime often exceeds what the caller expects.

> **The fix is simple**: Use value capture or `std::move` for the worker's file list (init-capture `files = std::move(worker_files)`), and capture `worker_id` by value `[worker_id = i]`. Then run with TSan—with a correct implementation, TSan should not report any data races.

### Verification

[`test/test_milestone3.cpp`](../../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone3.cpp) verifies that: non-divisible splits cover all files (30 files / 8 workers), prime file counts (17 files) result in no dropped files for any split, and move-only types (`unique_ptr`) can be safely passed into threads. For example, the prime number case:

```cpp
TEST_CASE("MS3: prime file count covered by any worker count", "[lab0][milestone3]") {
    // ... 创建 17 个文件（素数，任何分片都非整除）...
    lab0::FileScanner scanner(dir, 4);
    REQUIRE(scanner.scan().files_scanned == 17);   // 一个都不能丢
}
```

If your chunking logic is incorrect at the `start..end` boundaries, the prime file count will expose it most easily.

## Milestone 4: Thread-Local Statistics and Aggregation

### Goal

Replace the global `std::atomic` statistics from Milestone 1 with a "one local `WorkerStats` per worker, write-back to preallocated result slots, main thread aggregation" approach. This eliminates contention on global atomics and supports complex data like extension distributions.

### About `thread_local`: Think Before You Use

There is a subtle point here that is easy to get wrong. Many people instinctively write `thread_local WorkerStats local;` when they see "thread-local statistics"—but **in this Lab's scenario, a plain local variable `WorkerStats local;` is functionally equivalent to `thread_local`**, because each worker executes only once.

The real value of `thread_local` lies in: **state reuse and accumulation when the same thread enters the same function multiple times**. For example, a worker thread in a thread pool repeatedly fetches tasks from a queue and executes them, accumulating into the same statistics structure each time—this is when `thread_local` makes sense. In this Lab, each worker performs a one-time scan, so a plain local variable is sufficient and simpler.

Therefore, the requirement for this milestone is not "you must use the `thread_local` keyword," but rather: **correct statistics and a clean TSan report**. It is perfectly fine to implement this using plain local variables. Understanding the difference between these two is far more important than rote memorization of keywords.

### Implementation Guide

The core idea: the main thread pre-allocates `std::vector<WorkerStats> results(num_workers)`. Each worker writes its local statistics back to the corresponding slot: `results[worker_id] = local;` (different workers write to different slots, so there is no contention). Finally, the main thread iterates through `results` to aggregate:

```cpp
// scan() 里
std::vector<WorkerStats> results(num_workers_);

{
    std::vector<lab0::JoiningThread> workers;
    // ... 每个 worker:
    //   WorkerStats local;
    //   for (f : files) { local.files_scanned++; local.total_bytes += ...; local.ext_counts[...]++; }
    //   results[worker_id] = std::move(local);
}
// ← 见下方踩坑：必须在这里之前 join 完所有 worker

WorkerStats total;
for (auto& s : results) total += s;   // operator+= 已提供
return total;
```

> **Warning: This Pithole Bites**：Pay attention to the `{ }` scope above—it is not decorative. The destruction of `workers` (which triggers `join`) happens at the **end of the scope**; the summary loop `for (s : results)` executes **after** the scope. If you try to save space and put `workers` and the summary at the same level (so `workers` is only destroyed when the function returns), the summary might read `results` while workers are still writing to it—**data race**.
>
> I'm not making this up: while writing this handbook, I ran a "seemingly correct" implementation (all assertions passed) through TSan, and it caught it immediately—the main thread read `results` before joining, and the `operator+=` line reported a data race. The lesson is hard: **Before summarizing results, ensure all workers have been joined**. Using `{ }` to limit the lifetime of `workers` to before the summary is the cleanest approach. Don't rely on "natural destruction on function return"—by that time, the summary has long finished reading.

Another small point: the `worker_id` in `results[worker_id]` must be unique to each worker and captured by value `[worker_id = i]`. Do not use a reference to `i` (don't let the bug you just fixed in Milestone 3 come back).

### Verification

> **Don't Be Fooled by Tests**：`test_milestone4` only verifies if the numerical results are correct (consistent with single-threaded execution), **it does not check if the statistics are truly "local to each worker"**. So even if all tests pass, if `scan()` still uses a shared `mutex`/`atomic` for statistics—you are actually still at MS1, and this milestone is not truly complete. **The real acceptance criteria: no locks or shared atomics in `scan()`, statistics go to independent `results[worker_id]` slots + main thread aggregation.**

[`test/test_milestone4.cpp`](../../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone4.cpp) verification: Multi-threaded scan results are **completely consistent** with single-threaded sequential scanning (file count, byte count, and extension distribution must all match), plus a stress test with 200 files and 8 workers. Key assertions:

```cpp
TEST_CASE("MS4: multi-threaded stats match single-threaded baseline", "[lab0][milestone4]") {
    // 创建 .cpp×10, .h×5, .txt×3；先单线程算 expected
    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats actual = scanner.scan();
    REQUIRE(actual.files_scanned == expected.files_scanned);
    REQUIRE(actual.ext_counts[".cpp"] == 10);
    // ...
}
```

The stress test should run clean under TSan with zero reports. If you fell into the timing trap mentioned earlier regarding `join`, the TSan output will explicitly point to `operator+=` in `worker_stats.h`—if you see that, go back and check if you joined before aggregating.

## Self-Check List

Confirm each item before submitting:

- [ ] Milestone 1 tests pass—parallel scan does not miss files, empty directories do not crash, and byte counts are correct.
- [ ] Milestone 2 tests pass—`JoiningThread` automatically joins on both normal and exceptional paths, and move semantics are correct.
- [ ] Milestone 3 tests pass—prime/non-divisible chunking does not miss files, and move-only arguments are passed safely.
- [ ] Milestone 4 tests pass—multithreaded statistics match single-threaded results exactly (including extension distribution).
- [ ] **MS2 Real Verification**: `scan()` uses `std::vector<lab0::JoiningThread>`, with no manual `join()` loop (don't just rely on `test_milestone2` being green—that test doesn't inspect `scan`).
- [ ] **MS4 Real Verification**: `scan()` uses no locks or shared atomics; statistics go into `results[worker_id]` independent slots + main thread aggregation (don't just rely on `test_milestone4` being green—that test doesn't check the implementation).
- [ ] **All tests run under TSan with no data race reports** (run directly on a Debug build).
- [ ] No `std::thread` with `joinable()` returning true is destroyed.
- [ ] `detach()` is not used to bypass lifetime management.
- [ ] Before aggregating worker results, all workers are confirmed joined (use `{ }` scopes; don't rely on function return destructors).
- [ ] Can verbally explain the necessity of `try/catch` in the `JoiningThread` destructor.
- [ ] Can explain the difference between `[&]`, `[=]`, and `[x = std::move(y)]` in a multithreaded context.
- [ ] Can explain the two advantages of "per-worker local stats + aggregation" over global atomics (no contention + support for complex data structures).
- [ ] Can explain the difference between `thread_local` in this scenario versus a "worker repeatedly fetching tasks" scenario.

## Extensions (Bonus)

After completing the mainline, optional challenges:

- Sort and output scan results by extension to practice traversing and sorting `unordered_map`.
- Add a `--recursive=false` option to scan only the top-level directory (non-recursive) to practice interface design.
- Refactor `JoiningThread` using `std::jthread` + `stop_token` to experience C++20 cooperative cancellation (this is a preview of ch05).

These are not covered by tests; do them just for the satisfaction.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [ThreadSanitizer — Clang Docs](https://clang.llvm.org/docs/ThreadSanitizer.html)
- [`std::filesystem::recursive_directory_iterator` — cppreference](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
