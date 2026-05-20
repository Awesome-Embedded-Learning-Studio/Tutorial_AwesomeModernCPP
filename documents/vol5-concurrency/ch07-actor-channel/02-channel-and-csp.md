---
title: "Channel 与 CSP 模型"
description: "理解 CSP（Communicating Sequential Processes）并发模型，用 C++ 实现类 Go channel 的通信管道"
chapter: 7
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - 进阶
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard: [17, 20]
prerequisites:
  - "Actor 模型与消息传递"
  - "线程安全队列"
related:
  - "协程 Echo Server 实战"
---

# Channel 与 CSP 模型

上一篇我们聊了 Actor 模型——用有身份的 Actor + 异步消息传递来组织并发。这一篇我们看另一个同样主张"不共享内存"的流派：CSP（Communicating Sequential Processes，通信顺序进程）。

CSP 由 Tony Hoare 在 1978 年的论文 *"Communicating Sequential Processes"*（发表在 Communications of the ACM 上）中首次提出。和 Actor 模型一样，CSP 的核心思想也是用消息传递替代共享内存，但它走了一条不同的路：Actor 有身份、有邮箱，消息发送到特定的 Actor 地址；CSP 则是通过匿名的 channel 来通信，进程本身不需要知道对方是谁。这个区别看起来微妙，但在编程风格和表达能力上产生了很大的差异。Go 语言的 goroutine + channel 就是 CSP 在工业界最成功的实践，Rob Pike 的那句名言——"Don't communicate by sharing memory; share memory by communicating"——就是 Go 对 CSP 哲学的总结。

这一篇我们从 CSP 的理论基础开始，然后用 C++ 实现一个类 Go channel 的通信管道，包括有缓冲/无缓冲 channel、close 语义、select 模式，最后讨论什么时候用 channel、什么时候直接用锁。

## 环境说明

和上一篇一样，我们所有的代码基于 C++17，在 GCC 12+ / Clang 15+ / MSVC 2022+ 下编译通过，编译选项是 `-std=c++17 -pthread -O2`。Linux、macOS、Windows 都可以跑，只要你的标准库支持 `<thread>`、`<mutex>`、`<condition_variable>` 就行。整篇代码不依赖任何第三方库。

## CSP 的理论基础

CSP 的原始论文比 Actor 模型晚五年（1978 vs 1973），但它的影响同样深远。Hoare 最初的设计是一个并发编程语言（而不是像后来那样的形式化演算），语法长这样：

```text
COPY = *[c:character; west?c -> east!c]
```

这段代码的意思是：反复从名为 `west` 的进程接收一个字符 `c`，然后发送给名为 `east` 的进程。原始 CSP 的通信是基于进程名的同步消息传递——发送方和接收方必须同时就绪，通信才能发生。

后来（1984-1985），Hoare、Stephen Brookes 和 A. W. Roscoe 把 CSP 发展成了一个完整的进程代数（process algebra）。在这个版本里，通信不再基于进程名，而是基于匿名的 channel——这也是 Go 语言采纳的版本。

CSP 对编程语言的影响是深远的。它直接影响了 occam 语言（为 INMOS Transputer 处理器设计）、Limbo 语言（Plan 9 的编程语言），以及最重要的——Go 语言的并发模型。Go 不是 CSP 的完全实现，但它借用了核心思想：goroutine 对应 CSP 的进程，channel 对应 CSP 的通信通道。

### CSP 与 Actor 的根本区别

在 Wikipedia 的 CSP 词条里有一个非常清晰的对比，我们来看看它们到底差在哪。

第一个区别是身份。CSP 的进程是匿名的——你不需要知道对方是谁，只需要知道往哪个 channel 发数据。Actor 则不一样，每个 Actor 有一个地址（在 Erlang 里是 pid，在 Akka 里是 ActorRef），消息必须发送到特定地址。这意味着 CSP 的 channel 是一个解耦层：发送方和接收方通过 channel 间接关联，可以随时替换任何一端，而 Actor 模型的耦合更紧——发送方必须知道接收方的地址。

