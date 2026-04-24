#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>

namespace fs = std::filesystem;

/// @brief 执行日志轮转
/// @param log_path 日志文件路径
/// @param max_size 最大文件大小（字节）
/// @param max_backups 最大备份数量
void rotate_log(const fs::path& log_path,
                std::uintmax_t max_size,
                int max_backups) {
    std::error_code ec;

    // 检查日志文件是否存在且超过大小限制
    if (!fs::exists(log_path, ec) || ec) return;
    auto size = fs::file_size(log_path, ec);
    if (ec || size < max_size) return;

    auto stem = log_path.stem().string();
    auto ext = log_path.extension().string();
    auto parent = log_path.parent_path();

    // 收集已有的备份文件
    std::vector<fs::path> backups;
    for (int i = 1; i <= max_backups + 1; ++i) {
        auto backup_name = stem + "." + std::to_string(i) + ext;
        auto backup_path = parent / backup_name;
        if (fs::exists(backup_path)) {
            backups.push_back(backup_path);
        }
    }

    // 删除超出数量限制的旧备份
    std::sort(backups.begin(), backups.end());
    while (static_cast<int>(backups.size()) >= max_backups) {
        fs::remove(backups.back(), ec);
        backups.pop_back();
    }

    // 将现有备份序号 +1
    for (int i = static_cast<int>(backups.size()); i >= 1; --i) {
        auto old_name = stem + "." + std::to_string(i) + ext;
        auto new_name = stem + "." + std::to_string(i + 1) + ext;
        fs::rename(parent / old_name, parent / new_name, ec);
    }

    // 将当前日志重命名为 .1 备份
    auto first_backup = parent / (stem + ".1" + ext);
    fs::rename(log_path, first_backup, ec);

    // 创建新的空日志文件
    std::ofstream(log_path).close();

    std::cout << "日志轮转完成: " << log_path << "\n";
}

int main() {
    // 示例：当 app.log 超过 1MB 时轮转，最多保留 5 个备份
    rotate_log("/tmp/app.log", 1024 * 1024, 5);
    return 0;
}
