/*
 * 演示：基于 Actor 模型的轻量级并发框架
 *
 * 背景：文章 ch07 "Actor 模型与 CSP Channel" 涵盖——
 *       1. Actor 模型的核心思想：每个 Actor 拥有私有状态，通过消息通信
 *       2. 基于 std::variant 的消息类型设计
 *       3. ThreadSafeQueue 作为 Actor 的 mailbox（信箱）
 *       4. Actor 基类：消息循环、虚函数分派、生命周期管理
 *       5. ActorSystem：Actor 的注册、路由、统一销毁
 *
 * 预期结果：
 *   程序创建多个 Actor，通过消息传递进行交互，
 *   展示 Actor 模型中"不共享内存、靠消息通信"的并发范式。
 *
 * 编译命令：
 *   cmake -B build && cmake --build build
 *   ./build/01_actor_framework
 *
 * 编译器：GCC 12+ | Clang 15+ | MSVC 19.3+
 * 平台：x86-64 Linux / macOS / Windows
 * C++ 标准：C++17
 */

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// 线程安全输出的互斥量
std::mutex g_cout_mutex;

/// 线程安全的打印函数，避免多线程输出交错
void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << msg << std::endl;
}

// ============================================================
// 第一部分：消息类型定义
// ============================================================
//
// Actor 模型的核心是"一切靠消息"，所以我们先定义
// Actor 之间可以传递的消息类型。这里用 std::variant
// 来实现类型安全的消息联合体，比传统面向对象的
// 继承+dynamic_cast 方案更轻量、更安全。

/// 携带文本内容的消息，用于 Actor 间的通用通信
struct StringMessage {
    std::string content;
};

/// 请求 Actor 增加内部计数器，可指定增量
struct IncrementMessage {
    int delta;
};

/// 查询 Actor 当前状态，需要提供回复目标
struct QueryMessage {
    int reply_to_actor_id;
};

/// 通知 Actor 停止消息循环并退出
struct StopMessage {};

/// 所有消息类型的联合体——这就是我们的 "Message"
using Message = std::variant<StringMessage, IncrementMessage, QueryMessage, StopMessage>;

// ============================================================
// 第二部分：std::visit 的 overload 辅助工具
// ============================================================
//
// C++17 的 std::visit 需要一个"可对所有变体类型调用"的
// 访问者对象。overload 惯用法让我们用一组 lambda 来构建
// 这个访问者，是 C++17 中最常用的 variant 模式匹配技巧。

/// 辅助模板：从多个 lambda 构造一个重载集（overload pattern）
template <typename... Ts> struct Overload : Ts... {
    using Ts::operator()...;
};

/// CTAD 推导指引，让编译器自动推导模板参数
template <typename... Ts> Overload(Ts...) -> Overload<Ts...>;

// ============================================================
// 第三部分：ThreadSafeQueue —— Actor 的 Mailbox
// ============================================================
//
// 每个 Actor 拥有一个 ThreadSafeQueue 作为消息信箱。
// 生产者（其他 Actor 或外部线程）往里面 push 消息，
// Actor 自己的内部线程循环 pop 消息来处理。
// 关闭后的队列支持"排干"语义：已入队的消息仍可取出。

/// 线程安全队列，用作 Actor 的消息信箱（mailbox）
template <typename T> class ThreadSafeQueue {
  public:
    ThreadSafeQueue() = default;

    /// 向队列中放入一条消息（可被已关闭的队列拒绝）
    /// @return true 表示入队成功
    bool push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            return false;
        }
        queue_.push(std::move(value));
        cond_.notify_one();
        return true;
    }

    /// 从队列中取出一条消息；队列为空时阻塞等待
    /// @return nullopt 表示队列已关闭且已排空
    std::optional<T> wait_and_pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || closed_; });

        if (queue_.empty()) {
            // 队列已关闭且为空——排干完毕
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    /// 关闭队列，不再接受新消息，但已入队的消息仍可取出
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cond_.notify_all();
    }

    /// 查询队列是否已关闭
    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::queue<T> queue_;
    bool closed_ = false;
};

// ============================================================
// 第四部分：前向声明与 ActorId 类型
// ============================================================

// ActorSystem 需要知道 Actor 的接口，Actor 需要通过
// ActorSystem 来路由消息，所以这里先做前向声明。

class Actor;
class ActorSystem;

/// Actor 的唯一标识符，就是一个 int
using ActorId = int;

