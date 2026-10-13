// article.hpp —— 05-file-lock 实验的公共骨架,三件公共工具沿用思维基石两篇的定义:
//   errno_code / sys_call / unique_fd  (唯一定义处:thinking/01-raii-paradigm 与
//   thinking/02-error-paradigm)
// 编译口径:g++ -std=c++20 -Wall -Wextra -Wpedantic -O2,实测 g++ 16.2.1 零警告。
#pragma once

#include <cerrno>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

// 非抛路径:errno 是线程局部的,读进 error_code 就定格了
inline std::error_code errno_code() noexcept {
    return std::error_code{errno, std::generic_category()};
}

// 任何返回 -1 表失败的 syscall 都从这儿过:失败抛 system_error,what 带前缀
template <class F, class... Args> auto sys_call(const char* what, F&& f, Args&&... args) {
    auto result = std::forward<F>(f)(std::forward<Args>(args)...);
    if (result == -1) {
        throw std::system_error{errno, std::generic_category(), what};
    }
    return result;
}

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
            ::close(fd_); // 返回值丢弃:close 后 fd 号可能已被复用,重试是错的
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
