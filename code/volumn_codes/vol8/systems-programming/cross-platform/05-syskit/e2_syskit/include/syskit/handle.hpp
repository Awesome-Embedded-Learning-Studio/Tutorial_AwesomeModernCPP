// syskit/handle.hpp —— Win32 HANDLE 的 RAII(工程化收编自思维基石 01)
//
// 定义出处:thinking/01-raii-paradigm.md 的 unique_handle。
// 空哨兵是 INVALID_HANDLE_VALUE;NULL 家族 API 的判错必须发生在装进
// RAII 之前(check_win32 存在的理由),这条纪律在原文有实测背书。
#pragma once

#include <utility>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>

namespace syskit {

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

} // namespace syskit
#endif // _WIN32
