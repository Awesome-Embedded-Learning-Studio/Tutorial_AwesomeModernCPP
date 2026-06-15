---
chapter: 10
cpp_standard:
- 17
- 20
description: 通过并行文件扫描器，训练线程创建、RAII 包装、参数生命周期和 thread_local 统计的实战能力
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
---
# Lab 0: Thread Lifecycle Lab

## 目标

读完了 ch01 的四篇文章，我们现在已经知道 `std::thread` 怎么创建、参数怎么传、`JoiningThread` 怎么写、`thread_local` 怎么用。但"知道"和"写过"之间的距离，说实话，比很多朋友想象的要大。一个很典型的经历是：你看了 RAII 包装的代码觉得"这我懂了"，然后自己写一个多线程程序，一跑 TSan 就发现 data race 满天飞，或者某个异常路径把线程给忘了。

这个 Lab 的目标很直白：我们要写一个**并行文件扫描器**——主线程把一个目录下的文件分片，分发给 N 个 worker 线程去扫描，每个 worker 统计自己负责的文件的信息（大小、扩展名分布等），最后主线程汇总所有 worker 的统计结果。项目不大，但它会逼你直面四个核心问题：怎么创建和管理多个线程、怎么用 RAII 保证异常路径不泄漏线程、怎么安全地给线程传递参数、以及怎么用 `thread_local` 做线程安全的统计。

完成这个 Lab 之后，你应该能拿出一套可以复用的 `JoiningThread` 包装器和 `thread_local` 统计模式，在后续的 Lab 里直接拿来用。

## 前置知识

在开始之前，确保你已经读完以下章节：

- **ch00-01**：为什么需要并发 — 并发 vs 并行、Amdahl 定律
- **ch00-02**：并发基本问题 — data race、race condition、死锁
- **ch00-03**：CPU cache 与 OS 线程 — cache line、false sharing
- **ch01-01**：std::thread 基础 — 创建、join/detach、hardware_concurrency
- **ch01-02**：线程参数与生命周期 — decay-copy、悬空引用、move-only
- **ch01-03**：线程所有权与 RAII — thread_guard、joining_thread、异常安全
- **ch01-04**：thread_local 与 call_once — 线程局部存储、一次性初始化

这个 Lab 没有前置 Lab 依赖。

## 环境准备

我们需要 C++17（因为要用 `<filesystem>`），一个还算现代的编译器，以及 Catch2 v3 来跑测试。具体的版本要求如下：

- **编译器**：GCC 12+ 或 Clang 15+（需要完整的 `<filesystem>` 支持），笔者当时设计的时候使用的是GCC 16.1，如果
- **CMake**：3.14+（FetchContent 需要）
- **Catch2**：v3.x，header-only 模式，通过 FetchContent 拉取

TSan 在这个 Lab 里是我们的主要诊断工具。每次实现完一个 milestone 之后，都应该在 TSan 下跑一遍测试，确认没有 data race。编译选项是 `-fsanitize=thread -g`。

下面是一个最小可用的 CMakeLists.txt：

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

测试文件的骨架长这样：

```cpp
// tests/main.cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Lab 0 sanity check", "[lab0]")
{
    REQUIRE(1 + 1 == 2);
}
```

编译和运行：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/lab0_tests
```

如果一切正常，你应该看到一个绿色的测试通过输出。

## 最终接口

在开始写代码之前，我们先明确最终产物的形状。不急着写实现，先看清楚目标。

### `FileInfo` — 单文件扫描结果

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::filesystem::path` | `path` | 文件完整路径 |
| `std::uintmax_t` | `file_size` | 文件大小（字节） |
| `std::string` | `extension` | 扩展名（含点号，如 `.cpp`） |

### `WorkerStats` — 单 worker 统计汇总（Milestone 4 用 `thread_local` 维护，主线程汇总）

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::size_t` | `files_scanned` | 已扫描文件数 |
| `std::uintmax_t` | `total_bytes` | 已扫描总字节数 |
| `std::unordered_map<std::string, std::size_t>` | `ext_counts` | 扩展名 → 出现次数 |

### `JoiningThread` — RAII 线程包装器（Milestone 2，move-only，不可复制）

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::thread` | `thread_` | 被管理的底层线程对象 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造（可调用对象） | `JoiningThread(Callable&&, Args&&...)` | 接受任意可调用对象和参数 | MS2 |
| 构造（接管 thread） | `JoiningThread(std::thread) noexcept` | 从 `std::thread` move 构造 | MS2 |
| move 构造/赋值 | `JoiningThread(JoiningThread&&)` | 转移线程所有权 | MS2 |
| 析构 | `~JoiningThread()` | 如果 `joinable()` 则自动 join | MS2 |
| join | `void join()` | 等待线程完成 | MS2 |
| joinable | `bool joinable() const noexcept` | 是否持有活跃线程 | MS2 |

