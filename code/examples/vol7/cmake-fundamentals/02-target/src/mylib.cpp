#include "mylib/mylib.h"

#include "fmt.h"

namespace mylib {

std::string make_greeting(const std::string& name) {
    // fmt 是 mylib 内部实现细节,公开头文件 mylib.h 里看不到 fmt 的痕迹
    // 所以下游根本不需要知道 fmt 的存在 —— 这正是 fmt 应当为 PRIVATE 的理由
    return fmt::format("hello, {}!", name);
}

} // namespace mylib
