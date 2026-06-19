#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace lab0 {

/**
 * @brief This is the file system paths
 *
 */
struct FileInfo {
    std::filesystem::path path;   ///> where is the file?
    std::uintmax_t file_size = 0; ///> how large
    std::string extension;        ///> Extensions owns the point, like: ".cpp", or ".c"
};

} // namespace lab0
