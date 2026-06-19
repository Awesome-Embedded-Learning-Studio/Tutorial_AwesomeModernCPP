#pragma once

#include <cstddef>
#include <filesystem>
#include <utility>

#include "worker_stats.h"

namespace lab0 {

/// 并行文件扫描器：把 root 下所有文件分片给 N 个 worker 线程扫描，汇总统计。
///
/// 这是 Lab 0 的主载体，Milestone 1/3/4 都围绕 scan() 演进（接口不变，内部实现替换）：
///   - MS1：裸 std::thread + 全局 atomic 统计，跑通分片
///   - MS2：改用 JoiningThread（替换 scan() 里的裸 thread）
///   - MS3：审查参数捕获，消除悬空引用（值捕获 / move）
///   - MS4：全局 atomic 换成每 worker 的局部 WorkerStats，主线程汇总
class FileScanner {
  public:
    FileScanner(std::filesystem::path root, std::size_t num_workers)
        : root_path_(std::move(root)), num_workers_(num_workers) {}

    /// 扫描 root 目录，返回所有文件的汇总统计。
    ///
    // TODO(MS1): 用 recursive_directory_iterator 收集所有 regular_file 路径，
    //            按 num_workers_ 分片，每 worker 一段，裸 std::thread 并行扫描，
    //            全局 std::atomic 累计 files_scanned / total_bytes。
    // TODO(MS2): 把 std::thread 换成 lab0::JoiningThread，删除手工 join 循环。
    // TODO(MS3): 审查 worker 的参数捕获——确保每 worker 拿到独立的文件列表副本
    //            （值捕获或 std::move），不捕获可能悬空的引用；worker_id 用值捕获。
    // TODO(MS4): 去掉全局 atomic，每 worker 用局部 WorkerStats 统计，写回
    //            预分配的 std::vector<WorkerStats> 对应槽位，主线程汇总。
    WorkerStats scan();

  private:
    std::filesystem::path root_path_;
    std::size_t num_workers_;
};

} // namespace lab0
