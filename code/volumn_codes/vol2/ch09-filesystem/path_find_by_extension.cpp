#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

/// @brief 在指定目录下查找所有匹配扩展名的文件
/// @param dir 搜索目录
/// @param ext 目标扩展名（如 ".cpp"）
/// @return 匹配的文件路径列表
std::vector<fs::path> find_by_extension(const fs::path& dir,
                                          const std::string& ext) {
    std::vector<fs::path> results;
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "目录不存在或不是目录: " << dir << "\n";
        return results;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            auto path_ext = entry.path().extension().string();
            // 统一转小写比较，应对 .CPP 和 .cpp
            std::transform(path_ext.begin(), path_ext.end(),
                           path_ext.begin(), ::tolower);
            std::string lower_ext = ext;
            std::transform(lower_ext.begin(), lower_ext.end(),
                           lower_ext.begin(), ::tolower);
            if (path_ext == lower_ext) {
                results.push_back(entry.path());
            }
        }
    }

    // 按文件名排序
    std::sort(results.begin(), results.end());
    return results;
}

int main() {
    auto cpp_files = find_by_extension(".", ".md");
    for (const auto& f : cpp_files) {
        std::cout << f.filename().string() << "\n";
    }
    return 0;
}