/// 无效的 Actor ID，用于标记"无需回复"等场景
constexpr ActorId kInvalidActorId = -1;

// ============================================================
// 第五部分：Actor 基类
// ============================================================
//
// Actor 基类封装了消息循环的核心逻辑：
//   1. 内部线程不断从 mailbox 取消息
//   2. 取到消息后分派给 on_message() 虚函数
//   3. 收到 StopMessage 后自动退出循环
// 子类只需重写 on_message() 就能定义自己的行为。

/// Actor 基类：封装消息循环与生命周期管理
class Actor {
  public:
    Actor(ActorId id, ActorSystem& system)
        : id_(id), system_(system), running_(false), stopped_(false) {}

    /// 虚析构：停止线程、释放资源
    virtual ~Actor() { stop(); }

    // 禁止拷贝与移动——Actor 持有内部线程，不适合拷贝
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    /// 启动 Actor 的内部消息循环线程
    void start() {
        running_.store(true);
        thread_ = std::thread(&Actor::message_loop, this);
    }

    /// 停止 Actor：关闭信箱并等待线程结束
    void stop() {
        // 使用独立的 stopped_ 标志防止重复 stop
        bool expected = false;
        if (!stopped_.compare_exchange_strong(expected, true)) {
            return; // 已经 stop 过了
        }
        running_.store(false);
        // 关闭信箱，让 message_loop 中的 wait_and_pop 解除阻塞
        mailbox_.close();
        if (thread_.joinable()) {
            thread_.join();
        }
        safe_print("[Actor " + std::to_string(id_) + "] 已停止");
    }

    /// 向此 Actor 的信箱中投递一条消息
    void receive(Message msg) {
        if (!mailbox_.push(std::move(msg))) {
            safe_print("[Actor " + std::to_string(id_) + "] 信箱已关闭，消息被丢弃");
        }
    }

    ActorId get_id() const { return id_; }

  protected:
    /// 子类重写此方法来处理收到的消息
    virtual void on_message(const Message& msg) = 0;

    /// 向目标 Actor 发送消息（通过 ActorSystem 路由）
    void send(ActorId target, Message msg);

    /// 获取所属的 ActorSystem 引用
    ActorSystem& get_system() { return system_; }

    ActorId id_;
    ActorSystem& system_;

  private:
    /// 消息循环：不断从信箱取出消息并分派
    void message_loop() {
        safe_print("[Actor " + std::to_string(id_) + "] 开始消息循环");

        while (running_.load()) {
            auto msg = mailbox_.wait_and_pop();
            if (!msg.has_value()) {
                // 信箱已关闭且排空，退出循环
                break;
            }

            // 用 overload 模式匹配消息类型，
            // StopMessage 直接终止循环，其余交给子类处理
            std::visit(Overload{[this](const StopMessage&) {
                                    safe_print("[Actor " + std::to_string(id_) +
                                               "] 收到 StopMessage");
                                    running_.store(false);
                                },
                                [this](const auto& other) { on_message(other); }},
                       msg.value());
        }

        safe_print("[Actor " + std::to_string(id_) + "] 消息循环结束");
    }

    ThreadSafeQueue<Message> mailbox_;
    std::atomic<bool> running_;
    std::atomic<bool> stopped_;
    std::thread thread_;
};

// ============================================================
// 第六部分：ActorSystem —— Actor 的注册中心与消息路由器
// ============================================================
//
// ActorSystem 负责管理所有 Actor 的生命周期：
//   - spawn() 创建并注册一个 Actor，返回其 ID
//   - send() 根据 ActorId 查找目标 Actor 并投递消息
//   - shutdown() 依次停止所有 Actor
// 这个设计把"找谁发消息"和"怎么发消息"解耦了，
// Actor 本身不需要知道其他 Actor 的具体位置。

/// Actor 系统：管理 Actor 的创建、路由与销毁
class ActorSystem {
  public:
    ActorSystem() : next_id_(0) {}

    ~ActorSystem() { shutdown(); }

    /// 创建并注册一个 Actor，返回分配的 ActorId
    /// @tparam ActorT 具体的 Actor 子类类型
    /// @tparam Args 构造参数类型
    template <typename ActorT, typename... Args> ActorId spawn(Args&&... args) {
        std::lock_guard<std::mutex> lock(actors_mutex_);
        ActorId id = next_id_++;
        // 把 ActorT 的构造参数原样转发，
        // 第一个参数始终是 id，第二个是 *this
        auto actor = std::make_unique<ActorT>(id, *this, std::forward<Args>(args)...);
        actor->start();
        actors_[id] = std::move(actor);
        safe_print("[ActorSystem] spawn Actor " + std::to_string(id));
        return id;
    }

