---
title: "Lab 5: Channel or Actor Runtime"
description: "通过 Channel 或 Actor 模式实践消息传递并发，掌握 CSP、mailbox、select 和取消语义"
chapter: 10
order: 6
difficulty: advanced
tags:
  - host
  - cpp-modern
  - coroutine
  - advanced
cpp_standard: [20]
reading_time_minutes: 13
prerequisites:
  - "卷五 ch07: Actor 与 Channel"
  - "Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives"
  - "Lab 4: Coroutine Scheduler and Event Loop"
---

# Lab 5: Channel or Actor Runtime

## 目标

前面的 Lab 主要训练共享内存并发——多个线程通过 mutex、atomic 和条件变量来协调对共享数据的访问。这个 Lab 我们换一种思路：不让多个线程同时改同一份数据，而是通过 channel 或 mailbox 传递消息和所有权。数据跟着消息走，同一时刻只有一个线程/actor 拥有数据的访问权——从根本上消除 data race。

这个 Lab 提供两条路线，主线推荐 **Channel 路线**（测试更清晰、与 Lab 1 的队列有更多复用），Actor 路线适合想要挑战设计能力的同学作为扩展。

## 前置知识

在开始之前，确保你已经读完以下章节：

- **ch07-01**：Actor 模型与消息传递 — Actor 模型的基本概念和实现
- **ch07-02**：Channel 与 CSP 模型 — CSP 通信顺序进程、Go-style channel

## 环境准备

与 Lab 4 相同（C++20，Catch2 v3）。

## 路线选择

### Channel 路线（推荐）

实现 `Channel<T>`，支持 buffered channel、send/receive、close 语义和简化的 select。用 channel 实现 pipeline（parse → transform → write）。

### Actor 路线（扩展）

实现 `ActorSystem` 和 `ActorRef`，每个 actor 拥有自己的 mailbox，支持 spawn、send、stop。实现 ping-pong 或 chat room demo。

下面以 Channel 路线为主线展开。

## 最终接口（Channel 路线）

### `Channel<T>` — 消息通道

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::queue<T>` | `buffer_` | 缓冲区 |
| `mutable std::mutex` | `mutex_` | 保护内部状态 |
| `std::condition_variable` | `not_full_` | 发送者等待条件 |
| `std::condition_variable` | `not_empty_` | 接收者等待条件 |
| `std::size_t` | `capacity_` | 缓冲区容量（0 = 无缓冲/同步 channel） |
| `bool` | `closed_` | 关闭标志 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `Channel(size_t capacity = 1)` | 容量为 0 表示无缓冲同步 channel | MS1 |
| send | `bool send(T item)` | 阻塞发送；关闭后返回 false | MS1 |
| receive | `std::optional<T> receive()` | 阻塞接收；关闭且空时返回 nullopt | MS1 |
| try_send | `bool try_send(T item)` | 非阻塞发送；满或已关闭返回 false | MS2 |
| try_receive | `std::optional<T> try_receive()` | 非阻塞接收；空返回 nullopt | MS2 |
| close | `void close()` | 关闭 channel，唤醒所有等待线程 | MS1 |
| is_closed | `bool is_closed() const` | 查询关闭状态 | MS1 |
| len | `size_t len() const` | 缓冲区中元素数量 | MS1 |

### `channel_select` — 简化版 select（Milestone 3）

| 签名 | 说明 | Milestone |
|------|------|-----------|
| `optional<pair<size_t, T>> channel_select(vector<Channel<T>*>&)` | 从多个 channel 中选择一个 ready 的，返回 `(channel_index, value)` | MS3 |

## Milestone 1: Buffered Channel

### 目标

实现 `Channel<T>` 的 `send` 和 `receive`，支持带缓冲的消息传递。close 语义与 `BoundedBlockingQueue` 类似。

### 为什么

Channel 是 CSP（Communicating Sequential Processes）模型的核心抽象。它看起来跟 `BoundedBlockingQueue` 很像——线程安全的阻塞队列——但在概念上有重要区别：Channel 代表的是"通信端点"，而不仅仅是数据结构。这个区别在后面的 select 和 pipeline 中会体现出来。

### 实现指引

好消息是，`Channel<T>` 的底层实现跟 Lab 1 的 `BoundedBlockingQueue<T>` 几乎一模一样——mutex + 两个 condition_variable + 关闭标志。如果你 Lab 1 的实现是正确的，这个 milestone 主要是"换个名字和接口"。

一个微妙的不同是"无缓冲 channel"（capacity = 0）的概念。无缓冲 channel 的 send 和 receive 必须同时就绪才能完成——发送者阻塞直到有接收者，接收者阻塞直到有发送者。这实现了"同步握手"的语义。实现上，你可以把无缓冲 channel 当作容量为 0 的队列——send 发现 capacity_ 为 0 就直接进入等待，直到有 receive 唤醒它。

### 验证

```cpp
TEST_CASE("Milestone 1: channel send and receive",
          "[lab5][milestone1]")
{
    Channel<int> ch(10);

    JoiningThread producer([&]() {
        for (int i = 0; i < 100; ++i) {
            ch.send(i);
        }
        ch.close();
    });

    std::vector<int> received;
    while (auto val = ch.receive()) {
        received.push_back(*val);
    }

    REQUIRE(received.size() == 100);
    REQUIRE(received[0] == 0);
    REQUIRE(received[99] == 99);
}

