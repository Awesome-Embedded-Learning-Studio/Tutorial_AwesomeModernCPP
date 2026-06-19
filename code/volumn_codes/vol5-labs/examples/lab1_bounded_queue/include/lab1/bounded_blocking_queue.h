#pragma once

#include <chrono>
#include <cstddef>
#include <optional>

namespace lab1 {

/// 固定容量阻塞队列:生产者-消费者的核心组件。
/// **Lab 3 的 ThreadPool 会复用它做任务队列**, 所以接口要一次定稳。
///
/// Milestone 演进(接口不变, 内部实现逐步补全):
///   - MS1: 阻塞 push / pop(满则等、空则等)
///   - MS2: close() 语义(唤醒所有阻塞者; close 后 push 抛、pop 取完剩余后返 nullopt)
///   - MS3: 超时 try_push_for / try_pop_for
///   - MS4: 背压(容量与 close 后的拒绝行为, 语义在 MS2 已定, 这里验压力)
template <typename T> class BoundedBlockingQueue {
  public:
    explicit BoundedBlockingQueue(std::size_t capacity);

    /// MS1: 阻塞直到有空间放入。close 之后调用应抛 std::runtime_error。
    void push(T value);

    /// MS1: 阻塞直到能取到元素;队列 close 且已空时返回 std::nullopt(这是"正常结束"信号,
    ///      消费者据此退出循环,不是错误)。
    std::optional<T> pop();

    /// MS2: 关闭队列。必须唤醒所有正在阻塞的 push/pop。
    ///      close 之后:push 不再成功(抛),pop 仍能取完队列里剩余的元素,耗尽后返 nullopt。
    void close();

    bool is_closed() const noexcept;

    /// MS3: 超时版 push。成功放入返 true;超时或队列已 close 返 false(不抛)。
    bool try_push_for(T value, std::chrono::nanoseconds timeout);

    /// MS3: 超时版 pop。取到元素返 optional<T>;超时或 close 后队列空返 nullopt。
    std::optional<T> try_pop_for(std::chrono::nanoseconds timeout);

    /// 近似大小(并发场景下不必精确),调试和背压观测用。
    std::size_t size() const noexcept;
};

} // namespace lab1
