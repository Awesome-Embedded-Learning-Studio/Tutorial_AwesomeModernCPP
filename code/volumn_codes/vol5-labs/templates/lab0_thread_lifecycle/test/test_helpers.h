#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace lab0::test {

/// 在 dir 下创建 count 个文件，每个 100 字节，扩展名 ext（含点号，如 ".cpp"）。
/// 测试辅助函数，非练习重点。
inline std::filesystem::path create_test_files(const std::filesystem::path& dir, int count,
                                               const std::string& ext = ".txt") {
    std::filesystem::create_directories(dir);
    for (int i = 0; i < count; ++i) {
        std::ofstream(dir / (std::string("file_") + std::to_string(i) + ext))
            << std::string(100, 'x');
    }
    return dir;
}

} // namespace lab0::test
