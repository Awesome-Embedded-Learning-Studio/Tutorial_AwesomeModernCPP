---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: 掌握条件变量的 wait/notify 机制，理解虚假唤醒、谓词写法与丢失唤醒问题
difficulty: intermediate
order: 3
platform: host
prerequisites:
- mutex 与 RAII 锁
reading_time_minutes: 18
related:
- 读写锁与 shared_mutex
- 线程安全队列
tags:
- host
- cpp-modern
- intermediate
- mutex
- 异步编程
title: condition_variable 与等待语义
---
# condition_variable 与等待语义

上一篇我们聊了 mutex 和 RAII 锁——知道怎么保护临界区、怎么避免死锁。但有一个问题我们没有解决：如果线程需要"等某个条件成立"才能继续执行，光靠 mutex 怎么做？最朴素的想法是写一个循环，反复加锁检查条件，不满足就解锁睡一小会儿再试——这就是所谓的**忙等待（busy-wait）**或者**轮询（polling）**。它能工作，但代价是白白消耗 CPU 周期，而且"睡多久"这个参数很难调准：睡短了浪费 CPU，睡长了响应迟钝。

`std::condition_variable` 就是标准库给我们的答案。它提供了一套"等待-通知"机制：线程 A 可以在一个条件变量上**等待**，线程 B 在改变条件后**通知**条件变量，唤醒等待的线程。这套机制比轮询高效得多，因为等待中的线程会被操作系统挂起，不消耗 CPU 时间，直到被通知才重新调度。不过，条件变量的使用有一些非常微妙的坑——虚假唤醒、丢失唤醒、谓词写法——这些才是本文真正的重点。

## std::condition_variable 与 std::condition_variable_any

C++ 标准库提供了两个条件变量类，定义在 `<condition_variable>` 头文件中。`std::condition_variable` 是主力，它只能与 `std::unique_lock<std::mutex>` 搭配使用。`std::condition_variable_any` 是一个更通用的版本，可以跟任何满足 Lockable 要求的锁类型搭配——比如 `std::shared_lock` 或者自定义的锁包装器。代价是 `condition_variable_any` 的内部实现通常更重一些（可能用到额外的内部 mutex 或者动态分配），所以在大多数场景下我们优先使用 `std::condition_variable`。后文除非特别说明，提到"条件变量"指的都是 `std::condition_variable`。

条件变量的核心 API 非常精简，只有三组操作：`wait` 系列（`wait`、`wait_for`、`wait_until`）用于等待通知，`notify_one()` 唤醒一个等待线程，`notify_all()` 唤醒所有等待线程。我们一个一个来拆。

## wait()：最基本的等待

先看一个最简单的例子。假设我们有一个标志位 `ready`，主线程设置它，工作线程等待它变为 `true`：

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock);  // 释放锁，进入等待；被唤醒时重新获取锁
    std::cout << "Worker: proceeding after wakeup\n";
    // lock 在此处析构时释放 mtx
}