第二个区别在通信的同步性上。CSP 的通信在基础语义上是同步的（rendezvous）——发送方和接收方必须同时就绪，通信才能发生。Actor 模型的通信是异步的——发送方发出消息后立刻返回，不需要等接收方就绪。有趣的是，这两种语义是互为对偶的：同步通信加上缓冲队列就变成了异步通信，异步通信加上确认/应答协议就变成了同步通信。

第三个区别是组合性。CSP 提供了丰富的代数运算符来组合进程——顺序组合、选择（内部/外部）、并行、隐藏等，这些运算符有形式化的语义，可以用工具（比如 FDR refinement checker）做自动化的死锁和活性检查。Actor 模型的组合主要靠消息协议——两个 Actor 之间约定好消息格式和交互序列。前者更形式化，后者更灵活。

> 说实话，这两种模型没有绝对的优劣之分。在实际工程中，选择哪种更多取决于团队的熟悉度和具体的系统特性。Go 选了 CSP，Erlang 选了 Actor，两者都取得了巨大的成功。

## Channel 基础实现

我们来实现一个类 Go channel 的通信管道。Go 的 channel 有两种基本形态：无缓冲 channel（unbuffered channel）和有缓冲 channel（buffered channel）。无缓冲 channel 的发送方会阻塞直到有接收方就绪，接收方也阻塞直到有发送方就绪——这是一种同步通信，发送和接收在同一时刻发生。有缓冲 channel 内部有一个队列，缓冲区未满时发送方不阻塞，缓冲区满时发送方阻塞等待；缓冲区为空时接收方阻塞等待。两种 channel 都支持 `close` 操作——关闭后不能再发送，但可以继续接收剩余的数据。

### 无缓冲 Channel

无缓冲 channel 是最纯粹的形式。发送和接收必须同时发生——就像两个人握手，必须两个人都伸出手才能握上。

```cpp
#pragma once

#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class UnbufferedChannel {
public:
    UnbufferedChannel() = default;
    ~UnbufferedChannel()
    {
        close();
    }

    /// 发送一个值（阻塞，直到有接收方取走）
    /// 返回 true 表示发送成功，false 表示 channel 已关闭
    bool send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待接收方就绪，或者 channel 被关闭
        sender_cv_.wait(lock, [this] {
            return receiver_waiting_ || closed_;
        });

        if (closed_) {
            return false;
        }

        // 把值交给接收方
        transfer_buffer_ = value;
        data_ready_ = true;

        // 唤醒接收方来取数据
        receiver_cv_.notify_one();

        // 等待接收方确认取走了数据
        sender_cv_.wait(lock, [this] {
            return !data_ready_ || closed_;
        });

        return !closed_;
    }

    /// 接收一个值（阻塞，直到有发送方送来数据）
    std::optional<T> receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // 标记有接收方在等待
        receiver_waiting_ = true;
        sender_cv_.notify_one();

        // 等待数据到达，或者 channel 关闭且数据已耗尽
        receiver_cv_.wait(lock, [this] {
            return data_ready_ || closed_;
        });

        receiver_waiting_ = false;

        if (data_ready_) {
            T value = std::move(transfer_buffer_);
            data_ready_ = false;

            // 通知发送方：数据已被取走
            sender_cv_.notify_one();
            return value;
        }

        // channel 已关闭且没有数据
        return std::nullopt;
    }

    /// 关闭 channel
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            closed_ = true;
        }
        sender_cv_.notify_all();
        receiver_cv_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable sender_cv_;
    std::condition_variable receiver_cv_;

    T transfer_buffer_;          // 数据传递缓冲区
    bool data_ready_{false};     // 是否有待取的数据
    bool receiver_waiting_{false}; // 是否有接收方在等待
    bool closed_{false};
};
```

无缓冲 channel 的实现核心是"rendezvous"——发送方和接收方在同一个时刻完成数据交换。`send()` 把数据放进 `transfer_buffer_`，然后唤醒接收方，自己等待接收方确认取走。`receive()` 标记自己在等待，然后等数据到达。双方通过两个条件变量（`sender_cv_` 和 `receiver_cv_`）来协调。

这个实现有一个微妙的地方：`receiver_waiting_` 标志。它告诉发送方"现在有人在等着接收"，这样发送方就知道可以安全地开始传输了。如果没有这个标志，发送方可能在没有任何接收方的情况下唤醒——这就像你对着空房间说"有人要这个快递吗？"，然后永远等不到回应。

