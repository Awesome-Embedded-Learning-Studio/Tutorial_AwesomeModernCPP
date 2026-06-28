#pragma once
#include <unistd.h>

/// RAII 包装一个 POSIX fd:不可拷贝、仅可移动,析构时 close。
/// 替代裸 int fd + 手动 close——后者在提前 return / 异常时极易漏 close(fd)。
/// 配套 documents/vol8-domains/networking/01-modern-socket-wrapping.md。
class UniqueFd {
  public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_{fd} {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete; // 独占所有权,禁拷贝
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    /// 显式关闭并置 -1。已 close 再调安全(fd_ < 0 时不动)。
    void reset() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int get() const { return fd_; }
    int release() {
        int f = fd_;
        fd_ = -1;
        return f;
    }
    explicit operator bool() const { return fd_ >= 0; }

  private:
    int fd_{-1};
};
