#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <utility>
#include <vector>

#include "joining_thread.h"
#include "worker_stats.h"

namespace lab0 {

/**
 * @brief Core Scanner
 *
 */
class FileScanner {
  public:
    FileScanner(std::filesystem::path root, std::size_t num_workers)
        : root_path_(std::move(root)), num_workers_(num_workers) {}

    WorkerStats scan() {
        namespace fs = std::filesystem;

        // 主线程收集整棵目录树的 regular_file(recursive 默认不跟随 symlink)
        std::vector<fs::directory_entry> entries;
        for (const auto& entry : fs::recursive_directory_iterator(root_path_)) {
            if (entry.is_regular_file()) {
                entries.emplace_back(entry);
            }
        }

        WorkerStats stats;
        const auto files_count = entries.size();
        if (files_count == 0) {
            return stats; // 空目录直接返回
        }

        // 11 files, 3 workers -> each get 4 4 3(有余数则前面的 worker 多拿一个)
        auto each_file_cnt = files_count / num_workers_;
        if (files_count % num_workers_ != 0) {
            each_file_cnt += 1;
        }
        const auto worker_count = (files_count + each_file_cnt - 1) / each_file_cnt; // ceil

        // MS4: 每 worker 一个局部 WorkerStats, 写回 results[worker_id] 独立槽位。
        //      不同 worker 写不同槽位 -> 无竞争, 不再需要 mutex。
        //      (必须预分配大小, 否则 emplace 触发 reallocate 会和并发 worker 抢内存)
        std::vector<WorkerStats> results(worker_count);

        {
            // MS2: JoiningThread 替换裸 std::thread, 作用域结束自动 join。
            std::vector<JoiningThread> workers;
            workers.reserve(worker_count);
            for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
                const auto start = worker_id * each_file_cnt;
                const auto end = std::min(files_count, start + each_file_cnt);
                workers.emplace_back([worker_id, start, end, &entries, &results]() {
                    WorkerStats local;
                    for (std::size_t index = start; index < end; ++index) {
                        local.files_scanned++;
                        local.total_bytes += entries[index].file_size();
                        local.ext_counts[entries[index].path().extension()]++;
                    }
                    results[worker_id] = std::move(local);
                });
            }
        } // <- workers 在此析构 join; 必须在汇总 results 之前(MS4 踩坑:join 时机)

        for (const auto& s : results) {
            stats += s;
        }
        return stats;
    }

  private:
    std::filesystem::path root_path_;
    std::size_t num_workers_;
};

} // namespace lab0
