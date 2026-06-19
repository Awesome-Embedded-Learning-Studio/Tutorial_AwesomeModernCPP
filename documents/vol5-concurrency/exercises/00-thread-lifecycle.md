---
title: "Lab 0: Thread Lifecycle Lab"
description: "通过并行文件扫描器，训练线程创建、RAII 包装、参数生命周期和线程局部统计的实战能力"
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
  - "卷五 ch00: 并发思维与基础"
  - "卷五 ch01: 线程生命周期与 RAII"
related:
  - "并发基本问题"
  - "std::thread 基础"
  - "线程所有权与 RAII"
---

# Lab 0: Thread Lifecycle Lab

> 本 Lab 配套可运行工程在 [`code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/)。动手工作量约 **4–6 小时**（`reading_time_minutes` 是纯阅读分钟数，不是动手时间）。

## 目标

读完了 ch01 的四篇文章，我们现在已经知道 `std::thread` 怎么创建、参数怎么传、`JoiningThread` 怎么写、`thread_local` 怎么用。但"知道"和"写过"之间的距离，说实话，比很多朋友想象的要大。一个很典型的经历是：你看了 RAII 包装的代码觉得"这我懂了"，然后自己写一个多线程程序，一跑 TSan 就发现 data race 满天飞，或者某个异常路径把线程给忘了。

这个 Lab 的目标很直白：我们要写一个**并行文件扫描器**——主线程把一个目录下的文件分片，分发给 N 个 worker 线程去扫描，每个 worker 统计自己负责的文件信息（大小、扩展名分布），最后主线程汇总。项目不大，但它会逼你直面四个核心问题：怎么创建和管理多个线程、怎么用 RAII 保证异常路径不泄漏线程、怎么安全地给线程传参数、怎么用线程局部统计做无竞争的汇总。

完成这个 Lab 后，你应该能拿出一套可以复用的 `JoiningThread` 包装器和"每 worker 局部统计 + 主线程汇总"的模式，在后续的 Lab 里直接拿来用。

## 前置知识

开始前确保你读完以下章节：

- **ch00-01** 为什么需要并发 — 并发 vs 并行、Amdahl 定律
- **ch00-02** 并发基本问题 — data race、race condition、死锁
- **ch00-03** CPU cache 与 OS 线程 — cache line、false sharing
- **ch01-01** std::thread 基础 — 创建、join/detach、hardware_concurrency
- **ch01-02** 线程参数与生命周期 — decay-copy、悬空引用、move-only
- **ch01-03** 线程所有权与 RAII — thread_guard、joining_thread、异常安全
- **ch01-04** thread_local 与 call_once — 线程局部存储

这个 Lab 没有前置 Lab 依赖。

## 工程脚手架（先把这个跑起来）

这一节是本 Lab 和旧版最大的不同：**我们不在文章里贴一堆零散的代码片段让你自己拼**，而是给你一个能直接构建的工程。所有测试已经写好，你只需要补全实现。

每个 Lab 在 [vol5-labs/] 下有两份：**`templates/lab0_thread_lifecycle/`** 是空实现骨架（你拷贝去做），**`examples/lab0_thread_lifecycle/`** 是参考实现（卡住可对照，别先抄）。两份都是 standalone 工程。你要做的是 templates 那份，结构如下：

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

整个 `vol5-labs/` 目录的构建说明和 dogfooding 反馈流程见 [`vol5-labs/README.md`](../../../code/volumn_codes/vol5-labs/README.md)。先把它读一遍。

第一次构建（需要联网，FetchContent 会拉取 Catch2 v3）：

```bash
cd code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # Debug 默认开 ThreadSanitizer
cmake --build build
```

**预期：构建会停在链接阶段，报 `undefined reference to lab0::FileScanner::scan()`** —— 这是故意的。`file_scanner.h` 和 `joining_thread.h` 现在只有声明没有实现，链接器在提醒你"该动手了"。这就是 TDD 式练习的起点：测试已经写好等着你，你一步步把实现补上，对应 milestone 的测试就会从红变绿。

> 为什么用 Debug 配置？因为并发代码的正确性不能靠"跑通了就行"——TSan 是我们的主诊断工具，它在 Debug 构建里通过 `-fsanitize=thread` 自动开启。注意：**Catch2 没有 `--tsan` 这种运行参数**，TSan 是编译期开的，直接运行测试就在 TSan 下。想跑单个 milestone：

```bash
./build/test/test_milestone1                          # 跑 milestone 1
./build/test/test_milestone2 "[lab0][milestone2]"     # Catch2 标签过滤
ctest --test-dir build --output-on-failure                                  # 跑全部
```

## 最终接口

动手前先把目标形状看清楚。这些接口和工程里 `include/lab0/` 的头文件完全一致——你可以随时打开头文件对照。

### `FileInfo` — 单文件扫描结果（已提供，数据结构）

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::filesystem::path` | `path` | 文件完整路径 |
| `std::uintmax_t` | `file_size` | 文件大小（字节） |
| `std::string` | `extension` | 扩展名（含点号，如 `.cpp`） |

### `WorkerStats` — 单 worker 统计汇总（已提供，数据结构）

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::size_t` | `files_scanned` | 已扫描文件数 |
| `std::uintmax_t` | `total_bytes` | 已扫描总字节数 |
| `std::unordered_map<std::string, std::size_t>` | `ext_counts` | 扩展名 → 出现次数 |

`worker_stats.h` 还提供了 `operator+=`，主线程汇总各 worker 结果时直接用。

### `JoiningThread` — RAII 线程包装器（Milestone 2 你来实现）

move-only，不可复制。接口（见 `include/lab0/joining_thread.h`）：

| 方法 | 签名 | Milestone |
|------|------|-----------|
| 模板构造 | `JoiningThread(Callable&&, Args&&...)` | MS2（已给实现） |
| 接管 thread | `JoiningThread(std::thread) noexcept` | MS2 |
| move 构造/赋值 | `JoiningThread(JoiningThread&&)` / `operator=(JoiningThread&&)` | MS2 |
| 析构 | `~JoiningThread()` — joinable 则 join | MS2 |
| join / joinable | `void join()` / `bool joinable() const noexcept` | MS2 |

### `FileScanner` — 文件扫描器（主载体，Milestone 1/3/4 演进）

| 方法 | 签名 | Milestone |
|------|------|-----------|
| 构造 | `FileScanner(path root, size_t num_workers)` | MS1 |
| scan | `WorkerStats scan()` | MS1→MS4（接口不变，内部实现逐步替换） |

接下来按 milestone 拆解，一步一步实现。

## Milestone 1: 并行任务分发

### 目标

实现 `FileScanner::scan()` 的第一版：用裸 `std::thread` 启动固定数量 worker，每个 worker 扫描一段文件，用一组全局 `std::atomic` 累计文件数和总字节数。先把"多个线程同时工作"这件事跑通，不追求完美。

### 为什么先做这一步

这是最基本的一层。后面的 milestone 在这个基础上逐步改进——RAII 包装、参数安全、线程局部统计，每一步只引入一个新的工程问题。如果一开始就追求完美架构，很容易陷入"什么都还没跑起来就在纠结接口设计"的困境。

### 实现指引

整体思路分四步：

1. 用 `std::filesystem::recursive_directory_iterator` 在**主线程**收集所有 `regular_file` 路径到一个 `std::vector`；
2. 按 worker 数量等分（最后一个 worker 兜底拿余数）；
3. 创建 N 个 `std::thread`，每个线程遍历自己那段，统计文件数和总大小；
4. 手工 `join()` 所有线程，返回汇总结果。

第 1 步那个 `recursive_directory_iterator` 名字里的 **"recursive" 是关键**：它会**深度优先递归进入所有子目录**，所以你收到的是 `root` 整棵目录树里的普通文件，不是只有当前目录一层。`is_regular_file()` 只负责把遍历到的条目里"子目录、符号链接、特殊文件"过滤掉，它跟递不递归没关系——递归是 **iterator 的属性**。想只扫顶层目录、不进子目录，得换成 `std::filesystem::directory_iterator`（没有 `recursive_` 前缀）。另外 `recursive_directory_iterator` 默认 `directory_options::none`，**不跟随指向目录的符号链接**，只递归真实子目录——本 Lab 要扫整棵树，用 recursive、保持默认即可。

伪代码：

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

统计先用最简单的全局 `std::atomic<std::size_t>` 和 `std::atomic<std::uintmax_t>`，每个 worker 扫到一个文件就 `fetch_add`。这种方式有竞争开销（所有 worker 抢同一个 atomic），但对跑通骨架足够了，Milestone 4 会换掉它。

> **踩坑预警**：`recursive_directory_iterator` **不是线程安全的**——不能多个线程同时递增同一个迭代器。所以收集路径这步必须在主线程做完，worker 只处理已经收集好的 `vector`。另外传给 `std::thread` 的参数会被 decay-copy，按值传 `vector` 切片是安全的（worker 拿到独立副本）；这个 milestone 这么做完全 OK，Milestone 3 我们再细究捕获方式。还有：如果测试目录文件特别少（比如 3 个文件开了 8 个 worker），部分 worker 会拿到空列表——你的 worker 函数要正确处理空输入。

### 验证

对应测试在 [`test/test_milestone1.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone1.cpp)，覆盖三个场景：扫描收集到全部文件、空目录不崩溃、总字节数正确。关键断言：