    /// 根据 ActorId 向目标 Actor 投递消息
    void send(ActorId target, Message msg) {
        std::lock_guard<std::mutex> lock(actors_mutex_);
        auto it = actors_.find(target);
        if (it != actors_.end()) {
            it->second->receive(std::move(msg));
        } else {
            safe_print("[ActorSystem] Actor " + std::to_string(target) + " 不存在，消息丢弃");
        }
    }

    /// 停止所有 Actor 并清空注册表
    void shutdown() {
        std::lock_guard<std::mutex> lock(actors_mutex_);
        for (auto& [id, actor] : actors_) {
            actor->stop();
        }
        actors_.clear();
        safe_print("[ActorSystem] 所有 Actor 已关闭");
    }

  private:
    std::mutex actors_mutex_;
    std::unordered_map<ActorId, std::unique_ptr<Actor>> actors_;
    ActorId next_id_;
};

// Actor::send 的实现——放在这里是因为 ActorSystem 需要先定义
void Actor::send(ActorId target, Message msg) {
    system_.send(target, std::move(msg));
}

// ============================================================
// 第七部分：CounterActor —— 一个具体的 Actor 实现
// ============================================================
//
// CounterActor 维护一个内部计数器，能响应三种消息：
//   - IncrementMessage：增加计数器
//   - QueryMessage：将当前计数值发回给请求者
//   - StringMessage：打印收到的文本
// 这个例子展示了 Actor 的典型特征——
// 私有状态（counter_）没有任何锁保护，因为只有
// Actor 自己的内部线程会访问它。

/// 计数器 Actor：维护内部计数，响应查询与增量消息
class CounterActor : public Actor {
  public:
    CounterActor(ActorId id, ActorSystem& system, std::string name)
        : Actor(id, system), name_(std::move(name)), counter_(0) {}

  protected:
    void on_message(const Message& msg) override {
        // 用 overload 模式匹配各种消息类型
        std::visit(
            Overload{[this](const StringMessage& m) {
                         safe_print("  [" + name_ + "] 收到字符串: \"" + m.content + "\"");
                     },
                     [this](const IncrementMessage& m) {
                         counter_ += m.delta;
                         safe_print("  [" + name_ + "] 计数器 += " + std::to_string(m.delta) +
                                    "，当前值 = " + std::to_string(counter_));
                     },
                     [this](const QueryMessage& m) {
                         safe_print("  [" + name_ +
                                    "] 查询请求，当前计数 = " + std::to_string(counter_));
                         // 将计数值作为 StringMessage 发回给请求者
                         send(m.reply_to_actor_id,
                              StringMessage{name_ + " 的计数为 " + std::to_string(counter_)});
                     },
                     [](const StopMessage&) {
                         // 已在 message_loop 中处理
                     }},
            msg);
    }

  private:
    std::string name_;
    int counter_;
};

// ============================================================
// 第八部分：LoggerActor —— 日志收集 Actor
// ============================================================

/// 日志 Actor：收集并打印所有收到的字符串消息
class LoggerActor : public Actor {
  public:
    LoggerActor(ActorId id, ActorSystem& system) : Actor(id, system), log_count_(0) {}

  protected:
    void on_message(const Message& msg) override {
        std::visit(Overload{[this](const StringMessage& m) {
                                ++log_count_;
                                safe_print("  [Logger] 日志 #" + std::to_string(log_count_) + ": " +
                                           m.content);
                            },
                            [this](const QueryMessage& m) {
                                send(m.reply_to_actor_id,
                                     StringMessage{"Logger 共记录了 " + std::to_string(log_count_) +
                                                   " 条日志"});
                            },
                            [](const auto&) {
                                // 其他消息类型忽略
                            }},
                   msg);
    }

  private:
    int log_count_;
};

// ============================================================
// 第九部分：Demo 演示
// ============================================================