int main()
{
    std::thread t(worker);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();

    t.join();
    return 0;
}
```

这里有几个关键细节需要拆开来看。首先，`cv.wait(lock)` 的行为分三步：第一步，原子性地释放 `lock` 关联的 mutex 并将当前线程加入条件变量的等待队列；第二步，线程被挂起，进入阻塞状态，不消耗 CPU；第三步，当收到通知（或虚假唤醒）时，线程被重新调度，重新获取 mutex，然后 `wait` 返回。注意"原子性地释放 mutex 并加入等待队列"这一点非常重要——它保证了在释放 mutex 和开始等待之间不会有缝隙，通知不会在这个缝隙中被错过（这个我们后面会详细讨论）。

其次，`wait` 返回后，当前线程**重新持有 mutex**。这意味着 `wait` 的调用者可以在 `wait` 返回后安全地访问被 mutex 保护的共享状态，而不需要额外加锁。这也是为什么 `wait` 要求传入 `unique_lock` 而不是裸 mutex——`unique_lock` 的所有权在 `wait` 期间被转移出去又转移回来，整个生命周期管理是自动的。

但上面的代码有一个严重的问题。你发现了吗？worker 线程在 `wait` 返回后直接继续执行了，但它**完全没有检查 `ready` 的值**。如果这次唤醒是虚假的呢？如果通知在 worker 调用 `wait` 之前就发出了呢？这个程序的行为就会变得不可预测。这就是我们接下来要讨论的两个核心问题。

## 虚假唤醒：为什么 wait 必须配合谓词使用

**虚假唤醒（spurious wakeup）**是指线程在没有收到 `notify_one()` 或 `notify_all()` 调用的情况下，从 `wait` 中返回。这不是 bug，不是实现质量问题——POSIX 标准和 C++ 标准都明确允许这种行为。为什么？原因在于条件变量的底层实现。

在 Linux 上，`std::condition_variable` 基于 `futex`（fast user-space mutex）系统调用实现。条件变量的内部状态通常用一个原子计数器来跟踪等待者和通知者的数量。为了高效实现 `wait` 和 `notify`，条件变量的实现采用了"分散-聚集"策略：`notify` 只需要递增计数器并唤醒一个等待的 futex，而 `wait` 需要原子地递减计数器并检查是否有未处理的通知。在某些边界条件下——比如一个 `notify_all` 刚刚唤醒了一批线程，而这些线程还没来得及重新检查内部状态——内核可能会多唤醒一些线程。POSIX 标准委员会在权衡了实现效率和语义严格性之后，选择了允许虚假唤醒——这样条件变量可以用更轻量的内核原语实现，而不需要为每次通知都做精确的一对一映射。

具体的后果是：如果你写了 `cv.wait(lock)` 然后 `wait` 返回了，你**不能假设**有人调用了 `notify`。你必须在 `wait` 返回后重新检查等待条件。标准的做法是把 `wait` 放在一个 while 循环里：

```cpp
std::unique_lock<std::mutex> lock(mtx);
while (!ready) {
    cv.wait(lock);
}
// ready == true，安全地继续执行
```

这段代码的逻辑是：先检查条件，不满足就 `wait`，`wait` 返回后再检查，循环直到条件成立。这样虚假唤醒就无害了——即使被虚假唤醒了，循环会再次检查 `ready`，发现还是 `false`，就继续 `wait`。

C++ 标准库把这个模式封装成了一个更方便的重载：**带谓词的 `wait`**：

```cpp
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, [] { return ready; });
// 这里 ready 一定为 true
```

`cv.wait(lock, pred)` 的语义等价于 `while (!pred()) { cv.wait(lock); }`，但它可能比手写循环更高效——因为标准允许实现在某些平台上使用更优化的等待策略（比如在 Linux 上使用 `futex` 的位感知功能）。一句话总结：**永远使用带谓词的 `wait`，永远不要用不带谓词的版本**。这不是建议，是规则。

我们回头看前面的例子，正确写法应该是这样：

```cpp
void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return ready; });
    // 到达这里时，ready 一定为 true，且 lock 被持有
    std::cout << "Worker: proceeding after condition met\n";
}
```

## 丢失唤醒：先通知后等待的灾难

虚假唤醒说的是"没通知就醒了"，而**丢失唤醒（lost wakeup）**恰好相反——"通知了但没人收到"。发生的原因是通知在 `wait` 之前就发出了。

我们来构造一个丢失唤醒的场景：

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker()
{
    // 假设 worker 线程在这里被调度延迟了
    // 主线程先执行了 notify_one()
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::unique_lock<std::mutex> lock(mtx);
    // 如果这里用不带谓词的 wait，就会永远阻塞！
    cv.wait(lock, [] { return ready; });
    std::cout << "Worker: condition met\n";
}

int main()
{
    std::thread t(worker);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();  // 此时 worker 还没开始 wait

    t.join();  // 等待 worker（带谓词版本不会死锁）
    return 0;
}
```

