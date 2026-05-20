---
title: "线程安全队列"
description: "用 mutex + condition_variable 构建可关闭、支持超时的 bounded blocking queue"
chapter: 4
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard: [11, 14, 17, 20]
prerequisites:
  - "condition_variable 与等待语义"
related:
  - "线程安全容器设计"
  - "SPSC 与 MPMC 队列"
---

# 线程安全队列

上一篇我们在 condition_variable 那篇里写了一个简化版的 `BoundedQueue`——有 `push` 和 `pop`，能阻塞，能通知。说实话，当时写完觉得还挺像回事的，但如果你直接把它扔进生产代码里，我敢打赌不用两天就会出问题：队列怎么优雅地关闭？消费者阻塞在 `pop` 上的时候，生产者线程崩了怎么办？如果我不想无限期等待，只想尝试取一个元素呢？如果我想从外部取消等待呢？

这些问题不解决，这个队列就只是个教学玩具。这篇我们就把它从一个教学玩具改造成一个真正能用的组件——加上关闭机制、带超时的 try_push / try_pop、C++20 的 stop_token 集成，以及在队列满时的背压策略。我们会一步步来，每一步都在前一步的基础上增加一个能力，让你清楚地看到每个设计决策的来龙去脉。先别急，我们先把基础打牢。

## 起点：一个能用的 BoundedQueue

我们先把 condition_variable 那篇写的队列搬过来，作为今天的起点：

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity)
    {}

    void push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        not_empty_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return value;
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};
```

这个版本的核心逻辑没有问题。双条件变量（`not_full_` 和 `not_empty_`）各管各的等待者和通知者，带谓词的 `wait` 防住了虚假唤醒和丢失唤醒。但如果你仔细想想，它有三个致命缺陷：第一，`push` 和 `pop` 都可能无限期阻塞——如果生产者永远不 push，消费者就永远等下去，反之亦然；第二，没有任何关闭机制——队列的生命周期结束时，如果还有线程在 `wait` 上阻塞，它们永远不会醒来，程序直接卡死；第三，没有超时能力——调用者无法在指定时间内放弃等待。

这三个问题不解决，你拿这个队列去写服务端代码，基本上就是一个定时炸弹。接下来我们一个一个拆掉。

## 第一步：关掉它——关闭队列的正确姿势

关闭机制是线程安全队列最重要的非功能性需求，没有之一。想象一个典型的生产者-消费者场景：多个生产者往队列里放任务，多个消费者从队列里取任务执行。当程序需要退出时——不管是正常关闭、收到 SIGTERM，还是发生了什么异常——我们希望有一个清晰的 shutdown 流程：生产者停止投放新任务，消费者把队列里剩余的任务处理完，然后各自体面地退出。如果连"关机"都做不到，这个队列用着用着你就会心虚。

关闭的语义需要仔细设计，不是简单设个 `closed_ = true` 就完事了。我们需要一个 `closed_` 标志位来表示队列是否已关闭，它影响 `push` 和 `pop` 的行为。`push` 的规则比较简单：队列关闭后，所有新的 push 都应该被拒绝，因为没有人会再来消费这些数据了。`pop` 的规则要微妙一些：关闭后，如果队列里还有元素，消费者应该能把它们全部取走（drain），直到队列变空；队列空了之后，`pop` 不应该再阻塞，而应该返回一个"队列已空且已关闭"的信号。这个 drain 语义非常重要——如果不允许 drain，关闭时队列里还没处理的任务就全丢了。

好，语义搞清楚了，我们用一个枚举来表示操作结果：

```cpp
enum class QueueResult {
    kSuccess,
    kClosed,
    kTimeout
};
```

接下来把 `closed_` 标志加到队列里，修改 `push` 和 `pop` 的谓词逻辑：

```cpp
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity), closed_(false)
    {}

    // 关闭队列。调用后 push 会失败，pop 会 drain 剩余元素后失败
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        // 唤醒所有正在等待的线程，让它们检查 closed_ 标志
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    QueueResult push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 谓词：队列不满且未关闭时可以 push
        not_full_.wait(lock, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    QueueResult pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 谓词：队列不空，或者队列已关闭且已空
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });

        if (queue_.empty()) {
            // 队列空 + closed_ 为 true = drain 完成
            return QueueResult::kClosed;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};
