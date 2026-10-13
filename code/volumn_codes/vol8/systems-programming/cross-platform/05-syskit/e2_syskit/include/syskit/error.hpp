// syskit/error.hpp —— 错误装箱的统一出口(工程化收编自思维基石 02)
//
// 定义出处:documents/vol8-domains/systems-programming/thinking/02-error-paradigm.md
// 本头是"唯一定义处到工程库"的搬家,语义一字不改:
//   POSIX  : errno_code()      —— errno 装进 generic_category
//   Win32  : last_error_code() —— GetLastError 装进 system_category
// 纪律同名同义:失败分支的头一行立刻装箱,槽位值定格进 error_code。
#pragma once

#include <system_error>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace syskit {

#ifdef _WIN32
inline std::error_code last_error_code() noexcept {
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}
#else
inline std::error_code errno_code() noexcept {
    return std::error_code{errno, std::generic_category()};
}
#endif

// 两侧同名的别名:用户代码写 syskit::last_error(),平台差异收进实现
#ifdef _WIN32
inline std::error_code last_error() noexcept {
    return last_error_code();
}
#else
inline std::error_code last_error() noexcept {
    return errno_code();
}
#endif

} // namespace syskit
