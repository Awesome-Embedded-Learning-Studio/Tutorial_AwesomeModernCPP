// win_dir.hpp —— 契约工具(系列沿用 01-win32-file-io.md 的 win_util.hpp 形态,目录枚举篇新增
// unique_find)
//   last_error_code : 失败后立刻调用,GetLastError 在这一刻定格进 error_code
//   check_win32     : 失败值语义按 API 而定(句柄类 NULL/-1,BOOL 类 0),命中即抛 system_error
//   unique_handle   : HANDLE 的 RAII(~unique_handle 里 CloseHandle)
//   unique_find     : 本篇新增——FindFirstFileW 发回的 HANDLE 交给 RAII 管,~unique_find 里
//                     FindClose。哨兵与 CreateFileW 同款 INVALID_HANDLE_VALUE(两套失败值宇宙
//                     里它跟句柄族走)
//   to_utf8 / ft_str: 宽字符名与 FILETIME 的输出辅助(Win32_FIND_DATA 全是宽字符/FILETIME,
//                     .out 里要的是 UTF-8 字节)
#pragma once

#define WIN32_LEAN_AND_MEAN // 本头必须最先被 include,否则这两个宏可能已被预定义
#define NOMINMAX

#include <cstdio>
#include <functional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <windows.h>

// GetLastError 立即装箱:Win32 错误码挂 system_category(与 01-win32-file-io.md 同款)
inline std::error_code last_error_code() noexcept {
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}

// Win32 调用包装:失败值语义按 API 而定,命中即抛 std::system_error{last_error_code(), what}
template <class F, class... Args> auto check_win32(const char* what, F&& f, Args&&... args) {
    auto result = std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    bool failed = false;
    if constexpr (std::is_pointer_v<decltype(result)>) {
        failed = result == nullptr || result == INVALID_HANDLE_VALUE;
    } else {
        failed = result == 0; // BOOL 类 API 失败返回 FALSE
    }
    if (failed) {
        throw std::system_error{last_error_code(), what};
    }
    return result;
}

// HANDLE 的 RAII:形态与 Linux 篇的 unique_fd / 01 篇的 win_util.hpp 完全同构
class unique_handle {
  public:
    explicit unique_handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    unique_handle(unique_handle&& o) noexcept : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    unique_handle& operator=(unique_handle&& o) noexcept {
        if (this != &o) {
            reset(o.release());
        }
        return *this;
    }
    ~unique_handle() { reset(); }

    HANDLE get() const noexcept { return h_; }
    HANDLE release() noexcept { return std::exchange(h_, INVALID_HANDLE_VALUE); }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept {
        if (h_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(h_);
        }
        h_ = h;
    }
    explicit operator bool() const noexcept { return h_ != INVALID_HANDLE_VALUE; }

  private:
    HANDLE h_{INVALID_HANDLE_VALUE};
};

// FindFirstFileW 的查找句柄:失败值 INVALID_HANDLE_VALUE,关法是 FindClose 不是 CloseHandle
class unique_find {
  public:
    explicit unique_find(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    unique_find(unique_find&& o) noexcept : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    unique_find& operator=(unique_find&& o) noexcept {
        if (this != &o) {
            reset(o.release());
        }
        return *this;
    }
    ~unique_find() { reset(); }

    HANDLE get() const noexcept { return h_; }
    HANDLE release() noexcept { return std::exchange(h_, INVALID_HANDLE_VALUE); }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept {
        if (h_ != INVALID_HANDLE_VALUE) {
            ::FindClose(h_);
        }
        h_ = h;
    }
    explicit operator bool() const noexcept { return h_ != INVALID_HANDLE_VALUE; }

  private:
    HANDLE h_{INVALID_HANDLE_VALUE};
};

// 宽字符 → UTF-8:WIN32_FIND_DATA 的名字是 wchar_t,.out 是 UTF-8 字节流
inline std::string to_utf8(const wchar_t* ws) {
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), n, nullptr, nullptr);
    }
    return s;
}

// FILETIME → "YYYY-MM-DD hh:mm:ss.mmm"(本地时区,FileTimeToSystemTime 后直排)
inline std::string ft_str(const FILETIME& ft) {
    SYSTEMTIME st{};
    FileTimeToSystemTime(&ft, &st); // 失败(如时间未设置)得到全零,照排
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}