```

代码量不小，我们来拆一下里面的门道。先看 `close()` 方法——它先在锁的保护下设置 `closed_ = true`，然后释放锁，再用 `notify_all()` 唤醒所有等待线程。你可能想问为什么不直接在锁里面 notify？技术上可以，但 `notify_all` 本身不需要在锁内执行（标准允许在锁外通知），把 notify 移到锁外面可以减少一次不必要的锁争用：被唤醒的线程不需要等待 close 线程释放锁就能立刻去抢。而为什么要用 `notify_all` 而不是 `notify_one`？因为关闭是一个全局事件——所有在等待的生产者和消费者都需要被叫醒。如果只用 `notify_one`，每次只叫醒一个线程，其他线程还在傻等，那就需要被叫醒的那个线程再 `notify` 下一个……这个链条太脆弱了，而且延迟不可控。`notify_all` 是关闭场景下的标准做法。

再来看 `push` 的谓词。之前是 `queue_.size() < capacity_`，现在加上了 `|| closed_`。这意味着 `wait` 在两种情况下会返回：要么队列不满了，要么队列关闭了。返回后我们检查 `closed_`——如果为 `true`，说明队列已经关闭了，我们不应该 push，直接返回 `kClosed`。注意这里的检查顺序：先检查 `closed_`，再决定是否操作。这保证了关闭后不会有新元素进入队列。

`pop` 的谓词类似：`!queue_.empty() || closed_`。`wait` 返回后我们检查 `queue_.empty()`——如果队列空了，不管 `closed_` 是什么状态都没有东西可取，返回 `kClosed`。如果队列不空，即使 `closed_` 为 `true` 也继续取——这就是 drain 语义：关闭后允许消费者把剩余元素全部消费完。

你可能注意到了一个微妙的细节：`push` 返回后检查 `closed_`，但 `pop` 返回后检查 `queue_.empty()` 而不是 `closed_`。为什么不对称？因为语义不同：push 被拒绝的唯一原因是队列关闭了（队列不满的情况下 push 不会被阻塞），而 pop 失败的原因是队列空了（不管是否关闭）。关闭后队列不空时，pop 应该继续取出剩余元素；关闭后队列空了，pop 才应该报告失败。所以 pop 用 `queue_.empty()` 作为"是否还有东西可取"的判断标准——这比直接查 `closed_` 更准确地反映了 pop 的意图。

## 第二步：不想死等——带超时的 try_push 和 try_pop

关闭机制解决了"优雅退出"的问题，很好。但还有一类场景它管不了：调用者不想无限期阻塞，只想在一定时间内尝试操作，超时就放弃。比如一个网络服务想把请求塞进队列，但如果队列满了等了 100 毫秒还没位置，它宁可丢弃这个请求也不愿阻塞——响应延迟比丢一两个请求更致命。这时候就需要带超时的 `try_push` 和 `try_pop`。

我们直接用 `wait_for` 来实现，它天然适合这种"等一下试试"的场景：

```cpp
template <typename T>
class BoundedQueue {
public:
    // ... 前面的方法不变 ...

