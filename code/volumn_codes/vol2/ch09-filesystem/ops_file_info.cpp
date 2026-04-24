#include <filesystem>
#include <iostream>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

void print_file_info(const fs::path& p) {
    std::error_code ec;

    // 文件大小（字节）
    auto size = fs::file_size(p, ec);
    if (!ec) {
        std::cout << "大小: " << size << " 字节\n";
        if (size > 1024 * 1024) {
            std::cout << "      "
                      << size / (1024.0 * 1024.0) << " MB\n";
        } else if (size > 1024) {
            std::cout << "      "
                      << size / 1024.0 << " KB\n";
        }
    }

    // 最后修改时间
    auto ftime = fs::last_write_time(p, ec);
    if (!ec) {
        // C++20 之前：需要转换成 time_t 来显示
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
        std::cout << "修改时间: "
                  << std::ctime(&time_t_val);
    }

    // 文件状态（权限等）
    auto status = fs::status(p, ec);
    if (!ec) {
        std::cout << "类型: " << static_cast<int>(status.type()) << "\n";
        std::cout << "权限: " << static_cast<unsigned>(status.permissions()) << "\n";
    }
}

int main() {
    print_file_info("/usr/local/bin/gcc");
    return 0;
}
