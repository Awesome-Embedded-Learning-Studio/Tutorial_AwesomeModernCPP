// unique_file_lock.hpp —— E3:把 LockFileEx 的锁生死绑到对象生死上(对照 Linux 侧 file_lock)
//
// 与 Linux 侧的一处结构性差异:那边一把 flock 锁绑一个 fd,锁与句柄同生共死;
// 这边「锁」和「句柄」是两件事 —— UnlockFile 放锁不关句柄,CloseHandle 连锁带句柄
// 一起放。所以本类析构走两步:显式 UnlockFile(把「放锁」写在看得见的位置,
// 排查时时间线上有一句可对)+ CloseHandle 兜底。
// try_lock_for 的口径与 Linux 侧同样无奈:LockFileEx 没有「等多久」的参数,
// 只能 FAIL_IMMEDIATELY + Sleep(10ms) 小步轮询,等待分辨率即轮询间隔
// (想不轮询,看 02-try-lock 的 FILE_FLAG_OVERLAPPED 加餐)。
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

struct defer_lock_t {
    explicit defer_lock_t() = default;
};
inline constexpr defer_lock_t defer_lock{};

class unique_file_lock {
  public:
    explicit unique_file_lock(const char* path, bool exclusive = true, std::uint64_t off = 0,
                              std::uint64_t len = 4096)
        : h_{open_or_die(path)}, off_{off}, len_{len} {
        lock_blocking(exclusive);
    }

    unique_file_lock(const char* path, defer_lock_t, std::uint64_t off = 0,
                     std::uint64_t len = 4096)
        : h_{open_or_die(path)}, off_{off}, len_{len} {}

    ~unique_file_lock() {
        if (h_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                unlock(); // 显式放锁:排查时时间线上有这句可对
            }
            CloseHandle(h_); // 兜底:句柄一关,名下残余的锁也会被内核放掉
        }
    }

    unique_file_lock(unique_file_lock&& other) noexcept
        : h_{other.h_}, off_{other.off_}, len_{other.len_}, locked_{other.locked_} {
        other.h_ = INVALID_HANDLE_VALUE; // moved-from 成空壳:析构碰不到新主人的锁
        other.locked_ = false;
    }

    unique_file_lock& operator=(unique_file_lock&& other) noexcept {
        if (this != &other) {
            tidy();
            h_ = other.h_;
            off_ = other.off_;
            len_ = other.len_;
            locked_ = other.locked_;
            other.h_ = INVALID_HANDLE_VALUE;
            other.locked_ = false;
        }
        return *this;
    }

    unique_file_lock(const unique_file_lock&) = delete;
    unique_file_lock& operator=(const unique_file_lock&) = delete;

    bool try_lock(bool exclusive = true) noexcept {
        OVERLAPPED ov{};
        ov.Offset = (DWORD)off_;
        ov.OffsetHigh = (DWORD)(off_ >> 32);
        SetLastError(0);
        BOOL ok = LockFileEx(h_, flags(exclusive, true), 0, (DWORD)len_, (DWORD)(len_ >> 32), &ov);
        if (ok) {
            locked_ = true;
            return true;
        }
        return false; // 33(ERROR_LOCK_VIOLATION)是「锁被占着」的正常答案,不算错误;
                      // 其余 errno 理应升级成异常,偏偏 noexcept 屋檐下抛就是
                      // terminate,只能吞 —— 与 Linux 侧头文件里的取舍一字不差
    }

    template <class Rep, class Period>
    bool try_lock_for(std::chrono::duration<Rep, Period> d, bool exclusive = true) {
        constexpr DWORD kPollMs = 10; // 等待分辨率 = 轮询间隔
        const auto deadline = std::chrono::steady_clock::now() + d;
        for (;;) {
            if (try_lock(exclusive)) {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            ::Sleep(kPollMs);
        }
    }

    void unlock() noexcept {
        if (locked_) {
            UnlockFile(h_, (DWORD)off_, (DWORD)(off_ >> 32), (DWORD)len_, (DWORD)(len_ >> 32));
            locked_ = false;
        }
    }

    void reset() noexcept { tidy(); }

    bool owns_lock() const noexcept { return locked_; }
    bool alive() const noexcept { return h_ != INVALID_HANDLE_VALUE; }
    HANDLE native_handle() const noexcept { return h_; }
    std::uint64_t offset() const noexcept { return off_; }
    std::uint64_t length() const noexcept { return len_; }

  private:
    static DWORD flags(bool exclusive, bool failImmediately) {
        DWORD f = 0;
        if (exclusive) {
            f |= LOCKFILE_EXCLUSIVE_LOCK;
        }
        if (failImmediately) {
            f |= LOCKFILE_FAIL_IMMEDIATELY;
        }
        return f;
    }

    static HANDLE open_or_die(const char* path) {
        HANDLE h =
            CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            char msg[128];
            std::snprintf(msg, sizeof msg, "CreateFileA(%s) 失败:%lu", path,
                          (unsigned long)GetLastError());
            throw std::runtime_error(msg);
        }
        return h;
    }

    void lock_blocking(bool exclusive) {
        OVERLAPPED ov{};
        ov.Offset = (DWORD)off_;
        ov.OffsetHigh = (DWORD)(off_ >> 32);
        SetLastError(0);
        if (!LockFileEx(h_, flags(exclusive, false), 0, (DWORD)len_, (DWORD)(len_ >> 32), &ov)) {
            char msg[128];
            std::snprintf(msg, sizeof msg, "LockFileEx 失败:%lu", (unsigned long)GetLastError());
            throw std::runtime_error(msg);
        }
        locked_ = true;
    }

    void tidy() noexcept {
        if (h_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                unlock();
            }
            CloseHandle(h_);
        }
        h_ = INVALID_HANDLE_VALUE;
        locked_ = false;
    }

    HANDLE h_ = INVALID_HANDLE_VALUE;
    std::uint64_t off_ = 0;
    std::uint64_t len_ = 0;
    bool locked_ = false;
};
