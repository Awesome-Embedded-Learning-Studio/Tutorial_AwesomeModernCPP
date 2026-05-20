/*
 * 演示：Go 风格 Channel 的 CSP 并发模型实现
 *
 * 背景：文章 ch07 "Actor 模型与 CSP Channel" 涵盖——
 *       1. CSP（Communicating Sequential Processes）核心思想：
 *          不要通过共享内存来通信，而要通过通信来共享内存
 *       2. BufferedChannel：带缓冲区的 channel，满时 send 阻塞
 *       3. UnbufferedChannel：零缓冲区的 channel（rendezvous），
 *          发送方和接收方必须同时就绪才能完成一次传递
 *       4. try_send / try_receive：非阻塞的试探性操作
 *       5. close() 语义：关闭后已缓冲的数据仍可取出（drain）
 *       6. 三阶段流水线：generator -> transformer -> consumer
 *
 * 预期结果：
 *   程序演示带缓冲/无缓冲 channel 的行为差异，
 *   并构建一个三阶段流水线展示 CSP 模型的组合能力。
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/02_channel_csp
 *
 * 编译器：GCC 12+ | Clang 15+ | MSVC 19.3+
 * 平台：x86-64 Linux / macOS / Windows
 * C++ 标准：C++17
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// 线程安全输出的互斥量
std::mutex g_cout_mutex;

/// 线程安全的打印函数
void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << msg << std::endl;
}

// ============================================================
// 第一部分：BufferedChannel —— 带缓冲区的 Channel
// ============================================================
//
// BufferedChannel 是最接近 Go 的 chan T 的实现：
//   - 内部维护一个固定容量的队列
//   - send() 在队列满时阻塞，直到有空间腾出
//   - receive() 在队列空时阻塞，直到有新数据
//   - close() 关闭后，已入队的数据仍可取出（drain 语义）
//
// 关键区别于普通的生产者-消费者队列：
//   channel 是一等公民，可以在线程之间传递、组合，
//   形成复杂的并发拓扑。

/// 带缓冲区的 channel，类似 Go 的 chan T（带容量）
template <typename T> class BufferedChannel {
  public:
    /// 构造时指定缓冲区容量（必须 > 0）
    explicit BufferedChannel(std::size_t capacity) : capacity_(capacity), closed_(false) {}

    ~BufferedChannel() { close(); }

    // 禁止拷贝——channel 代表一个共享的通信端点
    BufferedChannel(const BufferedChannel&) = delete;
    BufferedChannel& operator=(const BufferedChannel&) = delete;

    /// 阻塞式发送：缓冲区满时等待，直到有空间或 channel 被关闭
    /// @return true 发送成功，false channel 已关闭
    bool send(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待缓冲区有空间（或 channel 被关闭）
        not_full_.wait(lock, [this] { return buffer_.size() < capacity_ || closed_; });

        if (closed_) {
            return false;
        }

        buffer_.push(value);
        not_empty_.notify_one();
        return true;
    }

    /// 阻塞式接收：缓冲区空时等待，直到有数据或 channel 关闭且排空
    /// @return nullopt 表示 channel 已关闭且完全排空
    std::optional<T> receive() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !buffer_.empty() || closed_; });

        if (buffer_.empty()) {
            // 关闭且排空——不会再有新数据了
            return std::nullopt;
        }

        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_.notify_one();
        return value;
    }

    /// 非阻塞发送：缓冲区满时立即返回 false
    /// 适合"发不进去就算了"的场景（轮询式生产者）
    bool try_send(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || buffer_.size() >= capacity_) {
            return false;
        }
        buffer_.push(value);
        not_empty_.notify_one();
        return true;
    }

    /// 非阻塞接收：缓冲区空时立即返回 nullopt
    std::optional<T> try_receive() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) {
            return std::nullopt;
        }
        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_.notify_one();
        return value;
    }

    /// 关闭 channel：不再接受新数据，已缓冲的数据仍可取出
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
        }
        // 唤醒所有在等待的发送方和接收方
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    /// 查询 channel 是否已关闭
    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    /// 查询缓冲区中当前的数据量（快照，非精确）
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

  private:
    std::size_t capacity_;
    bool closed_;
    std::queue<T> buffer_;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};

// ============================================================
// 第二部分：UnbufferedChannel —— 无缓冲区 Channel（Rendezvous）
// ============================================================
//
// UnbufferedChannel 就是 Go 里的 unbuffered channel：
//   make(chan int) 而不是 make(chan int, 10)
//
// 它的 send 和 receive 必须同时就绪才能完成一次传递，
// 这就是"rendezvous"（会合）语义——
// 先到的那一方会阻塞，直到另一方也到达。
//
// 实现上有几种方案，我们选择的是：
//   - 两个 condition_variable：一个通知"有发送方在等"，
//     一个通知"有接收方在等"
//   - 一次 handoff 只传递一个值，传递完毕后双方各自解除阻塞
//
// 这种设计很精巧，也是理解 CSP "同步通信" 的关键。

/// 无缓冲区 channel（rendezvous 语义）
/// 发送方和接收方必须同时就绪才能完成一次传递
template <typename T> class UnbufferedChannel {
  public:
    UnbufferedChannel() : closed_(false), has_value_(false), ready_(false) {}

    ~UnbufferedChannel() { close(); }

    UnbufferedChannel(const UnbufferedChannel&) = delete;
    UnbufferedChannel& operator=(const UnbufferedChannel&) = delete;

    /// 阻塞式发送：等待接收方就绪，然后传递值
    /// @return true 传递成功，false channel 已关闭
    bool send(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (closed_) {
            return false;
        }

        // 把值暂存到 handoff 缓冲区
        handoff_value_ = value;
        has_value_ = true;
        ready_ = false;

        // 通知接收方：我有数据要给你
        sender_ready_.notify_one();

        // 等待接收方取走数据（rendezvous 完成）
        handshake_done_.wait(lock, [this] { return ready_ || closed_; });

        if (closed_) {
            return false;
        }

        // handoff 完成，清理状态
        has_value_ = false;
        return true;
    }

    /// 阻塞式接收：等待发送方就绪，然后取出值
    /// @return nullopt 表示 channel 已关闭
    std::optional<T> receive() {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待发送方准备好数据
        sender_ready_.wait(lock, [this] { return has_value_ || closed_; });

        if (closed_ && !has_value_) {
            return std::nullopt;
        }

        // 取走 handoff 值
        T value = std::move(handoff_value_);
        has_value_ = false;
        ready_ = true;

        // 通知发送方：我拿走了，你可以继续了
        handshake_done_.notify_one();

        return value;
    }

    /// 关闭 channel，唤醒所有等待的线程
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
        }
        sender_ready_.notify_all();
        handshake_done_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable sender_ready_;
    std::condition_variable handshake_done_;
    T handoff_value_;
    bool closed_;
    bool has_value_;
    bool ready_;
};

// ============================================================
// 第三部分：Demo 1 —— BufferedChannel 生产者-消费者
// ============================================================
//
// 最经典的 channel 用法：一个生产者往 channel 里塞数据，
// 一个消费者从 channel 里取数据。channel 的缓冲区
// 在两者之间起到了"削峰填谷"的作用。

void demo_buffered_producer_consumer() {
    safe_print("\n=== Demo 1: BufferedChannel 生产者-消费者 ===");

    // 容量为 3 的缓冲 channel
    BufferedChannel<int> channel(3);

    // 生产者线程：往 channel 发送 10 个整数
    std::thread producer([&channel]() {
        for (int i = 1; i <= 10; ++i) {
            bool ok = channel.send(i);
            if (ok) {
                safe_print("  [生产者] 发送: " + std::to_string(i));
            }
            // 模拟生产速度不均匀
            if (i % 3 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        channel.close();
        safe_print("  [生产者] 生产完毕，已关闭 channel");
    });

    // 消费者线程：从 channel 接收数据直到排空
    std::thread consumer([&channel]() {
        int sum = 0;
        while (true) {
            auto value = channel.receive();
            if (!value.has_value()) {
                break;
            }
            sum += value.value();
            safe_print("  [消费者] 接收: " + std::to_string(value.value()) +
                       "，累计 sum = " + std::to_string(sum));
        }
        safe_print("  [消费者] 消费完毕，总和 = " + std::to_string(sum));
    });

    producer.join();
    consumer.join();
}

// ============================================================
// 第四部分：Demo 2 —— try_send / try_receive 非阻塞操作
// ============================================================
//
// 并非所有场景都适合阻塞等待。有时候我们需要
// "试一下，不行就算了"——这就是 try_send / try_receive
// 的用武之地，类似 Go 的 select + default 分支。

void demo_nonblocking_operations() {
    safe_print("\n=== Demo 2: try_send / try_receive 非阻塞操作 ===");

    // 容量为 2 的小 channel
    BufferedChannel<int> channel(2);

    // 先塞满缓冲区
    safe_print("  尝试连续 try_send 5 个值（容量仅 2）...");
    for (int i = 1; i <= 5; ++i) {
        bool ok = channel.try_send(i);
        safe_print("  try_send(" + std::to_string(i) + ") = " + std::string(ok ? "true" : "false"));
    }

    safe_print("  缓冲区中有 " + std::to_string(channel.size()) + " 个值");

    // 用 try_receive 把它们取出来
    safe_print("  尝试连续 try_receive 5 次...");
    for (int i = 0; i < 5; ++i) {
        auto val = channel.try_receive();
        if (val.has_value()) {
            safe_print("  try_receive() = " + std::to_string(val.value()));
        } else {
            safe_print("  try_receive() = nullopt（缓冲区空）");
        }
    }
}

// ============================================================
// 第五部分：Demo 3 —— UnbufferedChannel（Rendezvous）行为
// ============================================================
//
// 无缓冲 channel 的精髓在于"同步"——
// 发送方和接收方必须在同一时刻"会合"才能完成传递。
// 这个 Demo 会展示：先启动的那一方会等待另一方。

void demo_unbuffered_channel() {
    safe_print("\n=== Demo 3: UnbufferedChannel（Rendezvous 会合）===");

    UnbufferedChannel<std::string> channel;

    // 发送方线程：先 sleep 一会儿，模拟"晚到"
    std::thread sender([&channel]() {
        safe_print("  [发送方] 准备发送，先睡 100ms...");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        safe_print("  [发送方] 醒了，开始发送 \"Hello, Rendezvous!\"");
        bool ok = channel.send("Hello, Rendezvous!");
        safe_print("  [发送方] send 返回: " + std::string(ok ? "true" : "false"));
    });

    // 接收方线程：立即开始等待，会先于发送方就绪
    std::thread receiver([&channel]() {
        safe_print("  [接收方] 开始等待（会先于发送方就绪）...");
        auto value = channel.receive();
        if (value.has_value()) {
            safe_print("  [接收方] 收到: \"" + value.value() + "\"");
        }
    });

    sender.join();
    receiver.join();
}

// ============================================================
// 第六部分：Demo 4 —— 三阶段流水线
// ============================================================
//
// CSP 模型最强大的地方在于"组合"：
//   generator --ch1--> transformer --ch2--> consumer
//
// 三个阶段通过 channel 连接，每个阶段都是独立的线程，
// 各自的速率由 channel 的缓冲来解耦。
// 这种"用 channel 串联处理阶段"的模式在 Go 中极为常见，
// C++ 里我们可以用同样的思路来组织并发代码。

void demo_three_stage_pipeline() {
    safe_print("\n=== Demo 4: 三阶段流水线 (CSP Pipeline) ===");
    safe_print("  generator --ch1--> transformer --ch2--> consumer");

    // 两个 channel 连接三个阶段
    auto ch1 = std::make_shared<BufferedChannel<int>>(5);
    auto ch2 = std::make_shared<BufferedChannel<std::string>>(5);

    // ---- 阶段 1: Generator（生成器）----
    // 产生一组原始数据，送入 ch1
    std::thread generator([ch1]() {
        safe_print("  [Generator] 开始生成数据...");
        for (int i = 1; i <= 8; ++i) {
            ch1->send(i);
            safe_print("  [Generator] -> ch1: " + std::to_string(i));
        }
        ch1->close();
        safe_print("  [Generator] 完成，关闭 ch1");
    });

    // ---- 阶段 2: Transformer（转换器）----
    // 从 ch1 读取数据，变换后送入 ch2
    std::thread transformer([ch1, ch2]() {
        safe_print("  [Transformer] 开始转换...");
        while (true) {
            auto input = ch1->receive();
            if (!input.has_value()) {
                break;
            }
            int val = input.value();
            // 简单的变换：平方 + 加前缀
            std::string output = std::to_string(val) + "^2 = " + std::to_string(val * val);
            ch2->send(output);
            safe_print("  [Transformer] ch1 -> ch2: \"" + output + "\"");
        }
        ch2->close();
        safe_print("  [Transformer] 完成，关闭 ch2");
    });

    // ---- 阶段 3: Consumer（消费者）----
    // 从 ch2 读取最终结果并汇总
    std::thread consumer([ch2]() {
        safe_print("  [Consumer] 开始消费...");
        std::vector<std::string> results;
        while (true) {
            auto input = ch2->receive();
            if (!input.has_value()) {
                break;
            }
            results.push_back(input.value());
            safe_print("  [Consumer] 收到: \"" + input.value() + "\"");
        }
        safe_print("  [Consumer] 共收到 " + std::to_string(results.size()) + " 条结果");
    });

    generator.join();
    transformer.join();
    consumer.join();
}

// ============================================================
// 第七部分：Demo 5 —— 多生产者 / 多消费者
// ============================================================
//
// BufferedChannel 天然支持多对多的拓扑结构——
// 多个线程同时 send 不会丢数据（有 mutex 保护），
// 多个线程同时 receive 每条消息只会被一个消费者取走。

void demo_multiple_producers_consumers() {
    safe_print("\n=== Demo 5: 多生产者 / 多消费者 ===");

    auto channel = std::make_shared<BufferedChannel<int>>(4);
    constexpr int kProducerCount = 3;
    constexpr int kItemsPerProducer = 4;

    // 用原子计数器跟踪未完成的生产者数量，
    // 最后一个完成的生产者负责关闭 channel
    auto remaining_producers = std::make_shared<std::atomic<int>>(kProducerCount);

    // 多个生产者线程（与消费者并行运行）
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducerCount; ++p) {
        producers.emplace_back([channel, remaining_producers, p]() {
            for (int i = 0; i < kItemsPerProducer; ++i) {
                int value = p * 100 + i;
                channel->send(value);
                safe_print("  [生产者-" + std::to_string(p) + "] 发送: " + std::to_string(value));
            }
            // 最后一个完成的生产者负责关闭 channel
            if (remaining_producers->fetch_sub(1) == 1) {
                channel->close();
                safe_print("  最后一个生产者完成，关闭 channel");
            }
        });
    }

    // 消费者线程与生产者并行运行，
    // 这样 channel 满时消费者可以排空缓冲区，让生产者继续
    std::thread consumer([channel]() {
        int total = 0;
        int count = 0;
        while (true) {
            auto val = channel->receive();
            if (!val.has_value()) {
                break;
            }
            ++count;
            total += val.value();
            safe_print("  [消费者] 接收: " + std::to_string(val.value()) +
                       "，累计 sum = " + std::to_string(total));
        }
        safe_print("  [消费者] 共接收 " + std::to_string(count) +
                   " 条，总和 = " + std::to_string(total));
    });

    for (auto& t : producers) {
        t.join();
    }
    consumer.join();
}

// ============================================================
// 主函数：依次运行所有 Demo
// ============================================================

int main() {
    std::cout << "===== ch07: CSP Channel 并发模型 =====\n";

    demo_buffered_producer_consumer();
    demo_nonblocking_operations();
    demo_unbuffered_channel();
    demo_three_stage_pipeline();
    demo_multiple_producers_consumers();

    std::cout << "\n===== 所有 Demo 运行完毕 =====" << std::endl;
    return 0;
}
