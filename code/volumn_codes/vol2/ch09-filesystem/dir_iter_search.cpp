#include <filesystem>
#include <vector>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

struct SearchFilter {
    std::string extension;                // 目标扩展名，空表示不过滤
    std::uintmax_t min_size = 0;          // 最小文件大小
    std::uintmax_t max_size = UINTMAX_MAX; // 最大文件大小
    int max_depth = -1;                   // 最大递归深度，-1 表示不限
};

std::vector<fs::path> search_files(const fs::path& root,
                                     const SearchFilter& filter) {
    std::vector<fs::path> results;
    std::error_code ec;

    auto options = fs::directory_options::skip_permission_denied;

    // 注：depth() 是 recursive_directory_iterator 的成员，不是 directory_entry 的
    // 因此不能使用 range-based for，需要手动使用迭代器
    for (auto it = fs::recursive_directory_iterator(root, options, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) {
            std::cerr << "遍历错误: " << ec.message() << "\n";
            ec.clear();
            continue;
        }

        // 深度过滤
        if (filter.max_depth >= 0 &&
            it.depth() > filter.max_depth) {
            it.disable_recursion_pending();
            continue;
        }

        const auto& entry = *it;

        // 只处理普通文件
        if (!entry.is_regular_file()) {
            continue;
        }

        // 扩展名过滤
        if (!filter.extension.empty()) {
            if (entry.path().extension() != filter.extension) {
                continue;
            }
        }

        // 文件大小过滤
        auto size = entry.file_size();
        if (size < filter.min_size || size > filter.max_size) {
            continue;
        }

        results.push_back(entry.path());
    }

    std::sort(results.begin(), results.end());
    return results;
}

int main() {
    SearchFilter filter;
    filter.extension = ".cpp";
    filter.min_size = 100;      // 至少 100 字节
    filter.max_size = 1000000;  // 不超过 1MB

    auto files = search_files("/home/user/project", filter);
    std::cout << "找到 " << files.size() << " 个文件:\n";
    for (const auto& f : files) {
        std::cout << "  " << f << "\n";
    }
    return 0;
}
