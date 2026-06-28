#pragma once
#include <unistd.h>

namespace net {

/// RAII 包装一个 POSIX fd:不可拷贝、仅可移动,析构时 close。
/// (与 01-modern-socket 同一个,Lab 里复用。)
class UniqueFd {
  public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_{fd} {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
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

} // namespace net