在这个例子中，主线程在 `worker` 调用 `wait` 之前就调用了 `notify_one()`。如果是裸 `wait(lock)` 不带谓词，这个通知就永远丢失了——条件变量不会把通知"存起来"等你来取。但因为我们用了带谓词的版本 `wait(lock, []{ return ready; })`，worker 线程醒来后会检查 `ready` 的值（此时已经是 `true`），直接通过，不需要任何人通知。这就是带谓词 `wait` 的另一个巨大优势：它同时防住了虚假唤醒和丢失唤醒。

不过，更根本的防丢失唤醒策略是保证"检查条件-等待"和"修改条件-通知"使用**同一个 mutex** 保护。当等待线程持有 mutex 检查条件时，通知线程不可能同时修改条件；反之，当通知线程持有 mutex 修改条件时，等待线程不可能已经通过了条件检查却还没开始 `wait`。这就是为什么 `wait` 要求传入 `unique_lock`——它不仅仅是为了在等待期间释放锁，更是为了确保等待和通知之间的同步关系。

## wait_for() 与 wait_until()：超时等待

有时候我们不想无限期地等下去——比如一个网络请求的超时、一个用户操作的取消、一个定期状态检查的场景。`wait_for` 和 `wait_until` 提供了带超时的等待语义。

`wait_for(lock, duration, pred)` 等待指定的时间长度。`wait_until(lock, time_point, pred)` 等待到指定的时间点。两者都支持谓词版本和裸版本（同样，优先使用谓词版本）。带谓词版本返回 `bool`，表示谓词是否为 `true`（可能是被通知了也可能是超时了，但只有谓词为 `true` 才返回 `true`）。不带谓词版本返回 `std::cv_status`，可能是 `no_timeout`（被通知或虚假唤醒）或 `timeout`（超时了）。

看一个实际例子：我们要等待一个任务完成，但最多等 5 秒：

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

std::mutex mtx;
std::condition_variable cv;
bool task_done = false;

void long_task()
{
    std::this_thread::sleep_for(std::chrono::seconds(3));  // 模拟耗时操作
    {
        std::lock_guard<std::mutex> lock(mtx);
        task_done = true;
    }
    cv.notify_one();
}

int main()
{
    std::thread t(long_task);

    std::unique_lock<std::mutex> lock(mtx);
    bool success = cv.wait_for(lock, std::chrono::seconds(5),
                                [] { return task_done; });

    if (success) {
        std::cout << "Task completed within timeout\n";
    } else {
        std::cout << "Task timed out after 5 seconds\n";
        // 注意：t 还在运行，需要决定如何处理
    }

    lock.unlock();
    t.join();  // 无论超时与否，最终都要 join
    return 0;
}
```

`wait_for` 的谓词版本内部实现本质上是一个循环：每次被唤醒（无论是通知还是虚假唤醒），都检查谓词，如果为 `true` 就返回 `true`；如果超时了且谓词仍为 `false`，返回 `false`。注意返回 `false` 并不意味着永远不会有通知了——只是说在指定的时间内条件没有满足。超时之后的处理逻辑需要根据你的业务需求来设计。

`wait_until` 的用法类似，区别在于它接受一个绝对时间点（`std::chrono` 中 `time_point` 类型的模板参数），而不是相对时长。这在需要"在某个截止时间之前完成"的场景中更方便——你不需要自己计算 `now + duration`，直接传一个 deadline 进去就行。不过需要注意，系统时钟的调整可能影响 `system_clock` 的精度，所以如果你关心时钟单调性，优先用 `steady_clock`。

## 生产者-消费者模式：有界队列

条件变量最经典的应用场景就是生产者-消费者模式（Producer-Consumer Pattern）。我们来写一个完整的有界阻塞队列——生产者往队列里塞数据，满了就阻塞；消费者从队列里取数据，空了就阻塞。这个例子综合运用了 mutex、condition_variable 的等待-通知机制和谓词写法。

先定义队列的基本结构：

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <thread>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity)
    {}

    // 生产者调用：向队列放入元素，满了就阻塞等待
    void push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        not_empty_.notify_one();
    }

    // 消费者调用：从队列取出元素，空了就阻塞等待
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
    std::condition_variable not_full_;   // 队列不满时通知生产者
    std::condition_variable not_empty_;  // 队列不空时通知消费者
};
```