```cpp
TEST_CASE("MS1: scan collects all files", "[lab0][milestone1]") {
    // ... 创建 20 个测试文件 ...
    lab0::FileScanner scanner(dir, 4);
    lab0::WorkerStats stats = scanner.scan();
    REQUIRE(stats.files_scanned == 20);
}
```

补全 `scan()` 的 MS1 实现后，跑：

```bash
./build/test/test_milestone1
```

测试变绿即通过。**记得用 TSan 跑一遍**（Debug 构建直接跑就是 TSan 下），确认没有 data race。

## Milestone 2: RAII 包装

### 目标

实现 `JoiningThread`——一个析构时自动 `join()` 的 RAII 包装器。然后用它替换 Milestone 1 里 `scan()` 的裸 `std::thread`，删掉手工 join 循环，验证异常路径下线程仍被正确回收。

### 为什么

Milestone 1 的手工 `join()` 有个明显问题：如果在 join 循环之前某处抛了异常，剩下的线程就成了无主线程，析构时 `std::terminate()`。ch01-03 讲过这个根源和 RAII 的解法，这个 milestone 把它从"理解"推进到"实现并实战使用"。

### 实现指引

`JoiningThread` 的核心是接管 `std::thread` 所有权，析构里自动 `join()`。模板构造（接受任意 Callable + 参数）工程里已经给你了（用 `std::forward` 完美转发），你来实现其余成员。有三个设计点必须想清楚：

