---
title: 'Lab 0: Thread Lifecycle'
description: "Build hands-on skills in thread creation, RAII wrapping, parameter lifetimes, and thread-local statistics through a parallel file scanner"
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
cpp_standard: [17]
prerequisites:
  - 'Volume 5 ch00: Concurrent Thinking and Fundamentals'
  - 'Volume 5 ch01: Thread Lifecycle and RAII'
related:
  - "Fundamental Concurrency Problems"
  - "std::thread Basics"
  - "Thread Ownership and RAII"
translation:
  source: documents/vol5-concurrency/exercises/00-thread-lifecycle.md
  source_hash: ff4f57476dec5b5d89b2ce4d45333b7aa37f6a7714a8be66b5ffee096c7fea97
  translated_at: '2026-09-26T09:17:12+00:00'
  engine: anthropic
  token_count: 11800
---
# Lab 0: Thread Lifecycle

> The runnable project that goes with this Lab lives at [`code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/). Expect roughly **4–6 hours** of hands-on work (`reading_time_minutes` counts pure reading minutes, not hands-on time).

## Objectives

Having finished the four articles in ch01, we now know how to create a `std::thread`, how to pass arguments, how to write a `JoiningThread`, and how to use `thread_local`. But the distance between "knowing" and "having written", frankly, is bigger than many people imagine. A typical experience goes like this: you read the RAII wrapper code and think "I've got this", then you write a multithreaded program yourself, run it under TSan, and data races are everywhere — or some exception path simply forgets about a thread.

The goal of this Lab is blunt: we are going to write a **parallel file scanner** — the main thread shards the files under a directory and hands them out to N worker threads to scan; each worker tallies statistics for the files it owns (sizes, extension distribution), and the main thread aggregates everything at the end. The project isn't big, but it will force you to face four core questions head-on: how to create and manage multiple threads, how to use RAII so exception paths don't leak threads, how to pass arguments to threads safely, and how to use thread-local statistics for race-free aggregation.

Once you finish this Lab, you should walk away with a reusable `JoiningThread` wrapper and a "per-worker local statistics + main-thread aggregation" pattern that you can plug directly into the later Labs.

## Prerequisites

Before starting, make sure you have read the following chapters:

- **ch00-01** Why We Need Concurrency — concurrency vs. parallelism, Amdahl's Law
- **ch00-02** Fundamental Concurrency Problems — data race, race condition, deadlock
- **ch00-03** CPU Cache and OS Threads — cache line, false sharing
- **ch01-01** std::thread Basics — creation, join/detach, hardware_concurrency
- **ch01-02** Thread Arguments and Lifecycle — decay-copy, dangling references, move-only
- **ch01-03** Thread Ownership and RAII — thread_guard, joining_thread, exception safety
- **ch01-04** thread_local and call_once — thread-local storage

This Lab has no dependency on previous Labs.

## Project Scaffold (Get This Running First)

This section is the biggest difference between this Lab and the old version: **we do not paste a pile of scattered code snippets in the article for you to assemble** — instead we hand you a project that builds out of the box. All the tests are already written; you only fill in the implementation.

Each Lab ships in two copies under [vol5-labs/]: **`templates/lab0_thread_lifecycle/`** is the empty implementation skeleton (copy this one and work on it), and **`examples/lab0_thread_lifecycle/`** is the reference implementation (consult it when you are stuck; don't copy it up front). Both are standalone projects. You work on the templates copy, structured like this:

```text
templates/lab0_thread_lifecycle/
├── CMakeLists.txt       # standalone: FetchContent pulls Catch2 + INTERFACE library + test
├── include/lab0/        ← you fill in the implementation here
│   ├── file_info.h      #   data structure (fully provided, no changes needed)
│   ├── worker_stats.h   #   data structure (fully provided, no changes needed)
│   ├── joining_thread.h  #   implemented in Milestone 2
│   └── file_scanner.h   #   implemented in Milestones 1/3/4
└── test/                # tests provided by the tutorial (no changes needed; extra edge-case tests optional)
    ├── test_helpers.h
    └── test_milestone1.cpp … test_milestone4.cpp
```

Build instructions for the entire `vol5-labs/` directory, plus the dogfooding feedback process, live in [`vol5-labs/README.md`](../../../code/volumn_codes/vol5-labs/README.md). Read it first.

First build (requires internet access; FetchContent pulls Catch2 v3):

```bash
cd code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # Debug enables ThreadSanitizer by default
cmake --build build
```

**Expected: the build stops at the link stage, reporting `undefined reference to lab0::FileScanner::scan()`** — this is deliberate. `file_scanner.h` and `joining_thread.h` currently hold declarations with no implementations, and the linker is nudging you: time to get to work. That is the starting point of this TDD-style exercise: the tests are already written and waiting for you; as you fill in the implementation step by step, the tests for the corresponding milestones turn from red to green.

> Why the Debug configuration? Because the correctness of concurrent code can never rest on "well, it ran" — TSan is our primary diagnostic tool, and Debug builds enable it automatically via `-fsanitize=thread`. Note: **Catch2 has no `--tsan` run flag** — TSan is switched on at compile time, so simply running the tests already runs them under TSan. To run a single milestone:

```bash
./build/test/test_milestone1                          # run milestone 1
./build/test/test_milestone2 "[lab0][milestone2]"     # Catch2 tag filtering
ctest --test-dir build --output-on-failure                                  # run everything
```

## Final Interface

Before touching the keyboard, look carefully at the target shape. These interfaces match the headers under `include/lab0/` in the project exactly — you can open the headers and cross-check at any time.

### `FileInfo` — Per-File Scan Result (Provided, Data Structure)

| Type | Member | Semantics |
|------|------|------|
| `std::filesystem::path` | `path` | Full path of the file |
| `std::uintmax_t` | `file_size` | File size (bytes) |
| `std::string` | `extension` | Extension (with the dot, e.g. `.cpp`) |

### `WorkerStats` — Per-Worker Statistics Summary (Provided, Data Structure)

| Type | Member | Semantics |
|------|------|------|
| `std::size_t` | `files_scanned` | Number of files scanned |
| `std::uintmax_t` | `total_bytes` | Total bytes scanned |
| `std::unordered_map<std::string, std::size_t>` | `ext_counts` | Extension → occurrence count |

`worker_stats.h` also provides `operator+=`, which the main thread uses directly when aggregating each worker's results.

### `JoiningThread` — RAII Thread Wrapper (You Implement It in Milestone 2)

Move-only, non-copyable. Interface (see `include/lab0/joining_thread.h`):

| Method | Signature | Milestone |
|------|------|-----------|
| Templated constructor | `JoiningThread(Callable&&, Args&&...)` | MS2 (implementation provided) |
| Adopt a thread | `JoiningThread(std::thread) noexcept` | MS2 |
| move constructor/assignment | `JoiningThread(JoiningThread&&)` / `operator=(JoiningThread&&)` | MS2 |
| Destructor | `~JoiningThread()` — joins if joinable | MS2 |
| join / joinable | `void join()` / `bool joinable() const noexcept` | MS2 |

### `FileScanner` — The File Scanner (Main Vehicle, Evolving Through Milestones 1/3/4)

| Method | Signature | Milestone |
|------|------|-----------|
| Constructor | `FileScanner(path root, size_t num_workers)` | MS1 |
| scan | `WorkerStats scan()` | MS1→MS4 (interface unchanged, the internals get replaced step by step) |

Next we break it down milestone by milestone and implement it one step at a time.

## Milestone 1: Parallel Task Dispatch

### Goal

Implement the first version of `FileScanner::scan()`: launch a fixed number of workers on raw `std::thread`s, have each worker scan one segment of the files, and use a set of global `std::atomic` counters to accumulate the file count and total bytes. First get "multiple threads working at the same time" up and running; don't chase perfection.

### Why Start Here

This is the most basic layer. Later milestones improve on it step by step — RAII wrapping, argument safety, thread-local statistics — and each step introduces exactly one new engineering problem. If you chase the perfect architecture from the very start, it is easy to sink into the trap of agonizing over interface design before anything has run at all.

### Implementation Guide

The overall plan has four steps:

1. Use `std::filesystem::recursive_directory_iterator` in the **main thread** to collect every `regular_file` path into a `std::vector`;
2. Split it evenly by worker count (the last worker takes the remainder as a fallback);
3. Create N `std::thread`s; each thread walks its own segment, tallying the file count and total size;
4. Manually `join()` every thread and return the aggregated result.

That **"recursive" in the name of `recursive_directory_iterator` in step 1 is the key**: it **descends depth-first into every subdirectory**, so what you receive is the regular files of the entire directory tree under `root`, not just the top-level directory. `is_regular_file()` only filters "subdirectories, symlinks, special files" out of the entries visited — it has nothing to do with recursion, which is a property of the **iterator**. If you want to scan only the top level without descending into subdirectories, you must switch to `std::filesystem::directory_iterator` (no `recursive_` prefix). Also note that `recursive_directory_iterator` defaults to `directory_options::none`, which **does not follow symlinks pointing to directories** — it only recurses into real subdirectories. This Lab scans the whole tree, so recursive with the defaults is exactly right.

Pseudocode:

```text
// 1. Collected in the main thread (the iterator is not thread-safe; no concurrent incrementing)
all_files = [p for p in recursive_directory_iterator(root) if p.is_regular_file()]

// 2. Even split
chunk = all_files.size() / num_workers
for i in [0, num_workers):
    start = i * chunk
    end   = (i == num_workers-1) ? all_files.size() : start + chunk

// 3. Launch workers
threads[i] = thread(worker, all_files[start:end])   // passed by value; decay-copy gives the worker its own copy

// 4. join
for t in threads: t.join()
return aggregate
```

For statistics, start with the simplest thing possible: global `std::atomic<std::size_t>` and `std::atomic<std::uintmax_t>`, with each worker doing a `fetch_add` for every file it scans. This design carries contention overhead (all workers hammering the same atomic), but it is enough to get the skeleton running; Milestone 4 will replace it.

> **Pitfall alert**: `recursive_directory_iterator` is **not thread-safe** — multiple threads must never increment the same iterator concurrently. That is why the path collection step must be finished in the main thread; workers only chew through the already-collected `vector`. Also, arguments passed to `std::thread` are decay-copied, so passing `vector` slices by value is safe (each worker gets an independent copy). For this milestone that is completely fine; we scrutinize capture strategies in Milestone 3. One more thing: if the test directory has very few files (say 3 files with 8 workers), some workers will receive empty lists — your worker function must handle empty input correctly.

### Verification

The corresponding tests live in [`test/test_milestone1.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone1.cpp), covering three scenarios: the scan collects all files, an empty directory doesn't crash, and the total byte count is correct. Key assertion:

```cpp
TEST_CASE("MS1: scan collects all files", "[lab0][milestone1]") {
    // ... create 20 test files ...
    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats stats = scanner.scan();
    REQUIRE(stats.files_scanned == 20);
}
```

After filling in the MS1 implementation of `scan()`, run:

```bash
./build/test/test_milestone1
```

Tests turning green means pass. **Remember to run it under TSan** (a Debug build runs under TSan directly) and confirm there are no data races.

## Milestone 2: RAII Wrapper

### Goal

Implement `JoiningThread` — an RAII wrapper that calls `join()` automatically in its destructor. Then use it to replace the raw `std::thread`s in Milestone 1's `scan()`, delete the manual join loop, and verify that exception paths still reclaim threads correctly.

### Why

Milestone 1's manual `join()` has an obvious problem: if something throws before the join loop, the remaining threads become ownerless, and destroying them triggers `std::terminate()`. ch01-03 covered the root cause and RAII's answer; this milestone pushes it from "understood" to "implemented and used for real".

### Implementation Guide

The core of `JoiningThread` is taking ownership of a `std::thread` and calling `join()` automatically in the destructor. The templated constructor (accepting any Callable plus arguments) is already provided in the project (it uses `std::forward` for perfect forwarding); you implement the remaining members. Three design points must be thought through:

**First, in the move assignment, deal with the thread you currently hold before accepting the new one.** If the current `thread_` is still `joinable()`, you must join it first; otherwise the old thread gets overwritten and dropped, and destroying it triggers `std::terminate`. This "clean up the old before taking on the new" pattern is the same deal as assignment for `std::unique_ptr`.

**Second, the `join()` in the destructor can throw `std::system_error`.** Throwing from a destructor triggers `std::terminate`. The pragmatic approach is to wrap it in `try/catch` and swallow the exception. Don't skip this just because "join can't possibly fail" — the difference between production-grade code and the rest often lives in exactly these seemingly redundant defenses.

**Third, `joinable()` simply returns `thread_.joinable()`.**

> **On defining members inside the header**: `JoiningThread` is not a template class (only its constructor is a template), so the remaining members can be defined inside the class body (implicitly `inline`, so including it from multiple translation units causes no redefinition). Just change the declarations in the class body of `joining_thread.h` into definitions `{ ... }`; no separate `.cpp` is needed.

Once `JoiningThread` is done, go back to `file_scanner.h`, swap `std::vector<std::thread>` in `scan()` for `std::vector<lab0::JoiningThread>`, and delete the manual join loop — when the `vector` is destroyed, every `JoiningThread` joins automatically.

### Verification