TEST_CASE("Milestone 1: unbuffered channel blocks until paired",
          "[lab5][milestone1]")
{
    Channel<int> ch(0);  // 无缓冲
    std::atomic<int> value{0};
    std::atomic<bool> sent{false};

    JoiningThread sender([&]() {
        ch.send(42);
        sent.store(true);
    });

    // 等一小段时间，确认 send 阻塞了
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(sent.load());

    // receive 配对后 send 才完成
    auto val = ch.receive();
    REQUIRE(val.has_value());
    REQUIRE(*val == 42);
}

TEST_CASE("Milestone 1: close semantics",
          "[lab5][milestone1]")
{
    Channel<int> ch(5);
    ch.send(1);
    ch.send(2);
    ch.close();

    REQUIRE_FALSE(ch.send(3));     // 关闭后不能 send
    REQUIRE(ch.receive() == 1);    // 已有数据仍可 receive
    REQUIRE(ch.receive() == 2);
    REQUIRE(ch.receive() == std::nullopt);  // 耗尽后 nullopt
}
```

## Milestone 2: try_send、try_receive 与非阻塞操作

### 目标

实现 `try_send` 和 `try_receive`——非阻塞版本，立即返回成功或失败。

### 为什么

阻塞的 send/receive 在很多场景下太重了——你可能只想"如果有数据就取，没有就做别的事"。非阻塞操作让调用者有机会在没有数据时采取其他策略，而不是被动等待。后面 select 的实现也会用到 try_receive。

### 实现指引

`try_send` 就是加锁后检查缓冲区是否满——满了返回 false，不满就塞进去并 notify。`try_receive` 检查缓冲区是否空——空了返回 nullopt，不空就取出并 notify。

```cpp
bool try_send(T item) {
    lock_guard lock(mutex_);
    if (closed_ || buffer_.size() >= capacity_) return false;
    buffer_.push(move(item));
    not_empty_.notify_one();
    return true;
}
```

### 验证

```cpp
TEST_CASE("Milestone 2: try_send and try_receive",
          "[lab5][milestone2]")
{
    Channel<int> ch(2);

    REQUIRE(ch.try_send(1));
    REQUIRE(ch.try_send(2));
    REQUIRE_FALSE(ch.try_send(3));  // 满了

    REQUIRE(ch.try_receive() == 1);
    REQUIRE(ch.try_receive() == 2);
    REQUIRE(ch.try_receive() == std::nullopt);  // 空了
}

TEST_CASE("Milestone 2: try operations on empty channel",
          "[lab5][milestone2]")
{
    Channel<int> ch(5);
    REQUIRE(ch.try_receive() == std::nullopt);
    REQUIRE(ch.try_send(42));
    REQUIRE(ch.try_receive() == 42);
}
```

## Milestone 3: 简化版 select

### 目标

实现 `channel_select`，从多个 channel 中选择一个有数据可读的，返回 `(channel_index, value)`。如果所有 channel 都为空，阻塞等待。

### 为什么

select 是 CSP 模型中最强大的组合原语——它让一个协程/线程同时等待多个事件源，哪个先就绪就处理哪个。Go 的 `select` 语句是最著名的实现。在 C++ 中我们没有语言级的 select，但可以用轮询 + condition_variable 来模拟。

### 实现指引

最简单的实现是轮询：遍历所有 channel，对每个 channel 调用 `try_receive`。如果有一个成功了就返回。如果全部都空，就 `sleep` 一小段时间再重试。

更高效的实现是为每个 channel 注册一个回调——当 channel 有新数据时唤醒 select。但这需要在 Channel 中增加通知机制，复杂度较高。本 Lab 建议先用轮询实现，确认功能正确后再考虑优化。

```cpp

optional<pair<size_t, T>> channel_select(
    vector<Channel<T>*>& channels)
{
    while (true) {
        for (size_t i = 0; i < channels.size(); ++i) {
            auto val = channels[i]->try_receive();
            if (val) return make_pair(i, move(*val));
        }
        // 检查是否所有 channel 都关闭了
        bool all_closed = true;
        for (auto* ch : channels) {
            if (!ch->is_closed()) all_closed = false;
        }
        if (all_closed) return nullopt;

        // 短暂等待后重试
        this_thread::sleep_for(milliseconds(1));
    }
}

