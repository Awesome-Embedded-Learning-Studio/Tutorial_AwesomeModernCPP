// fsio.hpp —— 契约工具(系列沿用 01/02 两篇的形态,filesystem 篇新增 unique_dir)
//   errno_code : 失败后立刻调用,errno 在这一刻定格进 error_code(01-posix-file-io.md)
//   unique_dir : opendir 拿到的 DIR* 交给 RAII 管,~unique_dir() 里 closedir
// readdir 循环里 dirfd() 拿到的 fd 与 DIR* 共用游标,别自己再 open。
#pragma once

#include <cerrno>
#include <system_error>
#include <utility>

#include <dirent.h>
#include <unistd.h>

inline std::error_code errno_code() noexcept {
    return std::error_code{errno, std::generic_category()};
}

class unique_dir {
  public:
    explicit unique_dir(DIR* d = nullptr) noexcept : dir_(d) {}
    unique_dir(unique_dir&& other) noexcept : dir_(other.dir_) { other.dir_ = nullptr; }
    unique_dir& operator=(unique_dir&& other) noexcept {
        if (this != &other) {
            reset();
            dir_ = std::exchange(other.dir_, nullptr);
        }
        return *this;
    }
    ~unique_dir() { reset(); }

    DIR* get() const noexcept { return dir_; }
    explicit operator bool() const noexcept { return dir_ != nullptr; }
    int fd() const noexcept { return ::dirfd(dir_); } // 与 DIR* 共用游标
    void reset(DIR* d = nullptr) noexcept {
        if (dir_ != nullptr) {
            ::closedir(dir_);
        }
        dir_ = d;
    }

    unique_dir(const unique_dir&) = delete;
    unique_dir& operator=(const unique_dir&) = delete;

  private:
    DIR* dir_ = nullptr;
};
