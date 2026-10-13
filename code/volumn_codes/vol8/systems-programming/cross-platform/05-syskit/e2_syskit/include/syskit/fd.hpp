// syskit/fd.hpp —— POSIX fd 的 RAII(工程化收编自思维基石 01)
//
// 定义出处:thinking/01-raii-paradigm.md 的 unique_fd(全子卷唯一定义处)。
// 本头是搬家不是重设计:move-only、reset(带参)、release、swap、
// 析构丢弃 close 返回值(理由见原文:重试可能关掉别人复用的新 fd)。
#pragma once

#include <utility>

#ifndef _WIN32
#    include <unistd.h>

namespace syskit {

class unique_fd {
  public:
    explicit unique_fd(int fd = -1) noexcept : fd_(fd) {}

    unique_fd(unique_fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    unique_fd& operator=(unique_fd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~unique_fd() { reset(); }

    int get() const noexcept { return fd_; }

    int release() noexcept { return std::exchange(fd_, -1); }

    explicit operator bool() const noexcept { return fd_ >= 0; }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_); // 返回值直接丢弃,口径见思维基石 01
        }
        fd_ = fd;
    }

    void swap(unique_fd& other) noexcept { std::swap(fd_, other.fd_); }
    friend void swap(unique_fd& a, unique_fd& b) noexcept { a.swap(b); }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

  private:
    int fd_;
};

} // namespace syskit
#endif // !_WIN32