> ⚠️ **注意**：无缓冲 channel 的 send 是同步的——它会阻塞直到接收方取走数据。如果你的代码里有一个发送方但没有接收方，send 会永远阻塞。这是 CSP 哲学的直接体现：通信是同步的，双方必须同时参与。如果这不合你的意，用有缓冲 channel。

### 有缓冲 Channel

有缓冲 channel 内部就是一个线程安全队列——这我们在 ch04 已经非常熟悉了。缓冲区未满时，发送方直接入队就返回；缓冲区满时，发送方阻塞等待有空位。关闭后，接收方可以继续消费队列里的剩余数据，队列空了才返回 `std::nullopt`。

```cpp
template <typename T>
class BufferedChannel {
public:
    explicit BufferedChannel(size_t capacity)
        : capacity_(capacity)
    {
    }

    ~BufferedChannel()
    {
        close();
    }

    /// 发送一个值（缓冲区满时阻塞）
    bool send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待缓冲区有空位，或者 channel 被关闭
        not_full_cv_.wait(lock, [this] {
            return buffer_.size() < capacity_ || closed_;
        });

        if (closed_) {
            return false;
        }

        buffer_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    /// 尝试发送（非阻塞）
    /// 返回 true 表示发送成功
    bool try_send(const T& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (closed_ || buffer_.size() >= capacity_) {
            return false;
        }

        buffer_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    /// 接收一个值（缓冲区空时阻塞）
    std::optional<T> receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_cv_.wait(lock, [this] {
            return !buffer_.empty() || closed_;
        });

        if (buffer_.empty()) {
            // closed_ 一定为 true，且缓冲区已空
            return std::nullopt;
        }

        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    /// 尝试接收（非阻塞）
    std::optional<T> try_receive()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (buffer_.empty()) {
            return std::nullopt;
        }

        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    /// 关闭 channel
    /// 关闭后不能再 send，但可以继续 receive 缓冲区里的剩余数据
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_cv_.notify_all();
        not_empty_cv_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    /// 当前缓冲区中的元素数量
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_full_cv_;
    std::condition_variable not_empty_cv_;
    std::queue<T> buffer_;
    size_t capacity_;
    bool closed_{false};
};
```

有缓冲 channel 的实现就是经典的生产者-消费者模型。两个条件变量分别管理"缓冲区不满"和"缓冲区不空"两个条件。close 时唤醒所有等待中的线程——发送方发现 closed 返回 false，接收方继续消费完剩余数据后返回 `std::nullopt`。

这个 close 语义和 Go 的 channel 关闭行为基本一致：关闭后不能再发送（我们的实现里 send 返回 false，Go 里会 panic），接收方可以继续读取缓冲区里的数据直到耗尽（然后 Go 里会返回零值，我们返回 `std::nullopt`）。

### 统一的 Channel 接口

在实际使用中，我们往往不想关心一个 channel 到底是无缓冲还是有缓冲的——API 应该是一致的。所以我们把两种实现统一成一个模板类，用 `capacity` 参数来区分：0 就是无缓冲，大于 0 就是有缓冲。

