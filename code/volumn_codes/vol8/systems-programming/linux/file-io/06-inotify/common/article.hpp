// article.hpp —— 06-inotify 实验的公共骨架,沿用 file-io 系列的契约工具:
//   errno_code / sys_call / unique_fd  (01-posix-file-io.md 定义,此后各篇复用)
// 另加 inotify 专属三件:sysctl 上限读取 / mask 位解码 / 事件流读取器。
// 编译口径:g++ -std=c++20 -Wall -Wextra -Wpedantic -O2,实测 g++ 16.2.1 零警告。
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/inotify.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// 契约工具(与 01-posix-file-io / 05-file-lock 完全同款)
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

// ---------------------------------------------------------------------------
// inotify 专属工具
// ---------------------------------------------------------------------------

// /proc/sys/fs/inotify/ 的三个上限。本机实测:524288 / 1024 / 16384
struct inotify_limits {
    long max_user_watches;   // 每个真实用户所有实例加起来能挂的 watch 总数
    long max_user_instances; // 每个真实用户能 inotify_init 出的实例(fd)数
    long max_queued_events;  // 单个实例的事件队列深度,超了报 IN_Q_OVERFLOW
};

inline long read_sysctl(const char* path) {
    std::ifstream f{path};
    long v = -1;
    if (f) {
        f >> v;
    }
    return v;
}

inline inotify_limits read_inotify_limits() {
    return {read_sysctl("/proc/sys/fs/inotify/max_user_watches"),
            read_sysctl("/proc/sys/fs/inotify/max_user_instances"),
            read_sysctl("/proc/sys/fs/inotify/max_queued_events")};
}

inline void print_inotify_limits() {
    const auto lim = read_inotify_limits();
    std::printf("/proc/sys/fs/inotify/: max_user_watches=%ld max_user_instances=%ld"
                " max_queued_events=%ld\n",
                lim.max_user_watches, lim.max_user_instances, lim.max_queued_events);
}

// 把 mask 位域拆成人话。IN_ISDIR 不是事件而是状态位,也一并拆出来
inline std::string mask_to_str(std::uint32_t mask) {
    struct entry {
        std::uint32_t bit;
        const char* name;
    };
    static const entry table[] = {
        {IN_ACCESS, "IN_ACCESS"},
        {IN_MODIFY, "IN_MODIFY"},
        {IN_ATTRIB, "IN_ATTRIB"},
        {IN_CLOSE_WRITE, "IN_CLOSE_WRITE"},
        {IN_CLOSE_NOWRITE, "IN_CLOSE_NOWRITE"},
        {IN_OPEN, "IN_OPEN"},
        {IN_MOVED_FROM, "IN_MOVED_FROM"},
        {IN_MOVED_TO, "IN_MOVED_TO"},
        {IN_CREATE, "IN_CREATE"},
        {IN_DELETE, "IN_DELETE"},
        {IN_DELETE_SELF, "IN_DELETE_SELF"},
        {IN_MOVE_SELF, "IN_MOVE_SELF"},
        {IN_UNMOUNT, "IN_UNMOUNT"},
        {IN_Q_OVERFLOW, "IN_Q_OVERFLOW"},
        {IN_IGNORED, "IN_IGNORED"},
        {IN_ISDIR, "IN_ISDIR"},
    };
    std::uint32_t known = 0;
    std::string out;
    for (const auto& e : table) {
        known |= e.bit;
        if (mask & e.bit) {
            if (!out.empty()) {
                out += "|";
            }
            out += e.name;
        }
    }
    if (mask & ~known) { // 表外位兜底,防漏
        char hex[16];
        std::snprintf(hex, sizeof hex, "0x%x", static_cast<unsigned>(mask & ~known));
        if (!out.empty()) {
            out += "|";
        }
        out += hex;
    }
    return out.empty() ? "(0)" : out;
}

// 解码后的一条事件。len 是内核给的原始值:名字含结尾 '\0' 再向
// sizeof(struct inotify_event)(=16)对齐填充后的长度,不是 strlen
struct decoded_event {
    int wd = -1;
    std::uint32_t mask = 0;
    std::uint32_t cookie = 0;
    std::uint32_t len = 0;
    std::string name; // len==0(比如 watch 的是文件本身)时为空串
};

inline std::string format_event(const decoded_event& e) {
    char buf[512];
    std::snprintf(buf, sizeof buf, "wd=%-3d mask=0x%08x %-44s cookie=%-6u len=%-3u name='%s'", e.wd,
                  static_cast<unsigned>(e.mask), mask_to_str(e.mask).c_str(), e.cookie, e.len,
                  e.name.c_str());
    return buf;
}

// inotify fd 上的事件流读取器。read(2) 一次返回整数条事件(man 7 inotify),
// 缓冲区只要装得下「一条最大事件」(16 + NAME_MAX+1 后对齐,约 288 字节)
// 就不会被切开;这里用 4096 并对「短事件」做了防御性丢弃检查。
class event_reader {
  public:
    // 把当前排队的事件一次取干净。要求 fd 是 IN_NONBLOCK 的:队列空时
    // read 返回 EAGAIN,正好当「没了」的信号
    std::vector<decoded_event> drain(int fd) {
        std::vector<decoded_event> out;
        for (;;) {
            char buf[4096];
            ssize_t n = ::read(fd, buf, sizeof buf);
            if (n == -1 && errno == EINTR) {
                continue;
            }
            if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }
            if (n == -1) {
                throw std::system_error{errno_code(), "read(inotify fd)"};
            }
            decode_append(buf, static_cast<std::size_t>(n), out);
            if (static_cast<std::size_t>(n) < sizeof buf) {
                break; // 读干了
            }
        }
        return out;
    }

  private:
    static void decode_append(const char* buf, std::size_t n, std::vector<decoded_event>& out) {
        std::size_t off = 0;
        while (off + sizeof(struct inotify_event) <= n) {
            // memcpy 而不是指针转型:buf 是 char 数组,对齐不归我们管
            struct inotify_event raw;
            std::memcpy(&raw, buf + off, sizeof raw);
            const std::size_t step = sizeof raw + raw.len;
            if (off + step > n) {
                break; // read 不会切半条事件,真发生就是内核违约,丢弃防御
            }
            decoded_event e;
            e.wd = raw.wd;
            e.mask = raw.mask;
            e.cookie = raw.cookie;
            e.len = raw.len;
            if (raw.len > 0) {
                e.name.assign(buf + off + sizeof raw, ::strnlen(buf + off + sizeof raw, raw.len));
            }
            out.push_back(std::move(e));
            off += step;
        }
    }
};