```

踩坑预警：轮询实现的 CPU 利用率不好——在没有数据时仍然占用 CPU。生产级实现应该用 condition_variable 或者 epoll 来实现真正的等待-唤醒。但对于教学目的，轮询足够展示 select 的语义。

### 验证

```cpp
TEST_CASE("Milestone 3: select picks ready channel",
          "[lab5][milestone3]")
{
    Channel<int> ch1(5);
    Channel<int> ch2(5);

    ch2.send(42);  // 只有 ch2 有数据

    std::vector<Channel<int>*> channels = {&ch1, &ch2};
    auto result = channel_select(channels);

    REQUIRE(result.has_value());
    REQUIRE(result->first == 1);   // ch2 的索引
    REQUIRE(result->second == 42);
}

TEST_CASE("Milestone 3: select blocks until data available",
          "[lab5][milestone3]")
{
    Channel<int> ch1(5);
    Channel<int> ch2(5);

    std::vector<Channel<int>*> channels = {&ch1, &ch2};

    JoiningThread producer([&]() {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));
        ch1.send(99);
    });

    auto result = channel_select(channels);
    REQUIRE(result.has_value());
    REQUIRE(result->first == 0);
    REQUIRE(result->second == 99);
}

TEST_CASE("Milestone 3: select returns nullopt when all closed",
          "[lab5][milestone3]")
{
    Channel<int> ch1(5);
    Channel<int> ch2(5);
    ch1.close();
    ch2.close();

    std::vector<Channel<int>*> channels = {&ch1, &ch2};
    auto result = channel_select(channels);
    REQUIRE_FALSE(result.has_value());
}
```

## Milestone 4: Pipeline 模式

### 目标

用 channel 实现 pipeline：parse → transform → write。每个阶段是一个独立的线程/协程，通过 channel 传递数据。

### 为什么

Pipeline 是 channel 最经典的应用场景。它把一个复杂的处理流程拆分成多个独立的阶段，每个阶段只负责一件事，阶段之间通过 channel 连接。这种设计的优势是：每个阶段可以独立调整并发度（parse 可以是单线程，transform 可以多线程），阶段之间的速率差异通过 channel 的缓冲自然吸收（背压）。

### 实现指引

一个简单的 pipeline 有三个阶段和两个 channel：

```cpp
Channel<string> raw_data(16);     // parse 输出
Channel<string> transformed(16);  // transform 输出

// Stage 1: parse — 从数据源读取原始数据，解析后发给 raw_data
// Stage 2: transform — 从 raw_data 读取，转换后发给 transformed
// Stage 3: write — 从 transformed 读取，写入目标

// 每个 stage 是一个独立的线程函数
void parse_stage(Channel<string>& output) {
    for (...) {
        output.send(parsed_item);
    }
    output.close();
}

void transform_stage(Channel<string>& input,
                     Channel<string>& output) {
    while (auto val = input.receive()) {
        output.send(transform(*val));
    }
    output.close();
}

void write_stage(Channel<string>& input) {
    while (auto val = input.receive()) {
        write(*val);
    }
}
```

踩坑预警：pipeline 的关闭顺序很重要。上游 stage 必须在处理完所有数据后 `close()` 它的输出 channel，这样下游 stage 才能在 `receive` 返回 `nullopt` 后自然退出。如果你忘了 `close()`，下游 stage 就会永远阻塞。

### 验证

```cpp
TEST_CASE("Milestone 4: three-stage pipeline processes data",
          "[lab5][milestone4]")
{
    Channel<int> stage1_out(8);
    Channel<std::string> stage2_out(8);

    // Stage 1: 生成数字并翻倍
    JoiningThread s1([&]() {
        for (int i = 1; i <= 20; ++i) {
            stage1_out.send(i * 2);
        }
        stage1_out.close();
    });

    // Stage 2: 转成字符串
    JoiningThread s2([&]() {
        while (auto val = stage1_out.receive()) {
            stage2_out.send("item_" + std::to_string(*val));
        }
        stage2_out.close();
    });

    // Stage 3: 收集结果
    std::vector<std::string> results;
    while (auto val = stage2_out.receive()) {
        results.push_back(*val);
    }

    REQUIRE(results.size() == 20);
    REQUIRE(results[0] == "item_2");
    REQUIRE(results[19] == "item_40");
}
```

## 自查清单

- [ ] Channel 的 send/receive 使用谓词等待
- [ ] close 语义正确：关闭后不能 send，已有数据可 receive
- [ ] 无缓冲 channel 正确实现同步握手
- [ ] try_send/try_receive 的非阻塞行为正确
- [ ] select 能从多个 channel 中选择就绪的一个
- [ ] select 在所有 channel 关闭后返回 nullopt
- [ ] Pipeline 的关闭顺序正确，不会死锁
- [ ] 全部测试在 TSan 下无 data race 报告
- [ ] 能解释 Channel 与 mutex 方案相比的优势（消息传递消除共享状态）和代价（数据拷贝或 move 的开销）
- [ ] 能说明 Channel 的 close 语义与 Lab 1 BoundedBlockingQueue 的 close 语义的异同
- [ ] 如果选做了 Actor 路线，能对比 Channel 和 Actor 的设计取舍