```cpp
template <typename T>
class Channel {
public:
    /// capacity = 0 表示无缓冲 channel
    explicit Channel(size_t capacity = 0)
        : capacity_(capacity)
    {
    }

    ~Channel() { close(); }

    // 禁止拷贝
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    /// 发送（阻塞）
    bool send(const T& value)
    {
        if (capacity_ == 0) {
            return unbuffered_send(value);
        }
        return buffered_send(value);
    }

    /// 接收（阻塞）
    std::optional<T> receive()
    {
        if (capacity_ == 0) {
            return unbuffered_receive();
        }
        return buffered_receive();
    }

    /// 尝试发送（非阻塞）
    bool try_send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) return false;

        if (capacity_ == 0) {
            // 无缓冲 channel：没有接收方在等待，或者前一次传输尚未被消费，就失败
            if (!receiver_waiting_ || data_ready_) return false;
            transfer_buffer_ = value;
            data_ready_ = true;
            receiver_cv_.notify_one();
            return true;
        }

        if (buffer_.size() >= capacity_) return false;
        buffer_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    /// 尝试接收（非阻塞）
    std::optional<T> try_receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (capacity_ == 0) {
            if (!data_ready_) return std::nullopt;
            T value = std::move(transfer_buffer_);
            data_ready_ = false;
            sender_cv_.notify_one();
            return value;
        }

        if (buffer_.empty()) return std::nullopt;
        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        sender_cv_.notify_all();
        receiver_cv_.notify_all();
        not_full_cv_.notify_all();
        not_empty_cv_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    // --- 无缓冲 channel 的实现 ---
    bool unbuffered_send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        sender_cv_.wait(lock, [this] {
            return receiver_waiting_ || closed_;
        });
        if (closed_) return false;

        transfer_buffer_ = value;
        data_ready_ = true;
        receiver_cv_.notify_one();

        sender_cv_.wait(lock, [this] {
            return !data_ready_ || closed_;
        });
        return !closed_;
    }

    std::optional<T> unbuffered_receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        receiver_waiting_ = true;
        sender_cv_.notify_one();

        receiver_cv_.wait(lock, [this] {
            return data_ready_ || closed_;
        });
        receiver_waiting_ = false;

        if (data_ready_) {
            T value = std::move(transfer_buffer_);
            data_ready_ = false;
            sender_cv_.notify_one();
            return value;
        }
        return std::nullopt;
    }

    // --- 有缓冲 channel 的实现 ---
    bool buffered_send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_cv_.wait(lock, [this] {
            return buffer_.size() < capacity_ || closed_;
        });
        if (closed_) return false;

        buffer_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    std::optional<T> buffered_receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_cv_.wait(lock, [this] {
            return !buffer_.empty() || closed_;
        });
        if (buffer_.empty()) return std::nullopt;

        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    mutable std::mutex mutex_;

    // 无缓冲通道使用的成员
    std::condition_variable sender_cv_;
    std::condition_variable receiver_cv_;
    T transfer_buffer_;
    bool data_ready_{false};
    bool receiver_waiting_{false};

    // 有缓冲通道使用的成员
    std::condition_variable not_full_cv_;
    std::condition_variable not_empty_cv_;
    std::queue<T> buffer_;
    size_t capacity_;

    bool closed_{false};
};
```

这个统一接口把两种 channel 的实现打包在一起，构造时指定 `capacity` 就决定了行为——0 是无缓冲，大于 0 是有缓冲。对外暴露的 `send` 和 `receive` 完全一致，使用者不需要关心内部到底是直接交接还是走队列。这种设计在 Go 里也是一样的——`make(chan int)` 创建无缓冲 channel，`make(chan int, 5)` 创建缓冲区大小为 5 的 channel，用起来没有区别。

## Select 模式

Go 语言的 `select` 语句是 CSP 模型中最强大的组合原语之一。它允许你同时等待多个 channel 操作，哪个先就绪就执行哪个：

```go
// Go 代码示例
select {
case msg := <-ch1:
    fmt.Println("收到 from ch1:", msg)
case msg := <-ch2:
    fmt.Println("收到 from ch2:", msg)
case ch3 <- 42:
    fmt.Println("发送 42 到 ch3 成功")
case <-time.After(time.Second):
    fmt.Println("超时")
}
```

C++ 没有语言级的 select，但我们可以用轮询 + 条件变量的方式来模拟核心思想。完整的 select 实现相当复杂（需要公平调度、随机选择、避免饥饿等），这里我们实现一个简化版，展示核心机制。

### 简化版 Select

