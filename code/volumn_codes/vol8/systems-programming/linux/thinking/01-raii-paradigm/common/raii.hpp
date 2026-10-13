#pragma once
// common/raii.hpp —— 《OS 资源的 RAII 范式》实验的公共骨架
//
// 内容:errno_code / sys_call(错误处理契约,与 file-io/01 篇同款)、
// unique_fd 与 mapped_region 的完整版(析构释放、move-only、
// release/reset/get/swap、explicit operator bool、禁拷贝)。
//
// 编译口径:g++ -std=c++20 -Wall -Wextra [-O2],实测 g++ 16.2.1 零警告。

#include <cerrno>
#include <cstddef>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// 错误处理契约(与 file-io/01 篇一致)
// ---------------------------------------------------------------------------

// 契约一:非抛路径。errno 是线程局部的,读进 error_code 就定格了
inline std::error_code errno_code() noexcept {
    return std::error_code{errno, std::generic_category()};
}

// 契约二:任何「返回 -1 表失败」的 syscall 都从这儿过
template <class F, class... Args> auto sys_call(const char* what, F&& f, Args&&... args) {
    auto result = std::forward<F>(f)(std::forward<Args>(args)...);
    if (result == -1) {
        throw std::system_error{errno, std::generic_category(), what};
    }
    return result;
}

// ---------------------------------------------------------------------------
// unique_fd:把 close 写进析构函数
// ---------------------------------------------------------------------------
//
// close() 失败了怎么办?(依据 man 2 close,man-pages 6.19,NOTES 节
// "Dealing with error returns from close()")
//
//   1. Linux 内核在 close 操作的早期就释放了 fd 本身,此后这个 fd 号随时
//      可能被别的线程拿去复用;真正可能出错的步骤(向文件系统/设备冲刷
//      数据)发生在 close 的后半段。所以「close 失败就再 close 一次」是
//      错的:第二次 close 关掉的可能是别人刚拿到手的新 fd。
//   2. EINTR 是特例:POSIX.1-2008 只说「fd 的状态未指定」。Linux 与多数
//      实现的行为是:与其他错误一样,fd 保证已经被关闭,只是顺带报告了
//      一声;HP-UX 是文档写明的反例(EINTR 时 fd 仍然开着,必须再 close
//      一次才不泄漏)。POSIX.1-2024 采纳了 HP-UX 的行为,Linux 因此「不合
//      新标准」,man 页明说「无计划修改」。
//   3. 落到析构函数里,结论只有一条:close 的返回值直接丢弃。不重试
//      (见第 1 条),也不能抛(析构在 noexcept 语境里跑,抛了就是
//      std::terminate)。真在乎 I/O 错误,要在 close 之前对同一个 fd 调
//      fsync(2) 并检查它的返回值——那是 man 页给「想知道 I/O 错误的细心
//      程序员」的正解。
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

    // 放弃所有权:fd 交还调用方,此后析构不再 close
    int release() noexcept { return std::exchange(fd_, -1); }

    explicit operator bool() const noexcept { return fd_ >= 0; }

    // 先 close 手里的(若有),再接管新 fd。无参调用 = close + 变空
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            // 返回值按类注释第 3 条丢弃:不重试、不抛
            ::close(fd_);
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

// ---------------------------------------------------------------------------
// mapped_region:把 munmap 写进析构函数(与 unique_fd 同构)
// ---------------------------------------------------------------------------

class mapped_region {
  public:
    // release() 的带走清单:想手动 munmap,要的正是这两样
    struct released {
        unsigned char* addr;
        std::size_t length;
    };

    mapped_region() noexcept = default;

    mapped_region(const unique_fd& fd, std::size_t length, int prot, int flags, off_t offset = 0) {
        // mmap 失败返回的是 MAP_FAILED(即 (void*)-1)并设 errno,不是
        // nullptr;sys_call 的「-1 检查」对 void* 编不过,只能手写
        void* p = ::mmap(nullptr, length, prot, flags, fd.get(), offset);
        if (p == MAP_FAILED) {
            throw std::system_error{errno_code(), "mmap"};
        }
        addr_ = static_cast<unsigned char*>(p);
        const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
        length_ = (length + page - 1) / page * page; // 内核按页向上取整,munmap 要用取整后的长度
    }

    mapped_region(mapped_region&& other) noexcept : addr_(other.addr_), length_(other.length_) {
        other.addr_ = nullptr;
        other.length_ = 0;
    }

    mapped_region& operator=(mapped_region&& other) noexcept {
        if (this != &other) {
            reset();
            addr_ = std::exchange(other.addr_, nullptr);
            length_ = std::exchange(other.length_, 0);
        }
        return *this;
    }

    ~mapped_region() { reset(); }

    // munmap 同样没有「重试」的余地:失败多半意味着地址或长度已经错了,
    // 析构里能做的只有吞掉返回值
    void reset() noexcept {
        if (addr_ != nullptr) {
            ::munmap(addr_, length_);
            addr_ = nullptr;
            length_ = 0;
        }
    }

    // 接管一段裸映射(自己 mmap 出来的地址交给它管)
    void reset(unsigned char* addr, std::size_t length) noexcept {
        reset();
        addr_ = addr;
        length_ = length;
    }

    // 放弃所有权:地址与(取整后的)长度一并交还调用方
    released release() noexcept {
        return released{std::exchange(addr_, nullptr), std::exchange(length_, 0)};
    }

    unsigned char* data() const noexcept { return addr_; }
    std::size_t size() const noexcept { return length_; } // 向上取整后的映射长度
    explicit operator bool() const noexcept { return addr_ != nullptr; }

    void swap(mapped_region& other) noexcept {
        std::swap(addr_, other.addr_);
        std::swap(length_, other.length_);
    }
    friend void swap(mapped_region& a, mapped_region& b) noexcept { a.swap(b); }

    mapped_region(const mapped_region&) = delete;
    mapped_region& operator=(const mapped_region&) = delete;

  private:
    unsigned char* addr_ = nullptr;
    std::size_t length_ = 0;
};
