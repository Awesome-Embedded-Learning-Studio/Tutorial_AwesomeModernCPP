#pragma once
#include <string>

namespace mylib {

/// @brief 把问候语格式化成带前缀的字符串
/// @note  返回类型用 std::string —— 这是 mylib 公开 API 的一部分,
///        下游 app 也必须看到完整的 std::string 定义,
///        所以 <string> 对应的 include 路径属于 INTERFACE 需求
std::string make_greeting(const std::string& name);

} // namespace mylib