```cpp
/// channel 操作类型
enum class ChannelOpType {
    kSend,
    kReceive
};

/// 一个 channel 操作描述
template <typename T>
struct ChannelOp {
    Channel<T>* channel;
    ChannelOpType type;
    T send_value;            // 仅 kSend 时使用
    std::optional<T> result; // 仅 kReceive 时填充
    bool completed{false};
};

/// 简化版 select：同时等待多个 channel 操作
/// 返回第一个完成的操作的索引，如果没有操作能完成则阻塞
///
/// 用法示例：
///   Channel<int> ch1, ch2;
///   auto ops = make_receive_ops(ch1, ch2);
///   size_t idx = channel_select(ops);
///   if (idx == 0) { /* ch1 有数据 */ auto val = ops[0].result; }
///   if (idx == 1) { /* ch2 有数据 */ auto val = ops[1].result; }
template <typename T>
size_t channel_select(std::vector<ChannelOp<T>>& ops)
{
    // 反复轮询，尝试完成某个操作
    while (true) {
        for (size_t i = 0; i < ops.size(); ++i) {
            auto& op = ops[i];
            if (op.completed) {
                return i;
            }

            if (op.type == ChannelOpType::kReceive) {
                auto result = op.channel->try_receive();
                if (result.has_value()) {
                    op.result = std::move(result);
                    op.completed = true;
                    return i;
                }
            }
            else {
                if (op.channel->try_send(op.send_value)) {
                    op.completed = true;
                    return i;
                }
            }
        }

        // 没有操作能立刻完成，短暂让出时间片后再试
        // 真实实现应该用条件变量等待而不是忙等
        std::this_thread::yield();
    }
}

/// 辅助函数：创建一组接收操作
template <typename T, typename... Channels>
std::vector<ChannelOp<T>> make_receive_ops(Channels&... channels)
{
    std::vector<ChannelOp<T>> ops;
    (ops.push_back(ChannelOp<T>{
        &channels, ChannelOpType::kReceive, T{}, std::nullopt, false
    }), ...);
    return ops;
}
```

> ⚠️ **注意**：这个 select 实现是高度简化的。它使用忙等（`yield`）来轮询所有 channel，在高频场景下会浪费 CPU。Go 的 select 内部使用了复杂的运行时机制（`selectgo`），在等待时会让 goroutine 休眠，在 channel 就绪时精确唤醒，而且保证了多个 case 同时就绪时的随机选择以避免饥饿。如果要在 C++ 中实现高效的 select，需要维护一个全局的轮询器，或者用 epoll/kqueue 等系统级 I/O 多路复用机制。但对于理解 select 的语义来说，这个简化版已经足够了。

## 实战一：生产者-消费者模式

生产者-消费者是 channel 最经典的应用场景。我们用有缓冲 channel 来实现一个多生产者、多消费者的流水线。

```cpp
#include "channel.hpp"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

void producer(Channel<int>& ch, int id, int count)
{
    for (int i = 0; i < count; ++i) {
        int value = id * 1000 + i;
        ch.send(value);
        std::cout << "[Producer " << id << "] 发送: "
                  << value << "\n";

        // 模拟生产耗时
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10 + id * 5)
        );
    }
}

void consumer(Channel<int>& ch, int id)
{
    while (true) {
        auto value = ch.receive();
        if (!value.has_value()) {
            // channel 已关闭且缓冲区为空
            std::cout << "[Consumer " << id << "] 退出\n";
            break;
        }
        std::cout << "[Consumer " << id << "] 接收: "
                  << *value << "\n";
    }
}

int main()
{
    // 创建一个缓冲区大小为 5 的 channel
    Channel<int> ch(5);

    // 启动 2 个生产者和 3 个消费者
    std::vector<std::thread> threads;

    threads.emplace_back(producer, std::ref(ch), 0, 10);
    threads.emplace_back(producer, std::ref(ch), 1, 10);

    threads.emplace_back(consumer, std::ref(ch), 0);
    threads.emplace_back(consumer, std::ref(ch), 1);
    threads.emplace_back(consumer, std::ref(ch), 2);

    // 等待生产者完成
    // 注意：这里简化了，真实场景需要更好的协调机制
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 关闭 channel，通知消费者退出
    ch.close();

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}
```

这个例子非常直白：生产者往 channel 里塞数据，消费者从 channel 里取数据，channel 的缓冲区起到了弹性调节的作用——生产者暂时快的时候数据存在缓冲区里，消费者暂时快的时候缓冲区被消耗掉。当缓冲区满时生产者自动阻塞，当缓冲区空时消费者自动阻塞。不需要任何显式的锁或条件变量，channel 帮你全管了。

## 实战二：管道模式

管道（pipeline）是另一个 channel 的经典用法。管道的核心思想是把一个复杂的数据处理流程拆成多个阶段，每个阶段是一个独立的 goroutine（在 C++ 里就是线程），阶段之间用 channel 连接。