### `FileScanner` — 文件扫描器

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::filesystem::path` | `root_path_` | 扫描的根目录 |
| `std::size_t` | `num_workers_` | worker 线程数量 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `FileScanner(path, size_t num_workers)` | 指定扫描目录和 worker 数量 | MS1 |
| scan | `WorkerStats scan()` | 启动扫描并返回汇总结果 | MS1–4 |

接下来我们按 milestone 拆解，一步一步实现。

## Milestone 1: 并行任务分发

### 目标

用 `std::thread` 启动固定数量的 worker，每个 worker 负责扫描一部分文件。主线程等待所有 worker 完成后，输出汇总信息。这个 milestone 先不追求完美——手工 `join()`、不用 RAII、用全局的 `std::atomic` 做简单统计就行。我们先把多线程的骨架搭起来。

### 为什么先做这一步

在整体设计中，这是最基本的一层：先把"多个线程同时工作"这件事跑通。后面的 milestone 会在这个基础上逐步改进——RAII 包装、参数安全、thread_local 统计，每一步只引入一个新的工程问题。如果一开始就追求完美的架构，很容易陷入"什么都还没跑起来就在纠结接口设计"的困境。

### 实现指引

整体思路分三步：先用 `std::filesystem::recursive_directory_iterator` 收集根目录下所有文件路径到一个 `std::vector`；然后按 worker 数量分片，每个 worker 拿到一段文件列表；最后创建 N 个 `std::thread`，每个线程遍历自己那份文件列表，统计文件数和总大小。

分片策略上，简单的等分就好——假设有 100 个文件、4 个 worker，那每个 worker 负责 25 个文件。最后一个 worker 可能会多拿到几个（因为除法不一定整除）。核心伪代码如下：

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

对于统计结果的收集，这个 milestone 先用最简单的方式——一组全局的 `std::atomic<std::size_t>` 来累计文件数和总字节数。每个 worker 扫描完一个文件就 `fetch_add` 一次。这种方式有性能开销（所有 worker 竞争同一个 atomic），但对于理解多线程的基本骨架来说已经足够了，后面 Milestone 4 会用 `thread_local` 替代它。

踩坑预警有几个地方。第一，`std::filesystem::recursive_directory_iterator` 本身不是线程安全的——不能多个线程同时递增同一个迭代器。所以收集文件路径这一步必须在主线程完成，worker 只负责处理已经收集好的路径列表。第二，传递给 `std::thread` 的参数会被 decay-copy——如果你传了一个 `std::vector<std::filesystem::path>` 的切片引用，它会被复制一份。对于这个 milestone 来说这完全可以接受，但后面的 milestone 我们要思考怎么避免不必要的拷贝。第三，如果你的测试目录里文件特别少（比如只有 3 个文件但你开了 8 个 worker），部分 worker 会拿到空列表——你的 `worker_function` 需要正确处理这种情况。

### 验证

下面是 Catch2 测试代码。先创建一些临时文件，然后验证扫描结果是否正确。

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

这两个测试覆盖了基本场景：正常情况下的文件收集和空目录的边界情况。用 TSan 跑一下确认没有 data race：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/lab0_tests "[lab0][milestone1]"
```

## Milestone 2: RAII 包装

### 目标

实现 `JoiningThread`——一个在析构时自动 `join()` 的 RAII 包装器。用 `JoiningThread` 替换 Milestone 1 中的裸 `std::thread`，然后验证异常路径下线程仍然被正确回收。

### 为什么

Milestone 1 的代码有一个很明显的工程问题：手工 `join()`。我们写了一个 `for` 循环来逐个 join 线程，看起来没什么问题——但如果在 join 循环之前的某个地方抛了异常呢？或者其中一个 `join()` 本身就抛了异常（虽然罕见但标准允许）？剩下的线程就成了无主线程，析构时 `std::terminate()`。ch01-03 已经讲过这个问题的根源和 RAII 的解决方案，这个 milestone 就是把它从"理解"推进到"实现并在实战中使用"。

### 实现指引

`JoiningThread` 的核心思路是接管 `std::thread` 的所有权，在析构函数里自动调用 `join()`。ch01-03 已经给出了完整的实现代码，所以这里不重复——但有几个关键设计点需要你自己想清楚：