/// Demo 1：单个 CounterActor 的基本消息交互
void demo_single_actor() {
    safe_print("\n=== Demo 1: 单个 CounterActor 消息交互 ===");

    ActorSystem system;

    // 创建一个计数器 Actor
    ActorId counter_id = system.spawn<CounterActor>("Counter-A");

    // 向它发送一系列消息
    system.send(counter_id, IncrementMessage{1});
    system.send(counter_id, IncrementMessage{5});
    system.send(counter_id, IncrementMessage{-3});
    system.send(counter_id, StringMessage{"你好，Counter！"});

    // 稍等一下，确保消息被处理完
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 发送 StopMessage 让它优雅退出
    system.send(counter_id, StopMessage{});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // shutdown 会自动停止所有 Actor
    system.shutdown();
}

/// Demo 2：多个 Actor 之间互相通信
void demo_multi_actor_communication() {
    safe_print("\n=== Demo 2: 多 Actor 互相通信 ===");

    ActorSystem system;

    // 创建两个计数器 Actor 和一个日志收集 Actor
    ActorId counter_a = system.spawn<CounterActor>("Alpha");
    ActorId counter_b = system.spawn<CounterActor>("Beta");
    ActorId logger = system.spawn<LoggerActor>();

    // 向两个计数器发送不同的增量
    system.send(counter_a, IncrementMessage{10});
    system.send(counter_b, IncrementMessage{20});
    system.send(counter_a, IncrementMessage{5});
    system.send(counter_b, IncrementMessage{3});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 让两个计数器向 Logger 报告自己的状态
    // QueryMessage 的 reply_to_actor_id 设为 Logger 的 ID
    safe_print("\n--- 计数器向 Logger 汇报 ---");
    system.send(counter_a, QueryMessage{logger});
    system.send(counter_b, QueryMessage{logger});

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 直接向 Logger 发送一条普通字符串消息
    system.send(logger, StringMessage{"所有 Actor 通信正常"});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    system.shutdown();
}

/// Demo 3：Actor 之间的消息接力（Ping-Pong）
//
// 这个 Demo 展示了 Actor 之间的高频消息传递。
// 两个 CounterActor 互相发送 IncrementMessage，
// 每次 delta=1，总共来回 10 轮。通过 ActorId
// 查表路由，两个 Actor 完全不需要知道对方
// 的具体实现——这就是 Actor 模型的"位置透明性"。

void demo_ping_pong() {
    safe_print("\n=== Demo 3: Actor 间的 Ping-Pong 消息接力 ===");

    ActorSystem system;

    ActorId ping_id = system.spawn<CounterActor>("Ping");
    ActorId pong_id = system.spawn<CounterActor>("Pong");

    safe_print("  开始 Ping-Pong 接力（10 轮）...");

    // 一个简单的 Ping-Pong 策略：
    // 同时给两边各发几条 IncrementMessage
    for (int i = 0; i < 5; ++i) {
        system.send(ping_id, IncrementMessage{1});
        system.send(pong_id, IncrementMessage{1});
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 查询最终状态
    system.send(ping_id, QueryMessage{kInvalidActorId});
    system.send(pong_id, QueryMessage{kInvalidActorId});

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    system.shutdown();
}

/// Demo 4：广播式消息发送
//
// 现实场景中经常需要"把同一条消息发给所有 Actor"，
// ActorSystem 虽然没提供 broadcast() 方法，
// 但我们可以手动遍历所有已知的 ActorId 来实现。

void demo_broadcast() {
    safe_print("\n=== Demo 4: 广播式消息发送 ===");

    ActorSystem system;

    // 创建 4 个 CounterActor
    std::vector<ActorId> actor_ids;
    for (int i = 0; i < 4; ++i) {
        std::string name = "Worker-" + std::to_string(i);
        actor_ids.push_back(system.spawn<CounterActor>(name));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 向所有 Actor 广播同一条消息
    safe_print("  广播 IncrementMessage{7} 给所有 Actor...");
    for (ActorId id : actor_ids) {
        system.send(id, IncrementMessage{7});
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 广播一条字符串消息
    safe_print("  广播字符串消息...");
    for (ActorId id : actor_ids) {
        system.send(id, StringMessage{"广播测试：收到请回复"});
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    system.shutdown();
}

// ============================================================
// 主函数：依次运行所有 Demo
// ============================================================

int main() {
    std::cout << "===== ch07: Actor 模型并发框架 =====\n";

    demo_single_actor();
    demo_multi_actor_communication();
    demo_ping_pong();
    demo_broadcast();

    std::cout << "\n===== 所有 Demo 运行完毕 =====" << std::endl;
    return 0;
}