```cpp
#include "channel.hpp"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

/// 阶段一：生成数据
void generator(Channel<int>& output, int count)
{
    for (int i = 1; i <= count; ++i) {
        output.send(i);
        std::cout << "[Generator] 产生: " << i << "\n";
    }
    output.close();
    std::cout << "[Generator] 完成\n";
}

/// 阶段二：平方运算
void squarer(Channel<int>& input, Channel<int>& output)
{
    while (true) {
        auto value = input.receive();
        if (!value.has_value()) {
            break;
        }
        int squared = (*value) * (*value);
        output.send(squared);
        std::cout << "[Squarer] " << *value
                  << " -> " << squared << "\n";
    }
    output.close();
    std::cout << "[Squarer] 完成\n";
}

/// 阶段三：打印结果
void printer(Channel<int>& input)
{
    while (true) {
        auto value = input.receive();
        if (!value.has_value()) {
            break;
        }
        std::cout << "[Printer] 结果: " << *value << "\n";
    }
    std::cout << "[Printer] 完成\n";
}

int main()
{
    // 创建连接各阶段的 channel
    Channel<int> gen_to_square(3);   // generator -> squarer
    Channel<int> square_to_print(3); // squarer -> printer

    // 启动管道各阶段
    std::thread t1(generator, std::ref(gen_to_square), 8);
    std::thread t2(squarer,
                   std::ref(gen_to_square),
                   std::ref(square_to_print));
    std::thread t3(printer, std::ref(square_to_print));

    t1.join();
    t2.join();
    t3.join();

    // 预期输出：
    // [Generator] 产生: 1
    // [Squarer] 1 -> 1
    // [Printer] 结果: 1
    // [Generator] 产生: 2
    // [Squarer] 2 -> 4
    // [Printer] 结果: 4
    // ...
    // [Generator] 产生: 8
    // [Squarer] 8 -> 64
    // [Printer] 结果: 64

    return 0;
}
```

管道模式的美妙之处在于每个阶段都是独立的——它只需要关心从输入 channel 读数据、往输出 channel 写数据，不需要知道数据的来源和去向。这意味着你可以自由地插入、删除、重排阶段，而不影响其他阶段的代码。

Go 的博客里有一个经典的例子是用管道实现并发 MD5 哈希计算——每个文件经过读取、计算、汇总三个阶段，所有阶段并行运行。如果你写过 shell 管道（比如 `cat file | grep pattern | sort | uniq -c`），那你已经理解了 pipeline 的核心思想——只不过这里我们把它用在了 C++ 的并发编程里。

## Channel 与 mutex/condition_variable 的关系

现在我们已经实现并使用了 channel，接下来回答一个你可能早就想问的问题：channel 的底层到底是什么？

答案很简单：**channel 的底层就是 mutex + condition_variable + 队列**。没有什么魔法。

我们的 `BufferedChannel` 里，`mutex_` 保护 `buffer_` 队列，`not_full_cv_` 和 `not_empty_cv_` 分别通知"有空位了"和"有数据了"。这和 ch02 里讲的生产者-消费者模式完全一致。`UnbufferedChannel` 稍微复杂一点，但核心也是 mutex + condition_variable，只是传输模型从"放进队列"变成了"直接交接"。

那问题来了：既然 channel 底层就是锁，为什么还要用 channel？

答案是**抽象层次**。mutex 和 condition_variable 是底层原语，channel 是高层抽象。底层原语灵活但容易出错——你需要自己管理锁的获取和释放、条件变量的等待和通知、状态的检查和保护。高层抽象限制了你的自由度，但换来了正确性的保证——channel 的接口设计保证了你不会忘记解锁、不会忘记 notify、不会写错 wait 条件。

### 选择指南

什么时候用 channel，什么时候直接用 mutex/condition_variable？说实话这个问题没有标准答案，但有一个粗略的判断标准可以参考。

如果你的并发模型本质上就是"数据在生产者和消费者之间流动"——比如管道、工作队列、事件分发、日志收集——那 channel 的语义（发送、接收、关闭）恰好匹配这些场景。另外，当你的系统中需要大量并发实体（goroutine/线程），且它们之间的交互主要是点对点的消息传递时，channel 比锁更适合。