第一，move 赋值运算符里，接收新线程之前必须先处理当前持有的线程。如果当前线程还是 `joinable()` 的，必须先 join 它，否则就是 UB。这个"先清理旧的再接手新的"的模式，跟 `std::unique_ptr` 的赋值运算符是一个道理。

第二，析构函数里 `join()` 可能抛异常（`std::system_error`）。在析构函数里抛异常会触发 `std::terminate()`。务实的做法是用 `try-catch` 包住，吞掉异常并记录日志。不要觉得"join 不可能失败"就跳过这一步——工业级代码的区别往往就体现在这些看似多余的防御上。

第三，构造函数要支持从 `std::thread` move 构造、从另一个 `JoiningThread` move 构造、以及直接接受可调用对象和参数。前两个是 move 语义，第三个是模板构造函数，需要用 `std::forward` 完美转发。

用 `JoiningThread` 改造 Milestone 1 的代码非常简单——把 `std::vector<std::thread>` 换成 `std::vector<JoiningThread>`，删掉手动 join 的循环，就完事了。当 `vector` 析构时，每个 `JoiningThread` 的析构函数会自动被调用。

### 验证

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

这组测试覆盖了四个关键场景：正常析构自动 join、异常路径下自动 join、move 语义转移所有权、以及在 `vector` 中使用 `JoiningThread`。特别关注第二个测试——它模拟了一个在创建线程之后、手动 join 之前就抛异常的场景。没有 RAII 的话，这种情况会直接导致 `std::terminate()`。

## Milestone 3: 参数生命周期修复

### 目标

审视 Milestone 1 中的参数传递方式，识别并修复所有可能的悬空引用和生命周期问题。具体来说，我们要把 lambda 捕获中的引用改成安全的值捕获或 move，确保线程不会访问已经销毁的变量。

### 为什么

ch01-02 讲过 `std::thread` 的 decay-copy 语义和引用悬空的风险，但在小例子中这些问题往往不会暴露——因为小例子里的变量生命周期恰好够长。在真实的并行文件扫描器中，情况会更复杂：主线程可能在 worker 还没跑完就开始清理临时数据了，或者 lambda 捕获了一个局部 `vector` 的引用。这类 bug 在开发时可能偶然不触发，但在生产环境的高并发压力下会以不可预测的方式出现。

### 实现指引

Milestone 1 的代码里，我们把文件路径列表按值传给了 `worker`——这实际上是安全的，因为 `std::thread` 的构造函数会对参数做 decay-copy，所以 worker 拿到的是路径列表的一份独立副本。但问题往往藏在更微妙的地方。考虑以下几种容易翻车的场景。

第一种：lambda 捕获了局部变量的引用。假设你把 `worker` 改成了这样：

```cpp
auto worker = [&all_files, start_idx, end_idx]() {
    for (size_t i = start_idx; i < end_idx; ++i) {
        process(all_files[i]);  // 引用捕获，有风险
    }
};
```

如果 `all_files` 在 worker 还在执行时被销毁或修改，这里就是悬空引用。在我们的代码里 `all_files` 的生命周期足够长（在 `main` 的栈上），但这种写法让正确性依赖于调用者对生命周期的隐式理解——不是个好习惯。

第二种：通过 `std::ref` 传递参数。如果你觉得复制整个 `vector` 太浪费，想用引用来避免拷贝：

```cpp
threads.emplace_back(worker, std::ref(chunk_files));
```

这把 `chunk_files` 的引用传给了线程。如果 `chunk_files` 是一个在循环体内声明的局部变量，而下一次循环迭代时它被修改了，前一个 worker 就会读到被修改的数据——这是 data race。修复方案是用值捕获（让 decay-copy 给每个 worker 一份独立的副本）或者用 `std::move` 把所有权转移给线程。

第三种：`this` 指针的隐式捕获。如果你把 `FileScanner` 做成了类，lambda 里用了成员变量，那么 `[this]` 的捕获就隐含了对 `FileScanner` 对象生命周期的依赖——如果 `FileScanner` 对象在 worker 还没跑完时被析构了，`this` 就悬空了。这个 bug 在 Lab 3（线程池）里特别容易踩到，因为线程池的生命周期往往比调用者预期的要长。

这个 milestone 的核心任务是：审查你 Milestone 1 和 2 的代码，找出所有引用捕获和 `std::ref` 的使用，判断它们是否安全。对于不安全的捕获，改成值捕获或 `std::move`。验证方式是 TSan——一个正确的实现在 TSan 下不应该有任何 data race 报告。