> **Don't let the tests fool you**: `test_milestone2` only tests the `JoiningThread` class itself (decoupled from `FileScanner`) and **does not check whether `scan()` actually uses it**. So even with `JoiningThread` implemented and every test green, if `scan()` still uses raw `std::thread`s plus a manual `join()` loop, this milestone has not truly been completed. **The real acceptance criteria: no manual `join()` loop visible in `scan()`, and the thread container is `std::vector<lab0::JoiningThread>`.**

[`test/test_milestone2.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone2.cpp) only tests `JoiningThread` itself (decoupled from `FileScanner`), covering four scenarios: automatic join at scope exit, all workers still joined on an exception path, ownership transferred by move, and `vector` destruction joining everything. Pay attention to the exception-path one:

```cpp
TEST_CASE("MS2: exception path still joins all workers", "[lab0][milestone2]") {
    std::atomic<int> counter{0};
    auto make_workers = [&]() {
        std::vector<lab0::JoiningThread> workers;
        for (int i = 0; i < 4; ++i)
            workers.emplace_back([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        throw std::runtime_error("simulated failure");  // workers destroyed during stack unwinding → automatic join
    };
    REQUIRE_THROWS_AS(make_workers(), std::runtime_error);
    REQUIRE(counter.load() == 4);   // after the exception, all 4 workers have completed
}
```

Without RAII, this scenario goes straight to `std::terminate`.

## Milestone 3: Fixing Parameter Lifetimes

### Goal

Scrutinize how `scan()` passes its arguments, and identify and fix every possible dangling reference and lifetime problem. The core requirement: every worker gets an independent copy of its file list (captured by value or moved), and nothing captures a reference that might dangle.

### Why

ch01-02 covered `std::thread`'s decay-copy semantics and the risk of dangling references, but in small examples those problems tend not to surface — variable lifetimes just happen to be long enough. A real scanner is messier: the main thread might start tearing down temporary data before the workers finish, or a lambda captures a reference to a local `vector`. Bugs like these may never fire during development, then show up in unpredictable ways under heavy concurrency stress.

### Implementation Guide

In MS1 we passed the file path list to workers by value — which is in fact already safe (decay-copy hands each worker an independent copy). The danger hides in subtler places; there are three error-prone patterns you must learn to recognize:

**Capturing a local variable by reference.** If you take the shortcut `[&all_files, start, end]`, then the moment `all_files` is destroyed or modified while a worker is still running, you have a dangling reference. In this Lab `all_files` lives long enough, but this style makes correctness depend on the caller's implicit understanding of lifetimes — not a good habit.

**Passing arguments with `std::ref`.** If you want to avoid the copy by using a reference: `threads.emplace_back(worker, std::ref(chunk_files))`. If `chunk_files` is a local variable inside the loop body and gets modified in the next iteration, the previous worker reads modified data — a data race. The fix is capture by value or `std::move`.

**Implicit `this` capture.** If you put the scanning logic into a member function of `FileScanner` and the lambda touches member variables, `[this]` smuggles in a dependency on the `FileScanner` object's lifetime. This trap is especially easy to step on in Lab 3 (thread pool) — a pool's lifetime is often longer than callers expect.

> **The fix is simple**: the worker's file list is captured by value or moved (init-capture `files = std::move(worker_files)`), and `worker_id` is captured by value `[worker_id = i]`. Then run TSan — with a correct implementation, TSan should report zero data races.

### Verification

[`test/test_milestone3.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone3.cpp) verifies: non-divisible splits still cover all files (30 files / 8 workers), prime file counts (17 files) lose nothing under any split, and move-only types (`unique_ptr`) can be passed into threads safely. Take the prime case:

```cpp
TEST_CASE("MS3: prime file count covered by any worker count", "[lab0][milestone3]") {
    // ... create 17 files (a prime, so no split divides evenly) ...
    lab0::FileScanner scanner(dir, 4);
    REQUIRE(scanner.scan().files_scanned == 17);   // not a single file may be lost
}
```

If your chunking logic is off at the `start..end` boundaries, prime file counts will expose it most easily.

## Milestone 4: Thread-Local Statistics and Aggregation

### Goal

Replace Milestone 1's global `std::atomic` statistics with "one local `WorkerStats` per worker, written back to a preallocated result slot, aggregated by the main thread". This eliminates the contention on global atomics and supports richer data like extension distributions.

### About `thread_local`: Think It Through Before Deciding

There is a point people tie themselves into knots over here. Many folks, on seeing "thread-local statistics", reflexively write `thread_local WorkerStats local;` — but **in this Lab's setting, a plain local variable `WorkerStats local;` behaves exactly the same as `thread_local`**, because each worker executes exactly once.

`thread_local`'s real value lies in: **when the same thread enters the same function multiple times, the state gets reused and accumulates**. For example, a worker thread in a thread pool repeatedly pulls tasks off a queue and wants to accumulate into the same statistics on every execution — that is when `thread_local` earns its keep. In this Lab each worker scans once; a plain local variable is enough, and the code is simpler.

So the milestone's requirement is not "you must use the `thread_local` keyword", but: **statistics correct, TSan clean**. Implementing it with plain local variables is perfectly fine. Understanding the difference between the two matters far more than memorizing the keyword.

### Implementation Guide

The core idea: the main thread preallocates `std::vector<WorkerStats> results(num_workers)`; each worker writes its local statistics back to its own slot with `results[worker_id] = local;` (different workers write different slots, so there is no contention), and finally the main thread walks `results` and aggregates:

```cpp
// inside scan()
std::vector<WorkerStats> results(num_workers_);

{
    std::vector<lab0::JoiningThread> workers;
    // ... each worker:
    //   WorkerStats local;
    //   for (f : files) { local.files_scanned++; local.total_bytes += ...; local.ext_counts[...]++; }
    //   results[worker_id] = std::move(local);
}
// ← see the pitfall below: all workers must be joined before this point

WorkerStats total;
for (auto& s : results) total += s;   // operator+= is provided
return total;
```

> **Pitfall alert (this one genuinely bites)**: look at that `{ }` scope above — it is not decoration. `workers`' destruction (which is the `join`) happens at **the end of the scope**, while the aggregation loop `for (s : results)` runs **after** the scope. If you take the convenient route and put `workers` and the aggregation on the same level (letting `workers` destruct only when the function returns), then the aggregation may be reading `results` while workers are still writing — **data race**.
>
> I didn't invent this trap: while writing this handbook, I ran a "looks right" implementation (all assertions passing) under TSan and got caught red-handed — the main thread read `results` before joining, and the `operator+=` line was flagged for a data race. The lesson is hard: **before aggregating results, make sure every worker has been joined**. Restricting `workers`' lifetime to before the aggregation with `{ }` is the cleanest way to write it. Don't lean on "it destructs naturally when the function returns" — by then the aggregation has long finished reading.

One more small point: the `worker_id` in `results[worker_id]` must be unique per worker and captured by value `[worker_id = i]`, never via a reference to `i` (don't let the problem you just fixed in Milestone 3 sneak back in).

### Verification

> **Don't let the tests fool you**: `test_milestone4` only checks that the result numbers are right (matching the single-threaded run); it **does not check whether the statistics are genuinely "local per worker"**. So even with every test green, if `scan()` still does its statistics through a shared `mutex`/`atomic`, you are really still at MS1 and this milestone has not truly been completed. **The real acceptance criteria: no locks and no shared atomics in `scan()`; statistics go through independent `results[worker_id]` slots plus main-thread aggregation.**

[`test/test_milestone4.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone4.cpp) verifies: a multi-threaded scan's results are **exactly identical** to a single-threaded file-by-file scan (all three of file count, byte count, and extension distribution must match), plus a stress test of 200 files / 8 workers. Key assertion:

```cpp
TEST_CASE("MS4: multi-threaded stats match single-threaded baseline", "[lab0][milestone4]") {
    // create .cpp×10, .h×5, .txt×3; compute expected single-threaded first
    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats actual = scanner.scan();
    REQUIRE(actual.files_scanned == expected.files_scanned);
    REQUIRE(actual.ext_counts[".cpp"] == 10);
    // ...
}
```

The stress test runs under TSan and should produce zero reports. If you stepped on the join-timing trap above, this stress test's TSan output will point straight at `operator+=` in `worker_stats.h` — the moment you see that, go back and check whether you joined before aggregating.

## Self-Check List

Go through every item before submitting:

- [ ] Milestone 1 tests pass — the parallel scan misses no files, empty directories don't crash, byte counts are correct
- [ ] Milestone 2 tests pass — `JoiningThread` joins automatically on both normal and exception paths, move semantics are correct
- [ ] Milestone 3 tests pass — prime/non-divisible splits lose no files, move-only arguments are passed safely
- [ ] Milestone 4 tests pass — multi-threaded statistics match the single-threaded result exactly (extension distribution included)
- [ ] **MS2 real acceptance**: `scan()` uses `std::vector<lab0::JoiningThread>` with no manual `join()` loop (not just `test_milestone2` green — that test doesn't inspect `scan`)
- [ ] **MS4 real acceptance**: no locks or shared atomics in `scan()`; statistics go through independent `results[worker_id]` slots plus main-thread aggregation (not just `test_milestone4` green — that test doesn't check the implementation)
- [ ] **All tests run under TSan with zero data-race reports** (just run the Debug build directly)
- [ ] No `std::thread` with `joinable()` true ever gets destroyed
- [ ] `detach()` is not used to dodge lifetime management
- [ ] Before aggregating worker results, all workers are confirmed joined (use the `{ }` scope; don't rely on function-return destruction)
- [ ] You can explain out loud why the `try/catch` in `JoiningThread`'s destructor is necessary
- [ ] You can explain the difference between `[&]` vs `[=]` vs `[x = std::move(y)]` under multithreading
- [ ] You can explain the two advantages of "per-worker local statistics + aggregation" over global atomics (no contention + supports complex data structures)
- [ ] You can explain how `thread_local` differs between this scenario and the "worker repeatedly pulling tasks" scenario

## Extensions (Bonus)

With the mainline done, optional challenges:

- Sort the scan results by extension for output — practice traversing and sorting an `unordered_map`
- Add a `--recursive=false` option that scans only the top-level directory (no recursion) — practice interface design
- Rework `JoiningThread` with `std::jthread` + `stop_token` to get a feel for C++20 cooperative cancellation (a preview of ch05)

None of these are covered by the tests; the satisfaction of building them is its own reward.

## References

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [ThreadSanitizer — Clang documentation](https://clang.llvm.org/docs/ThreadSanitizer.html)
- [`std::filesystem::recursive_directory_iterator` — cppreference](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
