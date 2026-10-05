// article.hpp —— 01/02 两篇已定义的契约工具,原样搬来:
//   errno_code / sys_call / unique_fd  (01-posix-file-io.md)
//   mapped_region                      (02-mmap-memory-mapping.md)
// 新增的只有 sigout.hpp 里 handler 用的 write_all/write_hex(文章 sigbus 节选同款)。
#pragma once

#include <cerrno>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

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
            ::close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_;
};

class mapped_region {
  public:
    mapped_region() noexcept = default;

    mapped_region(const unique_fd& fd, std::size_t length, int prot, int flags, off_t offset = 0) {
        void* p = ::mmap(nullptr, length, prot, flags, fd.get(), offset);
        if (p == MAP_FAILED) {
            throw std::system_error{errno_code(), "mmap"};
        }
        addr_ = static_cast<unsigned char*>(p);
        const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
        length_ = (length + page - 1) / page * page;
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

    void reset() noexcept {
        if (addr_ != nullptr) {
            ::munmap(addr_, length_);
            addr_ = nullptr;
            length_ = 0;
        }
    }

    unsigned char* data() const noexcept { return addr_; }
    std::size_t size() const noexcept { return length_; }
    explicit operator bool() const noexcept { return addr_ != nullptr; }

    mapped_region(const mapped_region&) = delete;
    mapped_region& operator=(const mapped_region&) = delete;

  private:
    unsigned char* addr_ = nullptr;
    std::size_t length_ = 0;
};
