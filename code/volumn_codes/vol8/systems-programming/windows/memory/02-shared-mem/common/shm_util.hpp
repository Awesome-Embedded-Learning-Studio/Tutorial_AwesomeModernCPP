// shm_util.hpp —— 共享内存篇契约工具(形态沿用 win_util.hpp / win_dir.hpp,本篇新增 unique_view /
// spawn_self / qpc 计时)
//   last_error_code : 失败后立刻调用,GetLastError 在这一刻定格进 error_code
//   unique_handle   : HANDLE 的 RAII(~unique_handle 里 CloseHandle;CreateFileMappingW 族失败值是
//                     NULL,不是 INVALID_HANDLE_VALUE,接管时要指明哨兵)
//   unique_view     : MapViewOfFile 返回的基址的 RAII(~unique_view 里 UnmapViewOfFile;失败值 NULL)
//   spawn_self      : CreateProcessW 把自己按新命令行再拉一份(本篇双进程实验的统一起跑方式),
//                     子进程继承本进程的控制台(stdout 同一条流,.out 里前后可读)
//   qpc_ns / qpc_ms : QueryPerformanceCounter 计时(E4 吞吐用)
// 双进程实验的纪律:
//   (a) 起跑同步一律用命名事件(这也是本篇主题之一),父子都 printf 后必须 fflush——管道下
//       stdout 全缓冲,不冲刷的行会丢/乱序(W02 的老教训)
//   (b) 跨进程交换数据只用"共享内存里的偏移",绝不传指针(基址各进程不同,E2 专门实测)
#pragma once

#define WIN32_LEAN_AND_MEAN // 本头必须最先被 include,否则这两个宏可能已被预定义
#define NOMINMAX

#include <cstdint>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>
#include <windows.h>

// GetLastError 立即装箱:Win32 错误码挂 system_category(与 01-win32-file-io.md 同款)
inline std::error_code last_error_code() noexcept {
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}

// HANDLE 的 RAII:形态与 Linux 篇的 unique_fd / 01 篇的 win_util.hpp 完全同构。
// 注意哨兵可配:CreateFileMappingW/OpenFileMappingW/CreateMutexW 族失败给 NULL,
// CreateFileW 失败给 INVALID_HANDLE_VALUE——本篇全部对象(映射/事件/互斥体/信号量)走 NULL 哨兵。
class unique_handle {
  public:
    explicit unique_handle(HANDLE h = nullptr) noexcept : h_(h) {}
    unique_handle(unique_handle&& o) noexcept : h_(std::exchange(o.h_, nullptr)) {}
    unique_handle& operator=(unique_handle&& o) noexcept {
        if (this != &o) {
            reset(o.release());
        }
        return *this;
    }
    ~unique_handle() { reset(); }

    HANDLE get() const noexcept { return h_; }
    HANDLE release() noexcept { return std::exchange(h_, nullptr); }
    void reset(HANDLE h = nullptr) noexcept {
        if (h_ != nullptr) {
            ::CloseHandle(h_);
        }
        h_ = h;
    }
    explicit operator bool() const noexcept { return h_ != nullptr; }

  private:
    HANDLE h_{nullptr};
};

// MapViewOfFile 基址的 RAII:析构 UnmapViewOfFile(参数是基址不是句柄)
class unique_view {
  public:
    explicit unique_view(void* p = nullptr) noexcept : p_(p) {}
    unique_view(unique_view&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    unique_view& operator=(unique_view&& o) noexcept {
        if (this != &o) {
            reset(o.release());
        }
        return *this;
    }
    ~unique_view() { reset(); }

    void* get() const noexcept { return p_; }
    template <class T> T* as() const noexcept { return static_cast<T*>(p_); }
    void* release() noexcept { return std::exchange(p_, nullptr); }
    void reset(void* p = nullptr) noexcept {
        if (p_ != nullptr) {
            ::UnmapViewOfFile(p_);
        }
        p_ = p;
    }
    explicit operator bool() const noexcept { return p_ != nullptr; }

  private:
    void* p_{nullptr};
};

// 把自己再拉一份(argc/argv 换成 args),bInheritHandles=FALSE——本篇不需要句柄继承,
// 跨进程全靠"同名打开"(这正是主题:命名对象就是共享的通道)
inline bool spawn_self(const wchar_t* args, PROCESS_INFORMATION& pi) {
    wchar_t path[MAX_PATH * 2];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH * 2)) {
        return false;
    }
    std::wstring cmd = std::wstring(L"\"") + path + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    BOOL ok = CreateProcessW(path, cmd.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
                             nullptr, nullptr, &si, &pi);
    return ok != FALSE;
}

// QueryPerformanceCounter 计时:ns / ms 两种刻度
inline int64_t qpc_ns() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1'000'000'000LL / f.QuadPart;
}

inline double qpc_ms() {
    return static_cast<double>(qpc_ns()) / 1.0e6;
}

// 命名辅助:Local\ 前缀 + 实验名 + pid(每轮唯一,防上一轮崩溃残留的半死对象撞名)
inline std::wstring local_name(const wchar_t* what, DWORD pid) {
    return std::wstring(L"Local\\SysProgShm_") + what + L"_" + std::to_wstring(pid);
}