我们来逐步拆解这个实现。队列内部维护了两个条件变量：`not_full_` 用于生产者等待（队列满了就等，有人消费了就通知），`not_empty_` 用于消费者等待（队列空了就等，有人生产了就通知）。这种双条件变量的设计比单条件变量更精确——它避免了不必要的唤醒：生产者只唤醒消费者（`not_empty_`），消费者只唤醒生产者（`not_full_`），各管各的。

`push` 方法的逻辑是：先获取 mutex，然后用带谓词的 `wait` 等待队列不满。`wait` 返回时我们保证 `queue_.size() < capacity_`（因为谓词为 `true`），所以可以安全地 push。push 完后调用 `not_empty_.notify_one()` 通知一个等待中的消费者。`pop` 方法的逻辑对称：等待队列不空，取出元素，通知生产者。

注意 `push` 和 `pop` 中 notify 的时候锁还被持有——这没有问题，而且有时候还是一种优化。notify 本身不需要等待对方响应，它只是把等待队列中的线程移到 mutex 的等待队列上。等到当前线程释放锁（`unique_lock` 析构）后，被唤醒的线程才能获取锁继续执行。所以 notify 时持不持锁在正确性上没有区别，但在某些平台上，在持锁状态下 notify 可以减少一次不必要的上下文切换。

现在来使用这个队列：