    template <typename Rep, typename Period>
    QueueResult try_push(T value,
                         const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_full_.wait_for(lock, timeout, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (!ok) {
            // 超时了，谓词仍然为 false
            return QueueResult::kTimeout;
        }

        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    template <typename Rep, typename Period>
    QueueResult try_pop(T& value,
                        const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_empty_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok) {
            return QueueResult::kTimeout;
        }

        if (queue_.empty()) {
            return QueueResult::kClosed;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }
};
```

`wait_for` 的带谓词版本返回 `bool`——如果谓词为 `true` 就返回 `true`（不管是被通知了还是超时前一瞬间条件满足了），超时且谓词仍为 `false` 时返回 `false`。我们利用这个返回值来区分三种情况：超时（`!ok`，返回 `kTimeout`），关闭（`ok` 但 `closed_` 为 `true`，返回 `kClosed`），以及成功。

这里有一个设计选择值得说一下：为什么先检查 `!ok` 再检查 `closed_`？因为如果超时了，我们不需要再关心 `closed_` 的状态——调用者关心的是"我在给定时间内没操作成功"，具体原因（队列满还是队列关闭）对调用者来说已经不重要了。当然，你也可以反过来——如果你的业务场景需要区分"超时"和"关闭"，把判断顺序调整一下就行。这里没有唯一的正确答案，取决于你要把什么信息传递给调用者。

## 第三步：让它可以被取消——C++20 stop_token 集成

`try_push` 和 `try_pop` 解决了"我不想等太久"的问题，但还有一种场景它管不了：外部主动取消等待。C++20 引入了 `std::stop_token` / `std::stop_source` / `std::jthread` 三件套，提供了协作式取消的标准机制。我们能不能让队列的 `pop` 操作支持 `stop_token`——当外部请求停止时，正在阻塞的 `pop` 立刻被唤醒，不用等超时，不用等队列有数据？

答案是肯定的，但有一个前提条件：需要用 `std::condition_variable_any` 代替 `std::condition_variable`。原因是 C++20 给 `condition_variable_any` 新增了接受 `stop_token` 的 `wait` 重载——当 stop 被请求时，`wait` 会被自动唤醒。而 `std::condition_variable` 没有这个重载，因为它与 `unique_lock<mutex>` 的耦合太深，加入 stop_token 支持需要修改内部实现，标准委员会选择了只在更通用的 `condition_variable_any` 上提供这个功能。也就是说，想要 stop_token，就得接受 `condition_variable_any` 稍重一些的开销。

我们来看怎么集成。为了突出核心逻辑，这里先给一个独立的简化版——只保留 stop_token 相关的 pop 和它需要的最小上下文：

```cpp
#include <stop_token>
#include <condition_variable>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity), closed_(false)
    {}

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    // 支持 stop_token 的 pop：外部请求停止时返回 false
    bool pop(T& value, std::stop_token stoken)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // condition_variable_any 的 stop_token 重载
        bool ok = cv_.wait(lock, stoken, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok) {
            // stop 被请求了，谓词还没满足
            return false;
        }

        if (queue_.empty()) {
            // 队列关闭且已空
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();
        cv_.notify_one();
        return true;
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_;
    mutable std::mutex mutex_;
    // 使用 condition_variable_any 以支持 stop_token
    std::condition_variable_any cv_;
};
```

你会发现这段代码和前面的版本相比，最核心的变化就一个：条件变量从 `std::condition_variable` 换成了 `std::condition_variable_any`。后者的接口完全兼容前者，但额外支持与 `stop_token` 搭配使用——代价是内部实现稍重一些（需要一个额外的内部 mutex 来管理等待队列），但在绝大多数场景下这点开销完全可以忽略。

然后是 `cv_.wait(lock, stoken, pred)` 的语义。它等待直到 `pred()` 为 `true`，或者 `stoken` 上的 stop 被请求。返回 `true` 表示谓词满足，返回 `false` 表示 stop 被请求且谓词未满足。如果 stop 被请求时谓词恰好也满足了，返回 `true`——也就是说谓词优先于 stop。这很合理：如果你等的东西已经到了，就没必要因为 stop 而放弃它。

在消费端，配合 `std::jthread` 使用非常自然。`jthread` 是 C++20 新引入的线程类，和 `std::thread` 最大的区别就是它内置了 stop_token 支持和自动 join 语义——析构时会自动请求停止并等待线程结束，再也不用手动 join 了：

```cpp
#include <thread>
#include <iostream>

int main()
{
    BoundedQueue<int> queue(16);

    std::jthread consumer([&](std::stop_token stoken) {
        int value;
        while (queue.pop(value, stoken)) {
            std::cout << "Consumed: " << value << "\n";
        }
        std::cout << "Consumer exiting (stop requested or queue closed)\n";
    });

    // 生产者
    for (int i = 0; i < 100; ++i) {
        queue.push(i);
    }

    // 优雅关闭：先关闭队列，再请求停止
    queue.close();
    consumer.request_stop();

    // jthread 析构时自动 join
    return 0;
}
```

`jthread` 在构造时会自动把内部的 `stop_token` 传给线程函数——只要函数签名的第一个参数是 `std::stop_token`。消费者在 `pop` 中传入这个 `stop_token`，当主线程调用 `request_stop()` 时，正在阻塞的 `pop` 会被唤醒并返回 `false`，消费者循环就此退出。

> 有一点值得强调：这里我们同时做了 `close()` 和 `request_stop()`。`close()` 保证生产者不再投放新元素，`request_stop()` 保证消费者不会在空队列上无限期等待。两者缺一不可——只 close 不 stop，消费者可能还在 `pop` 里傻等最后一个元素（如果队列已经空了）；只 stop 不 close，生产者可能还在往一个没人消费的队列里塞数据。两者配合才是完整的优雅退出。

## 第四步：队列满了怎么办——背压策略

到现在为止，我们处理队列满的方式都是"阻塞等待"——生产者在 `push` 中阻塞，直到消费者取走元素腾出空间。这是最简单的策略，但不是唯一的。在某些场景下，阻塞生产者是不合适甚至危险的。想象一个高吞吐量的网络服务，每秒接收上万条请求，如果下游处理速度跟不上导致队列满了，生产者线程阻塞会意味着整个服务的接收线程卡死，新连接全部超时——这不是"慢了一点"，是整个服务挂了。这时候我们需要的是**背压（backpressure）**——让生产者感知到下游的压力，做出有意识的应对，而不是傻等。

常见的背压策略有三种。第一种是阻塞等待，也就是我们已有的实现，适用于生产者可以承受延迟的场景。第二种是丢弃最新（drop newest）——队列满了就直接丢弃新来的元素，适用于允许丢数据的场景，比如日志聚合、指标上报。第三种是丢弃最旧（drop oldest）——队列满了就把队列里最老的元素踢掉，腾出位置给新元素，适用于"只关心最近数据"的场景，比如实时监控的滑动窗口。

我们以丢弃最新为例，实现一个 `push_or_drop`。它的语义很简单：队列没满就正常入队，满了就直接丢弃，绝不阻塞：

```cpp
// 如果队列满了就丢弃，不阻塞
// 返回 true 表示成功入队，false 表示被丢弃
bool push_or_drop(T value)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_) {
        return false;
    }

