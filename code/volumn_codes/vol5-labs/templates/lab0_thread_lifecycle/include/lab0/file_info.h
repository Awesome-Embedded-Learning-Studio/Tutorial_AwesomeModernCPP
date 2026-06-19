#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace lab0 {

/// 单文件扫描结果。
/// 纯数据结构，非练习重点——已提供完整定义，你不用修改。
struct FileInfo {
    std::filesystem::path path;
    std::uintmax_t file_size = 0;
    std::string extension; // 含点号，如 ".cpp"
};

} // namespace lab0
