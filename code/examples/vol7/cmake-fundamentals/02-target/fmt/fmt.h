#pragma once
#include <string>

namespace fmt {
// 仅供演示用的极简 fmt::format,真实工程用 find_package(fmt) 接入
inline std::string format(const std::string& tmpl, const std::string& value) {
    auto pos = tmpl.find("{}");
    if (pos == std::string::npos)
        return tmpl;
    return tmpl.substr(0, pos) + value + tmpl.substr(pos + 2);
}
} // namespace fmt
