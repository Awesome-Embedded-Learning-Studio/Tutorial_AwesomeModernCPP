#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace lab0 {

/// 单 worker 的统计汇总。Milestone 4 用它做线程局部统计，主线程汇总。
/// 纯数据结构，非练习重点——已提供完整定义，你不用修改。
struct WorkerStats {
    std::size_t files_scanned = 0;
    std::uintmax_t total_bytes = 0;
    std::unordered_map<std::string, std::size_t> ext_counts; // 扩展名 → 出现次数
};

/// 把 other 累加到 target（Milestone 4 主线程汇总各 worker 结果时用）。
inline WorkerStats& operator+=(WorkerStats& target, const WorkerStats& other) {
    target.files_scanned += other.files_scanned;
    target.total_bytes += other.total_bytes;
    for (const auto& [ext, count] : other.ext_counts) {
        target.ext_counts[ext] += count;
    }
    return target;
}

} // namespace lab0