### 验证

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

跑一下 TSan 确认：

```bash
./build/lab0_tests "[lab0][milestone3]" --tsan
```

如果一切正常，TSan 不应该输出任何 data race 报告。

## Milestone 4: thread_local 统计与汇总

### 目标

把 Milestone 1 中全局 `std::atomic` 的统计方式替换为 `thread_local` 统计。每个 worker 维护自己的 `WorkerStats` 对象，扫描完毕后将结果汇总到主线程。

### 为什么

Milestone 1 用了一个全局的 `std::atomic<std::size_t>` 来累计统计——这种方式有两个问题。第一，所有 worker 竞争同一个 atomic 变量，造成不必要的缓存行失效（false sharing 的近亲）。第二，它只能统计简单的计数，一旦你想统计"每个扩展名各出现了多少次"这样的分布数据，全局 atomic 就不够用了——你不能用一个 atomic 来保护一个 `unordered_map`（除非加锁，但那又回到了 ch02 的范畴）。

`thread_local` 给了一种更干净的方案：每个 worker 线程有自己的 `WorkerStats` 实例，各算各的，完全无竞争。算完之后，主线程收集所有 worker 的结果做汇总。这个模式不仅是这个 Lab 的核心设计，也是后续 Lab 的基础——Lab 2 的 atomic metrics 和 Lab 3 的线程池都会用到类似的"线程本地统计 → 汇总"结构。

### 实现指引

核心思路是：在 `worker_function` 内部声明一个 `thread_local WorkerStats stats;`，每个 worker 在扫描过程中往自己的 `stats` 里累加数据，扫描结束后把 `stats` 通过某种方式返回给主线程。

返回统计结果的方式有几种选择。最简单的是让 `worker_function` 返回 `WorkerStats`，然后主线程通过 `std::future` 来收集。但 `std::future` 是 ch05 的内容，我们在这个 Lab 里不应该提前引入。所以更合适的做法是给每个 worker 一个指向输出区域的指针——主线程预先分配一个 `std::vector<WorkerStats>`，每个 worker 通过索引写入自己的位置。

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

这里有一个微妙的地方值得注意：`thread_local WorkerStats local_stats` 在多次调用同一个 worker 函数时会**复用**同一个实例。在我们的场景中每个 worker 只被调用一次，所以这不是问题。但如果你不小心让同一个线程多次进入 worker 函数，就需要在函数开头手动重置 `local_stats`。

汇总逻辑就很简单了——遍历 `results`，把所有 `WorkerStats` 加起来：

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

踩坑预警：`results[worker_id] = local_stats` 这行代码中，`worker_id` 必须是每个 worker 独有的，不能有重复。如果你用循环变量 `i` 的引用来传递 `worker_id`，而 lambda 捕获了 `i` 的引用——恭喜，你刚刚在 Milestone 3 修过的问题又回来了。用值捕获 `[&results, worker_id = i]` 来避免这个问题。

另一个要注意的是 `WorkerStats` 的拷贝开销。如果扩展名种类特别多，`ext_counts` 这个 `unordered_map` 的拷贝可能不便宜。对于这个 Lab 的规模来说完全不是问题，但如果你在写生产代码，可以考虑 `std::move(results[worker_id])` 来避免不必要的拷贝。

### 验证

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

用 TSan 跑全部测试，确认从 Milestone 1 到 4 都没有 data race：

```bash
./build/lab0_tests "[lab0]" --tsan
```

## 自查清单

在提交之前，逐项确认以下内容：

- [ ] Milestone 1 的测试全部通过——并行扫描不遗漏文件
- [ ] Milestone 2 的测试全部通过——`JoiningThread` 在正常路径和异常路径都能自动 join
- [ ] Milestone 3 的测试全部通过——无悬空引用，move-only 参数正确传递
- [ ] Milestone 4 的测试全部通过——`thread_local` 统计结果与单线程结果一致
- [ ] 全部测试在 TSan 下无 data race 报告
- [ ] 不存在 `joinable()` 为 true 的 `std::thread` 被析构的情况
- [ ] 没有使用 `detach()` 来逃避生命周期管理
- [ ] 能口头解释 `JoiningThread` 析构函数中 `try-catch` 的必要性
- [ ] 能解释 lambda 捕获 `[&]` vs `[=]` vs `[x = std::move(y)]` 在多线程场景下的区别
- [ ] 能解释 `thread_local` 统计模式相比全局 atomic 的两个优势（无竞争 + 支持复杂结构）
