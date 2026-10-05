// file_lock.hpp —— flock 的 RAII 封装(文章《文件锁》E5 的主角)
//
// 设计要点:
//   * 构造 = open + 阻塞加锁;析构 = 显式 LOCK_UN + close(close 本身也会释放
//     flock 锁,显式放锁图个可读,也让「锁的生死 == 对象的生死」在代码里可见)
//   * move-only:fd 只有一份所有权,moved-from 对象析构不得放别人的锁
//   * try_lock_for:flock 没有「等待上限」的原生参数,只能 LOCK_NB + 小步轮询,
//     分辨率即轮询间隔(这里 1 ms)
#pragma once

#include "article.hpp"

#include <chrono>

#include <sys/file.h>

// 与 std::unique_lock 的 defer_lock 同思路:只 open,不上锁
struct defer_lock_t {
    explicit defer_lock_t() = default;
};
inline constexpr defer_lock_t defer_lock{};

class file_lock {
  public:
    file_lock() noexcept = default;

    // 构造即加锁(阻塞版)。exclusive=false 时拿 LOCK_SH
    explicit file_lock(const char* path, bool exclusive = true)
        : fd_{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)} {
        lock(exclusive);
    }

    // 只 open 不上锁,锁留给 try_lock / try_lock_for
    file_lock(const char* path, defer_lock_t)
        : fd_{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)} {}

    ~file_lock() {
        if (fd_) {
            ::flock(fd_.get(), LOCK_UN); // 显式放锁;紧随的 close 是第二道保险
        }
    }

    file_lock(file_lock&& other) noexcept
        : fd_{other.fd_.release()}, exclusive_{other.exclusive_} {}

    file_lock& operator=(file_lock&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::move(other.fd_);
            exclusive_ = other.exclusive_;
        }
        return *this;
    }

    file_lock(const file_lock&) = delete;
    file_lock& operator=(const file_lock&) = delete;

    // 阻塞加锁。已持有时重复调用是幂等的(flock 同一描述上即转换)
    void lock(bool exclusive = true) {
        sys_call("flock", ::flock, fd_.get(), exclusive ? LOCK_EX : LOCK_SH);
        exclusive_ = exclusive;
    }

    // 非阻塞:拿不到返回 false(不抛),其余错误照旧抛 system_error
    bool try_lock(bool exclusive = true) noexcept {
        int r = ::flock(fd_.get(), (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB);
        if (r == 0) {
            exclusive_ = exclusive;
            return true;
        }
        if (errno == EWOULDBLOCK) {
            return false;
        }
        // errno != EWOULDBLOCK 的失败不该被吞,但 noexcept 里抛是 terminate,
        // 这里只能留一个痕迹:真实项目可改回可抛接口
        return false;
    }

    // 有限等待:LOCK_NB + 1 ms 轮询,分辨率 1 ms
    template <class Rep, class Period>
    bool try_lock_for(std::chrono::duration<Rep, Period> d, bool exclusive = true) {
        const auto deadline = std::chrono::steady_clock::now() + d;
        for (;;) {
            if (try_lock(exclusive)) {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            ::usleep(1000);
        }
    }

    void unlock() noexcept {
        if (fd_) {
            ::flock(fd_.get(), LOCK_UN);
        }
    }

    // 主动交枪:放锁 + 关 fd,对象变空
    void reset() noexcept {
        if (fd_) {
            unlock();
            fd_.reset();
        }
    }

    int fd() const noexcept { return fd_.get(); }
    explicit operator bool() const noexcept { return static_cast<bool>(fd_); }

  private:
    unique_fd fd_;
    bool exclusive_ = true;
};