    if (queue_.size() >= capacity_) {
        // 队列满，丢弃
        return false;
    }

    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

你会发现这里不需要 `condition_variable` 的等待——直接加锁检查容量，满了就返回 `false`。这个操作的时间复杂度是 O(1)，不会阻塞，生产者永远卡不死。调用者拿到 `false` 后可以决定是重试、丢弃还是走降级逻辑，灵活性比阻塞等待好得多。

如果需要丢弃最旧的策略，逻辑稍作修改，把最老的元素踢掉就行：

```cpp
bool push_or_evict_oldest(T value)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (closed_) {
        return false;
    }

    if (queue_.size() >= capacity_) {
        // 踢掉最老的元素
        queue_.pop();
    }

    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

这种"策略化"的设计在实际项目中很常见——队列本身提供多种 push 模式，让调用者根据业务场景选择合适的策略。你也可以把策略模板化或者用枚举参数化，让队列在编译期或运行时决定背压行为。怎么选取决于你的业务是"宁可丢也不能卡"还是"宁可卡也不能丢"——这两种需求笔者在实际项目中都碰到过。

## 多生产者多消费者的正确性

我们前面的所有实现都天然支持 MPMC（Multiple Producers, Multiple Consumers）场景——因为所有对共享状态（`queue_`、`closed_`）的访问都在 `mutex_` 的保护下进行。所以"正确性"这块是不用担心的。但"正确"和"高效"是两回事，我们来看看在实际的 MPMC 场景中你会踩什么坑。

最明显的问题是锁争用。当生产者和消费者数量增加时，所有线程都在竞争同一把 mutex——某个时刻只有一个线程能操作队列，其他线程都在等锁。在高吞吐场景下，这把 mutex 会成为瓶颈，大家排队等锁的时间可能比真正干活的时间还长。我们下一篇会详细讨论分片锁、细粒度锁等减少争用的策略，这里先知道有这么个问题就行。

另一个容易忽视的问题是 `notify_one` 的公平性。`notify_one` 唤醒的是等待队列中的"一个"线程，但具体是哪个线程取决于操作系统的调度策略——通常是 FIFO（先等的先醒），但标准并不保证这一点。在极端情况下，某些消费者可能总是被跳过，导致饥饿。如果你需要严格的公平性，需要在应用层实现，比如用 ticket lock 或者轮询分发。

还有一个正确性细节值得一提：`notify_one` vs `notify_all` 的选择。我们在 `push` 中用 `notify_one` 唤醒一个消费者，在 `pop` 中用 `notify_one` 唤醒一个生产者。这在 SPSC（单生产者单消费者）和低争用的 MPMC 场景下是最优的——只叫醒一个人，避免了惊群效应。但在高争用场景下，`notify_one` 可能导致一种变体的惊群问题：一个 `notify_one` 唤醒了一个消费者，但这个消费者拿到锁后发现队列已经被另一个消费者抢先取空了，只好继续等。这种"无效唤醒"在高争用下会频繁发生。讽刺的是，在这种场景下 `notify_all` 反而可能更好——虽然它唤醒了更多线程，但至少有一个线程能成功操作。不过这种优化需要对具体的负载模式做基准测试，没有放之四海皆准的答案。

## 异常安全性：一个容易被忽略的角落

最后我们来聊一个很容易被忽略但真出了问题会让人血压拉满的话题：异常安全。在前面的实现中，我们默认假设 `queue_.push(std::move(value))` 不会抛异常——但如果 `T` 的移动构造函数抛了怎么办？`T` 的拷贝构造函数抛了怎么办？

好消息是 `std::queue` 的 `push` 提供了强异常保证：如果 `push` 抛出异常，队列的状态不变（元素不会被添加）。所以在我们的 `push` 方法中，如果 `queue_.push(std::move(value))` 抛了异常，`unique_lock` 的析构函数会自动释放 mutex，`notify_one` 不会被调用（因为异常跳过了它），队列的状态和调用 `push` 之前完全一致——这正好是我们想要的行为。

但还有一个更隐蔽的问题藏在 `pop` 里：如果 `T` 的移动赋值运算符（在 `value = std::move(queue_.front())` 这一行）抛了异常怎么办？这时候元素还在队列里（`queue_.front()` 返回的是引用），但赋值给 `value` 这一步失败了。结果是元素仍然在队列中，但调用者没拿到值——下次 `pop` 还会取到同一个元素。这不一定是个 bug（取决于 `T` 的语义），但如果 `T` 的移动赋值不是 `noexcept` 的，你需要仔细考虑这个边界情况。

如果 `T` 是 `int`、`std::string`、`std::unique_ptr` 这些标准类型，它们的移动操作都是 `noexcept` 的，不用操心。但如果你要存自定义类型，最好确保它的移动操作是 `noexcept` 的——最简单的办法是在队列的模板约束中加上 `static_assert`，让编译器帮你把关：

```cpp
static_assert(std::is_nothrow_move_constructible_v<T>,
              "T must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<T>,
              "T must be nothrow move assignable");
```

这样如果你不小心存了一个会抛异常的类型，编译期就能直接拦住你，而不是等到运行时才在某个奇怪的路径上崩掉。

顺便提一下，`wait` 本身在异常安全方面是可靠的。C++ 标准保证：如果 `wait` 在等待期间收到信号但谓词仍为 `false`（虚假唤醒），它会重新等待，不会泄漏锁。如果 `wait` 因为异常退出（极端情况），锁会被正确释放。所以条件变量的 `wait` 在异常安全方面不需要我们额外操心。

## 完整实现：把一切组装起来

到这里，我们已经讨论了关闭机制、超时操作、stop_token 集成和背压策略。现在把所有这些特性整合到一起，给出一个完整的、可以直接拿去用的 `BoundedBlockingQueue`：

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stop_token>
#include <type_traits>

enum class QueueResult {
    kSuccess,
    kClosed,
    kTimeout
};

template <typename T>
class BoundedBlockingQueue {
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "T must be nothrow move constructible");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "T must be nothrow move assignable");

public:
    explicit BoundedBlockingQueue(std::size_t capacity)
        : capacity_(capacity), closed_(false)
    {}

    // === 基本操作 ===

    QueueResult push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    QueueResult pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });

        if (queue_.empty()) {
            return QueueResult::kClosed;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    // === 超时操作 ===

    template <typename Rep, typename Period>
    QueueResult try_push(T value,
                         const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_full_.wait_for(lock, timeout, [this] {
            return queue_.size() < capacity_ || closed_;
        });

        if (!ok) {
            return QueueResult::kTimeout;
        }
        if (closed_) {
            return QueueResult::kClosed;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    template <typename Rep, typename Period>
    QueueResult try_pop(T& value,
                        const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = not_empty_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok) {
            return QueueResult::kTimeout;
        }
        if (queue_.empty()) {
            return QueueResult::kClosed;
        }

        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

    // === stop_token 可取消操作 (C++20) ===

    bool pop(T& value, std::stop_token stoken)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool ok = cv_any_.wait(lock, stoken, [this] {
            return !queue_.empty() || closed_;
        });

        if (!ok || queue_.empty()) {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();

        if (queue_.size() < capacity_) {
            not_full_.notify_one();
        }
        return true;
    }

    // === 背压策略 ===

    bool push_or_drop(T value)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (closed_ || queue_.size() >= capacity_) {
            return false;
        }

        queue_.push(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    // === 管理 ===

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
        cv_any_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::condition_variable_any cv_any_;  // 给 stop_token 用的
};
```

你可能注意到这里同时维护了 `not_full_`、`not_empty_`（`condition_variable`）和 `cv_any_`（`condition_variable_any`）。基本的 `push`/`pop` 用前者（更高效），`stop_token` 版本的 `pop` 用后者（支持 stop_token）。这是一种实用的折衷：不需要 stop_token 的代码走高性能路径，需要 stop_token 的代码走通用路径。两全其美，各取所需。

## 小结

这篇我们从 condition_variable 文章中的教学版 `BoundedQueue` 出发，一步步把它改造成了一个生产级别的 `BoundedBlockingQueue`。我们依次加上了四个关键能力：关闭机制（`close()` 拒绝新 push、允许 drain pop），带超时的 try_push/try_pop（`wait_for` 实现非阻塞尝试），stop_token 集成（`condition_variable_any` 的 C++20 重载实现协作式取消），以及背压策略（`push_or_drop` 提供非阻塞丢弃模式）。

每个能力都不是孤立的——关闭机制依赖 `notify_all` 来唤醒所有等待线程，超时操作依赖 `QueueResult` 枚举来区分失败原因，stop_token 版本的 pop 需要和 `close()` 配合才能实现完整的优雅退出。这些设计组合在一起，构成了一个在真实项目中可以直接使用的线程安全队列。

当然，这个队列在高争用场景下仍有性能瓶颈——所有线程共享一把 mutex，吞吐量上不去。下一篇我们会讨论分片锁、细粒度锁、copy-on-write 等策略来减少争用，核心思想就是"让更少的线程抢同一把锁"。

## 练习

### 练习 1：bounded blocking queue 附关闭测试

编写一个多线程测试，验证关闭机制的正确性：启动 3 个生产者线程和 2 个消费者线程，生产者各 push 100 个元素，消费者各 pop 直到收到 `kClosed`。在生产者全部完成后调用 `close()`，验证消费者最终恰好消费了 300 个元素（无丢失无重复），且所有线程都正常退出。

提示：用一个 `std::atomic<int>` 统计消费者取到的总元素数，所有线程 join 后检查它是否等于 300。

### 练习 2：超时 pop 的正确性验证

创建一个容量为 5 的队列，不 push 任何元素。启动一个消费者线程调用 `try_pop` 超时 200ms，验证它返回 `kTimeout`。然后向队列 push 一个元素，再次调用 `try_pop` 超时 200ms，验证它返回 `kSuccess`。用 `std::chrono` 测量两次操作的实际耗时，确认超时版本的等待时间在预期范围内。

### 练习 3：stop_token 取消 pop

使用 `std::jthread` 创建一个消费者，传入 `stop_token` 版本的 `pop`。主线程 sleep 100ms 后调用 `request_stop()`，验证消费者线程在 `pop` 中被唤醒并正常退出。然后尝试另一个顺序：先 `close()` 队列，再 `request_stop()`，观察消费者的行为——如果队列里还有元素，消费者应该先把它们取完再退出。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch04-concurrent-data-structures/`。

## 参考资源

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable_any -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any)
- [std::stop_token -- cppreference](https://en.cppreference.com/w/cpp/thread/stop_token)
- [std::jthread -- cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4 & 6](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
- [Why does std::condition_variable not support std::stop_token? -- StackOverflow](https://stackoverflow.com/questions/66309276/why-does-c20-stdcondition-variable-not-support-stdstop-token)