反过来，如果你需要保护的是一小块共享数据，而不是"在实体之间传递数据"——比如一个共享的计数器、一个缓存表、一个配置对象——那用 channel 反而笨拙了。你为了更新一个计数器还得创建一个 channel、一个处理线程、一套消息协议，完全得不偿失。另外，当你需要非常精细的性能控制时（比如在热路径上），直接用 atomic 或者 spinlock 可能比 channel 的开销低得多。

一个实用的经验法则：如果你发现自己在用 channel 来模拟一把锁（比如用一个 channel 来串行化对某个资源的访问），那你应该直接用锁。channel 解决的是"数据在实体之间流动"的问题，不是"保护共享状态"的问题。用对了工具，代码才干净。

## CSP 在 C++ 中的生态系统

虽然 C++ 标准库里没有 channel，但社区里有一些成熟的库提供了类似的功能：

- **Boost.Asio** 的 `experimental::channel`：Boost 正在实验性地引入 channel，API 风格接近 Go 的 channel，但集成了 Asio 的执行器模型。
- **cppcoro**（Lewis Baker）：虽然主要是协程库，但它提供的 `single_consumer_async_queue` 和 `static_thread_pool` 可以构建 channel 语义。
- **Folly**（Facebook/Meta）：`folly/ProducerConsumerQueue.h` 提供了高性能的单生产者单消费者无锁队列，可以用作 channel 的底层。
- **moodycamel::ConcurrentQueue**：一个高性能的多生产者多消费者无锁队列，很多高性能 channel 实现的底层。

如果你在严肃的项目中需要 channel，建议优先考虑 Boost.Asio 的实验性 channel 或者基于 moodycamel 的封装，而不是像我们这样从头实现——我们的实现侧重于教学目的，在高并发场景下的性能和公平性还有很多可以优化的地方。

## 我们的位置

这一篇我们从 CSP 的理论出发，了解了它与 Actor 模型的核心区别——匿名的 channel 对比有身份的 Actor、同步通信对比异步通信、代数组合对比消息协议。然后我们用 C++ 实现了一个完整的 channel 类，包括无缓冲和有缓冲两种模式、close 语义、try_send/try_receive、以及一个简化版的 select。最后通过生产者-消费者和管道两个实战案例展示了 channel 的使用方式，并讨论了 channel 与 mutex/condition_variable 的选择标准。

到这里，ch07 的两篇文章就结束了。我们用了两篇的篇幅来探讨"不用共享内存"的并发范式——Actor 模型和 CSP 模型。它们都追求同一个目标：消除共享状态带来的复杂性，但走了不同的路。Actor 用身份和邮箱来解耦，CSP 用匿名的 channel 来解耦。在实际工程中，这两种模型经常会混合使用——比如在一个 Actor 系统内部，Actor 之间的通信可能通过 channel 来实现。

下一篇开始，我们将进入卷五的最后一个大话题：调试、测试与性能优化——当你的并发程序出了问题时，怎么定位和修复。从理论到实践，从实现到排障，这是我们整个并发之旅的闭环。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch07-actor-channel/`。

## 参考资源

- [Communicating Sequential Processes — Hoare, 1978 (CACM)](https://dl.acm.org/doi/10.1145/359576.359585) — CSP 的原始论文
- [Communicating Sequential Processes — Hoare, 1985 (Book)](https://usingcsp.com/cspbook.pdf) — CSP 的完整专著，免费在线版
- [CSP — Wikipedia](https://en.wikipedia.org/wiki/Communicating_sequential_processes) — CSP 的详细历史和理论介绍
- [Go Channel Types Specification](https://go.dev/ref/spec#Channel_types) — Go 语言 channel 的官方语义定义
- [Go Concurrency Patterns: Pipelines and cancellation (Go Blog)](https://go.dev/blog/pipelines) — Go 官方博客的管道模式教程
- [Share Memory By Communicating (Go Blog)](https://go.dev/blog/codelab-share) — Go 对 CSP 哲学的阐述
- [Boost.Asio Experimental Channel](https://www.boost.org/doc/libs/release/doc/html/boost_asio/overview/composition/channel.html) — C++ 生态中正在标准化的 channel 实现