**第一，move 赋值里，接收新线程前必须先处理当前持有的线程。** 如果当前 `thread_` 还 `joinable()`，必须先 join 它，否则旧线程被覆盖丢弃、析构时 `std::terminate`。这个"先清理旧的再接手新的"的模式，和 `std::unique_ptr` 的赋值是一个道理。

**第二，析构里的 `join()` 可能抛 `std::system_error`。** 在析构函数里抛异常会触发 `std::terminate`。务实的做法是 `try/catch` 包住、吞掉异常。别觉得"join 不可能失败"就跳过——工业级代码的区别往往就体现在这些看似多余的防御上。

**第三，`joinable()` 直接返回 `thread_.joinable()`。**

> **关于头文件内定义**：`JoiningThread` 不是模板类（只有构造函数是模板），所以其余成员可以类内定义（隐式 `inline`，多个翻译单元 include 不会重复定义）。直接在 `joining_thread.h` 的类体内把声明改成定义 `{ ... }` 即可，不需要单独的 `.cpp`。

实现完 `JoiningThread` 后，回到 `file_scanner.h` 把 `scan()` 里的 `std::vector<std::thread>` 换成 `std::vector<lab0::JoiningThread>`，删掉手工 join 循环——`vector` 析构时每个 `JoiningThread` 自动 join。

### 验证

> **别被测试骗了**：`test_milestone2` 只测 `JoiningThread` 类本身（和 `FileScanner` 解耦），**不检查 `scan()` 有没有真的用它**。所以哪怕你实现了 `JoiningThread`、测试全绿，但 `scan()` 里还是裸 `std::thread` + 手工 `join()` 循环——这个 milestone 就没真正完成。**真正的验收标准：`scan()` 里看不到手工 `join()` 循环，线程容器是 `std::vector<lab0::JoiningThread>`。**