```cpp
int main()
{
    constexpr std::size_t kQueueCapacity = 10;
    BoundedQueue<int> queue(kQueueCapacity);

    // 生产者线程
    std::thread producer([&queue]() {
        for (int i = 1; i <= 20; ++i) {
            queue.push(i);
            std::cout << "Produced: " << i << "\n";
        }
    });

    // 消费者线程
    std::thread consumer([&queue]() {
        for (int i = 1; i <= 20; ++i) {
            int value = queue.pop();
            std::cout << "Consumed: " << value << "\n";
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

队列容量是 10，生产者要生产 20 个元素，所以中间必然会满——生产者在第 11 个元素时阻塞，等消费者取走元素后才能继续。消费者的节奏则取决于生产者的产出速度——如果生产者跟不上，消费者会在 `pop` 中等待。两个线程就这样通过条件变量协调前进的节奏。

## notify_all 与 notify_one 的选择策略

上面的有界队列例子中，我们用的是 `notify_one()`——每次只唤醒一个等待线程。但在某些场景下，我们需要 `notify_all()` 来唤醒所有等待线程。选择哪个取决于"条件变化的性质"。

`notify_one()` 适合"每次通知只让一个线程继续"的场景。生产者-消费者队列就是典型代表——每个 push 只需要唤醒一个消费者来取，唤醒多个消费者没有意义（只有一个元素可取，其他人取不到还得回去等）。`notify_one()` 的优势是减少不必要的唤醒：只叫醒一个线程，其他线程继续睡，节省上下文切换的开销。

`notify_all()` 适合"条件变化可能让多个等待线程同时满足条件"的场景。一个经典的例子是**线程池的 shutdown**：当你设置了一个 `shutdown` 标志并调用 `notify_all()`，所有等待任务的线程都需要醒来检查这个标志，然后各自退出。另一个例子是**屏障（barrier）模式**——所有线程都需要等到某个条件成立才一起继续，条件改变时需要通知所有人。

有个常见的误区是认为 `notify_all` 总是安全的所以总是用它。确实，`notify_all` 在正确性上不会比 `notify_one` 差——所有等待线程最终都会醒来检查条件。但性能上差别很大：如果 10 个线程在等，`notify_all` 会唤醒所有 10 个，它们会竞争同一把 mutex，最终只有 1 个能拿到锁并通过条件检查，其他 9 个白跑一趟。所以"能用 `notify_one` 就别用 `notify_all`"是一个合理的性能优化原则——前提是你确定通知只关联一个等待线程。

## std::condition_variable_any：通用条件变量

到目前为止我们一直在用 `std::condition_variable`，它只接受 `std::unique_lock<std::mutex>`。但有时候我们可能需要跟其他锁类型搭配——比如 `std::shared_lock<std::shared_mutex>`（下一篇会详细讲）。这时候就需要 `std::condition_variable_any` 了。

它的接口和 `std::condition_variable` 完全一致，只是模板化的 `wait` 可以接受任何满足 Lockable 要求的锁。使用方式几乎没有学习成本，直接把 `condition_variable` 替换成 `condition_variable_any` 就行。代价呢？它的内部实现通常需要一个额外的 mutex 来保护内部的等待队列（因为 `condition_variable` 可以利用 `unique_lock<mutex>` 的内部结构做优化，而 `condition_variable_any` 不了解外部锁的内部实现），所以在性能上会稍逊一筹。如果你的场景只需要 `unique_lock<std::mutex>`，那就老老实实用 `condition_variable`。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 练习

### 练习 1：线程安全的倒计时器

实现一个 `CountdownEvent` 类，行为类似 C# 的 `ManualResetEvent` 或 Java 的 `CountDownLatch`。它有一个内部计数器，初始值为 N。线程可以调用 `wait()` 阻塞等待计数器归零，其他线程调用 `signal()` 将计数器减 1。当计数器减到 0 时，所有等待的线程都应该被唤醒。

要求：

- 使用 `std::mutex` 和 `std::condition_variable`
- `wait()` 使用带谓词的版本
- `signal()` 中思考该用 `notify_one()` 还是 `notify_all()`

提示：当计数器从 1 变成 0 的那一刻，所有在 `wait()` 上阻塞的线程的条件都同时满足了——这是 `notify_all()` 的典型场景。

### 练习 2：扩展有界队列支持 try_pop_for

在本文的 `BoundedQueue` 基础上，增加一个 `try_pop_for(duration)` 方法：尝试在指定时间内从队列取出元素。如果在超时前成功取出，返回 `std::optional<T>` 包含值；如果超时了，返回 `std::nullopt`。

提示：使用 `wait_for` 的带谓词版本，检查返回值判断是超时还是成功。注意超时返回后线程是否安全——因为 `optional` 的 `nullopt` 明确告诉调用者"没有取到"，调用者可以决定重试还是放弃。

### 练习 3：复现丢失唤醒

写一个程序，故意构造"先 notify 后 wait"的时序。不用带谓词的 `wait`，观察程序是否会永久阻塞（大概率会，取决于调度）。然后在 `wait` 中加上谓词，确认即使通知先发出，程序也能正常退出。这个练习的目的是让你亲身感受丢失唤醒的危险，以及谓词 `wait` 为什么是必须的。

## 参考资源

- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::condition_variable::wait -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable/wait)
- [Condition variable -- Wikipedia（虚假唤醒的 POSIX 标准讨论）](https://en.wikipedia.org/wiki/Monitor_(synchronization)#Condition_variables)
- [Why do spurious wakeups happen? -- StackOverflow](https://stackoverflow.com/questions/8594591/why-does-pthreads-cond-wait-have-spurious-wakeups)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 4](https://www.oreilly.com/library/view/c-concurrency-in/9781617294643/)
