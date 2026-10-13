// syskit/call.hpp —— 判错、装箱、EINTR 重试的统一入口(工程化收编自思维基石 02 + W01)
//
// 定义出处:
//   sys_call    : thinking/02-error-paradigm.md(含 EINTR 内部重试的最终版)
//   check_win32 : windows/file-io/01-win32-file-io.md(if constexpr 按返回类型分流)
// 收编后两侧各住各的 #ifdef,对调用方暴露的是同一句话:
//   失败抛 std::system_error,what 带前缀,code 带装箱后的错误码。
#pragma once

#include <functional>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "error.hpp"

namespace syskit {

#ifdef _WIN32
// Win32 调用包装:失败值语义按 API 而定——指针/HANDLE 类 NULL 或
// INVALID_HANDLE_VALUE,BOOL 类 FALSE,命中即抛 system_error
template <class F, class... Args> auto check_win32(const char* what, F&& f, Args&&... args) {
    auto result = std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    bool failed = false;
    if constexpr (std::is_pointer_v<decltype(result)>) {
        failed = result == nullptr || result == INVALID_HANDLE_VALUE;
    } else {
        failed = result == 0; // BOOL 类 API 失败返回 FALSE
    }
    if (failed) {
        throw std::system_error{last_error(), what};
    }
    return result;
}
#else
// POSIX 调用包装:任何"返回 -1 表失败"的 syscall 从这儿过。
// EINTR 不算错,从头再来;其余 errno 装箱抛 system_error
template <class F, class... Args> auto sys_call(const char* what, F&& f, Args&&... args) {
    for (;;) {
        auto r = std::forward<F>(f)(std::forward<Args>(args)...);
        if (r == -1) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error{errno, std::generic_category(), what};
        }
        return r; // 0(EOF)不是 -1,原样放行,判断留给调用方
    }
}
#endif

} // namespace syskit