[`test/test_milestone2.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone2.cpp) 只测 `JoiningThread` 本身（和 `FileScanner` 解耦），覆盖四个场景：作用域结束自动 join、异常路径仍 join 全部 worker、move 转移所有权、`vector` 析构 join 全部。重点看异常路径这个：

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

没有 RAII 的话，这种场景会直接 `std::terminate`。

## Milestone 3: 参数生命周期修复

### 目标

审视 `scan()` 里的参数传递方式，识别并修复所有可能的悬空引用和生命周期问题。核心是：确保每个 worker 拿到独立的文件列表副本（值捕获或 move），不捕获可能悬空的引用。

### 为什么

ch01-02 讲过 `std::thread` 的 decay-copy 语义和引用悬空风险，但小例子里这些问题往往不暴露——因为变量生命周期恰好够长。真实扫描器里情况更复杂：主线程可能在 worker 没跑完就开始清理临时数据，或者 lambda 捕获了局部 `vector` 的引用。这类 bug 开发时可能偶然不触发，高并发压力下才以不可预测的方式出现。

### 实现指引

MS1 我们把文件路径列表按值传给 worker——这其实已经是安全的（decay-copy 给了独立副本）。但问题藏在更微妙的地方，有三种容易翻车的写法你要会识别：

**引用捕获局部变量**。如果你贪图省事写 `[&all_files, start, end]`，一旦 `all_files` 在 worker 执行期间被销毁或修改就是悬空引用。在本 Lab 里 `all_files` 生命周期够长，但这种写法让正确性依赖调用者对生命周期的隐式理解——不是好习惯。

**用 `std::ref` 传参**。如果想避免拷贝用引用：`threads.emplace_back(worker, std::ref(chunk_files))`。如果 `chunk_files` 是循环体内的局部变量、下一轮迭代被改了，前一个 worker 就读到被改的数据——data race。修法是值捕获或 `std::move`。

**`this` 隐式捕获**。如果你把扫描逻辑放进 `FileScanner` 的成员函数、lambda 里用了成员变量，`[this]` 就隐含了对 `FileScanner` 对象生命周期的依赖。这个坑在 Lab 3（线程池）特别容易踩——线程池生命周期往往比调用者预期的长。

> **修法很简单**：worker 的文件列表用值捕获或 `std::move`（init-capture `files = std::move(worker_files)`），`worker_id` 用值捕获 `[worker_id = i]`。然后用 TSan 跑——正确实现下 TSan 不该有任何 data race 报告。

### 验证

[`test/test_milestone3.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone3.cpp) 验证：非整除分片也覆盖所有文件（30 文件 / 8 worker）、素数文件数（17 文件）任何分片都不丢、move-only 类型（`unique_ptr`）能安全传入线程。比如素数那个：

```cpp
TEST_CASE("MS3: prime file count covered by any worker count", "[lab0][milestone3]") {
    // ... 创建 17 个文件（素数，任何分片都非整除）...
    lab0::FileScanner scanner(dir, 4);
    REQUIRE(scanner.scan().files_scanned == 17);   // 一个都不能丢
}
```

如果你的分片逻辑在 `start..end` 边界上算错，素数文件数最容易暴露。

## Milestone 4: 线程局部统计与汇总

### 目标

把 Milestone 1 的全局 `std::atomic` 统计换成"每 worker 一个局部 `WorkerStats`、写回预分配的结果槽位、主线程汇总"。消除全局 atomic 的竞争，并支持扩展名分布这种复杂数据。

### 关于 `thread_local`：先想清楚再决定用不用

这里有个容易绕进去的点。很多朋友一看到"线程局部统计"就条件反射地写 `thread_local WorkerStats local;`——但**在本 Lab 的场景下，普通局部变量 `WorkerStats local;` 和 `thread_local` 行为完全等价**，因为每个 worker 只执行一次。

`thread_local` 的真正价值在于：**同一个线程多次进入同一个函数时，状态会被复用和累积**。比如一个线程池里的 worker 线程反复从队列取任务执行，每次执行都想往同一份统计里累加——这时候 `thread_local` 才有意义。本 Lab 每 worker 一次性扫描，用普通局部变量就够了，代码也更简单。

所以 milestone 的要求不是"必须用 `thread_local` 关键字"，而是：**统计正确、TSan 干净**。你用普通局部变量实现完全没问题。想清楚这两者的区别，比硬背关键字重要得多。

### 实现指引

核心思路：主线程预分配 `std::vector<WorkerStats> results(num_workers)`，每个 worker 把自己的局部统计 `results[worker_id] = local;` 写回对应槽位（不同 worker 写不同槽位，无竞争），最后主线程遍历 `results` 汇总：

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

