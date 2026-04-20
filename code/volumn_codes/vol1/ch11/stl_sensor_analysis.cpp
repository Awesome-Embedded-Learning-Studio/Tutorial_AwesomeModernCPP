#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// 单条传感器读数
struct Reading {
    std::string sensor_id;
    double value;
    uint32_t timestamp;
};

/// 分析报告
struct Report {
    std::string sensor_id;
    double min_val;
    double max_val;
    double avg_val;
    std::size_t count;
};

/// 过滤异常值：按传感器分组，去掉偏离该传感器均值超过 kSigma 个标准差的数据
void filter_outliers(std::vector<Reading>& readings, double k_sigma)
{
    if (readings.empty()) {
        return;
    }

    // 按传感器分组，分别计算均值和标准差
    std::unordered_map<std::string, std::vector<double>> groups;
    for (const auto& r : readings) {
        groups[r.sensor_id].push_back(r.value);
    }

    std::unordered_map<std::string, std::pair<double, double>> stats;
    for (const auto& [id, values] : groups) {
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean = sum / static_cast<double>(values.size());

        double sq_sum = std::accumulate(values.begin(), values.end(), 0.0,
            [mean](double acc, double v) { return acc + (v - mean) * (v - mean); });
        double stddev = std::sqrt(sq_sum / static_cast<double>(values.size()));

        stats[id] = {mean, stddev};
    }

    // remove-erase 删除异常值
    auto it = std::remove_if(readings.begin(), readings.end(),
        [&](const Reading& r) {
            const auto& [mean, stddev] = stats[r.sensor_id];
            return std::abs(r.value - mean) > k_sigma * stddev;
        });
    readings.erase(it, readings.end());
}

/// 为每个传感器生成分析报告
std::vector<Report> generate_reports(std::vector<Reading>& readings)
{
    // 用 unordered_map 按传感器分组（不需要有序遍历，O(1) 查找）
    std::unordered_map<std::string, std::vector<Reading>> groups;
    groups.reserve(16);  // 预分配，减少 rehash

    for (auto& r : readings) {
        groups[r.sensor_id].push_back(std::move(r));
    }

    std::vector<Report> reports;
    reports.reserve(groups.size());

    for (auto& [id, recs] : groups) {
        if (recs.empty()) {
            continue;
        }

        // 按时间戳排序
        std::sort(recs.begin(), recs.end(),
            [](const Reading& a, const Reading& b) {
                return a.timestamp < b.timestamp;
            });

        // 用 STL 算法计算统计量
        auto [min_it, max_it] = std::minmax_element(recs.begin(), recs.end(),
            [](const Reading& a, const Reading& b) {
                return a.value < b.value;
            });

        double sum = std::accumulate(recs.begin(), recs.end(), 0.0,
            [](double acc, const Reading& r) { return acc + r.value; });

        reports.push_back({
            id,
            min_it->value,
            max_it->value,
            sum / static_cast<double>(recs.size()),
            recs.size()
        });
    }

    // 按传感器 ID 排序输出，保证结果稳定
    std::sort(reports.begin(), reports.end(),
        [](const Report& a, const Report& b) { return a.sensor_id < b.sensor_id; });

    return reports;
}

/// 去除重复读数（同一传感器、同一时间戳视为重复）
void deduplicate(std::vector<Reading>& readings)
{
    // 用 unordered_set 记录已见过的 (sensor_id, timestamp) 组合
    struct Key {
        std::string sensor_id;
        uint32_t timestamp;
    };

    // 自定义哈希和相等比较——unordered_set 必需
    struct KeyHash {
        std::size_t operator()(const Key& k) const
        {
            auto h1 = std::hash<std::string>{}(k.sensor_id);
            auto h2 = std::hash<uint32_t>{}(k.timestamp);
            return h1 ^ (h2 << 1);  // 简单组合哈希
        }
    };

    struct KeyEqual {
        bool operator()(const Key& a, const Key& b) const
        {
            return a.sensor_id == b.sensor_id && a.timestamp == b.timestamp;
        }
    };

    std::unordered_set<Key, KeyHash, KeyEqual> seen;
    seen.reserve(readings.size());

    auto it = std::remove_if(readings.begin(), readings.end(),
        [&seen](const Reading& r) {
            Key k{r.sensor_id, r.timestamp};
            if (seen.count(k)) {
                return true;  // 重复，标记删除
            }
            seen.insert(k);
            return false;
        });
    readings.erase(it, readings.end());
}

int main()
{
    // 模拟传感器数据——包含重复和异常值
    std::vector<Reading> readings = {
        {"temp-01", 22.5, 1001},
        {"temp-01", 22.7, 1002},
        {"temp-01", 22.5, 1001},  // 重复
        {"temp-01", 85.0, 1003},  // 异常值
        {"temp-01", 22.9, 1004},
        {"temp-01", 22.6, 1005},
        {"temp-01", 23.0, 1006},
        {"press-01", 1013.2, 1001},
        {"press-01", 1013.5, 1002},
        {"press-01", 1013.2, 1001},  // 重复
        {"press-01", 12.0, 1003},    // 异常值
        {"press-01", 1013.8, 1004},
        {"press-01", 1013.0, 1005},
        {"press-01", 1013.6, 1006},
    };

    std::cout << "=== Raw readings: " << readings.size() << " ===\n";

    // 第一步：去重
    deduplicate(readings);
    std::cout << "After dedup: " << readings.size() << "\n";

    // 第二步：过滤异常值（2 倍标准差）
    filter_outliers(readings, 2.0);
    std::cout << "After outlier filter: " << readings.size() << "\n";

    // 第三步：生成分析报告
    auto reports = generate_reports(readings);

    std::cout << "\n=== Analysis Reports ===\n";
    for (const auto& r : reports) {
        std::cout << "  [" << r.sensor_id << "] "
                  << "min=" << r.min_val << ", max=" << r.max_val
                  << ", avg=" << r.avg_val
                  << ", n=" << r.count << "\n";
    }

    return 0;
}
