#pragma once

#include <cstddef>
#include <functional>

namespace lab1 {

// MS6: C++20 同步原语实践。
// 不是要你造轮子,而是用 std::latch / std::barrier / std::counting_semaphore 实现三种经典并发模式。
// 这三个函数各自用一种原语 —— 实现时想清楚"为什么是这个原语"。

/// fork-join:派发 n_workers 个任务到线程,std::latch 等全部完成,主线程汇总。
/// 每个 task 拿到自己的 worker_id(0..n_workers-1),返回一个 std::size_t。
/// 函数返回所有 task 结果之和。
///
/// 用 std::latch —— 为什么? 因为这是一次性的"等 N 个任务全完成"(countdown 到 0),
/// 而 barrier 是可复用的、semaphore 是限流的,语义不对。
std::size_t fork_join_sum(std::size_t n_workers, std::function<std::size_t(std::size_t)> task);

/// 两阶段并行:每个 worker 先把 per_worker_value 放进自己的槽位(phase 1),
/// barrier 同步(所有 worker 都完成 phase 1 才进 phase 2),再由主线程汇总(phase 2)。
/// 返回所有 worker 贡献的总和 = n_workers * per_worker_value。
///
/// 用 std::barrier —— 为什么? 因为这是"多阶段、阶段间要同步"的场景,barrier 可复用且能指定
/// completion 函数。
std::size_t two_phase_sum(std::size_t n_workers, std::size_t per_worker_value);

/// 资源池限流:n_callers 个线程都尝试进入临界区,但同一时刻最多 max_concurrent 个能进。
/// 返回观测到的"峰值并发数"(应该 == max_concurrent, 不会超过)。
///
/// 用 std::counting_semaphore —— 为什么? 因为这是"允许 N 个并发"的计数信号量典型场景。
std::size_t measure_max_concurrency(std::size_t n_callers, std::size_t max_concurrent);

} // namespace lab1