> **踩坑预警（这个坑真的会咬人）**：注意上面那个 `{ }` 作用域——它不是装饰。`workers` 的析构（也就是 `join`）发生在**作用域结束**时；而汇总循环 `for (s : results)` 在作用域**之后**执行。如果你图省事把 `workers` 和汇总写在同一层（让 `workers` 等函数返回时才析构），那汇总读 `results` 的时候 worker 可能还在写——**data race**。
>
> 这个坑不是我编的：写这本手册时，我用一个"看起来对"的实现（断言全过）跑 TSan，直接被它抓出来——主线程在 join 之前读 `results`，`operator+=` 那行报 data race。教训很硬：**汇总结果前，必须确保所有 worker 已经 join**。用 `{ }` 把 `workers` 的生命周期限制在汇总之前，是最干净的写法。别依赖"函数返回时自然析构"——那时候汇总早就读完了。

另一个小点：`results[worker_id]` 里的 `worker_id` 必须每 worker 独有、用值捕获 `[worker_id = i]`，别用 `i` 的引用（你在 Milestone 3 刚修过的问题别让它回来）。

### 验证

> **别被测试骗了**：`test_milestone4` 只验结果数值对不对（和单线程一致），**不检查统计是不是真的"每 worker 局部"**。所以哪怕测试全绿，但 `scan()` 里还在用共享 `mutex`/`atomic` 统计——你其实停在 MS1，这个 milestone 没真正完成。**真正的验收标准：`scan()` 里没有锁、没有共享 atomic，统计走 `results[worker_id]` 独立槽位 + 主线程汇总。**

[`test/test_milestone4.cpp`](../../../code/volumn_codes/vol5-labs/templates/lab0_thread_lifecycle/test/test_milestone4.cpp) 验证：多线程扫描结果与单线程逐一扫描**完全一致**（文件数、字节数、扩展名分布三项都对），外加一个 200 文件 / 8 worker 的压力测试。关键断言：

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

压力测试在 TSan 下跑，应该零报告。如果你踩了上面那个 join 时机的坑，这个压力测试的 TSan 输出会明确指向 `worker_stats.h` 的 `operator+=`——看到那个就回去检查汇总前有没有 join。

## 自查清单

提交前逐项确认：

- [ ] Milestone 1 测试通过——并行扫描不遗漏文件、空目录不崩、字节数正确
- [ ] Milestone 2 测试通过——`JoiningThread` 正常路径和异常路径都能自动 join，move 语义正确
- [ ] Milestone 3 测试通过——素数/非整除分片不丢文件，move-only 参数安全传递
- [ ] Milestone 4 测试通过——多线程统计与单线程完全一致（含扩展名分布）
- [ ] **MS2 真验收**：`scan()` 里用的是 `std::vector<lab0::JoiningThread>`，没有手工 `join()` 循环（不只是 `test_milestone2` 绿——那个测试不查 `scan`）
- [ ] **MS4 真验收**：`scan()` 里没有锁/共享 atomic，统计走 `results[worker_id]` 独立槽位 + 主线程汇总（不只是 `test_milestone4` 绿——那个测试不查实现）
- [ ] **全部测试在 TSan 下无 data race 报告**（Debug 构建直接跑）
- [ ] 不存在 `joinable()` 为 true 的 `std::thread` 被析构的情况
- [ ] 没有用 `detach()` 逃避生命周期管理
- [ ] 汇总 worker 结果前，已确保所有 worker join（用 `{ }` 作用域，别依赖函数返回析构）
- [ ] 能口头解释 `JoiningThread` 析构里 `try/catch` 的必要性
- [ ] 能解释 `[&]` vs `[=]` vs `[x = std::move(y)]` 在多线程下的区别
- [ ] 能解释"每 worker 局部统计 + 汇总"相比全局 atomic 的两个优势（无竞争 + 支持复杂数据结构）
- [ ] 能解释 `thread_local` 在本场景与"worker 反复取任务"场景下的差异

## 扩展（bonus）

主线完成后，可选挑战：

- 把扫描结果按扩展名排序输出，练一下对 `unordered_map` 的遍历和排序
- 加一个 `--recursive=false` 选项，只扫顶层目录（不递归），练接口设计
- 用 `std::jthread` + `stop_token` 改造 `JoiningThread`，体会 C++20 的协作式取消（这是 ch05 的预告）

这些都不在测试覆盖范围内，做出来你自己爽就行。

## 参考资源

- [std::thread — cppreference](https://en.cppreference.com/w/cpp/thread/thread)
- [ThreadSanitizer — Clang 文档](https://clang.llvm.org/docs/ThreadSanitizer.html)
- [`std::filesystem::recursive_directory_iterator` — cppreference](https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator)
