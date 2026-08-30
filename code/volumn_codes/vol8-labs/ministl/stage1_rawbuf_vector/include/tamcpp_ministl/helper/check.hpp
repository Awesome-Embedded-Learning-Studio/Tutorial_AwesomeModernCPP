#pragma once
#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace tamcpp::ministl::debug {
static inline void Check(bool IsExpectedTrue, const char* msg,
                         std::source_location loc = std::source_location::current()) {
    if (IsExpectedTrue) {
        return; // 哈，就是对的？无事发生~
    }

    std::fprintf(stderr, "[TAMCPP Check Crash]%s-%d(%s): %s\n", loc.file_name(), loc.line(),
                 loc.function_name(), msg);
    // 再见了小朋友
    std::abort();
}
} // namespace tamcpp::ministl::debug
