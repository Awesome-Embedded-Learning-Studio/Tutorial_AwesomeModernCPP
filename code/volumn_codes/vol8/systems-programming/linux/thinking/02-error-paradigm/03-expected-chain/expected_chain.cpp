// E3:expected 双出口——工具层 expected 贯穿,应用顶层一次性升级成 system_error
// 编译:g++ -std=c++23 -Wall -Wextra e3_expected_chain.cpp -o e3
//   (std::expected 需要 C++23,实测 -std=c++20 下 g++ 16.2.1 拒绝)
//
// 跑法(四个场景,一次编译):
//   ./e3 /tmp/errpar/e3.conf           成功路径:and_then 一路串到底
//   ./e3 /tmp/errpar/e3_missing.conf   ENOENT:错误在最底层出生,原样传到 main
//   ./e3 /tmp/errpar                   EISDIR:open 成功、read 失败,错误生在链的中层
//   ./e3 /tmp/errpar/e3_empty.conf     invalid_argument:非 errno 错误同样能进链
//
// 分层:
//   L0 open_checked      expected<unique_fd, error_code>,唯一的 open 出口
//   L1 read_text         and_then 串接读循环
//   L2 first_line        and_then 串接解析,空文件给 errc::invalid_argument
//   L3 first_line_logged or_else 记一行日志并原样透传(不改写)
//   main                 !r 时 throw std::system_error(r.error(), ...) —— 全程序唯一 throw 点
#include <cerrno>
#include <cstdio>
#include <expected>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <unistd.h>

// 公共工具(系列沿用)
std::error_code errno_code() noexcept {
    return std::error_code{errno, std::generic_category()};
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
    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;
    ~unique_fd() { reset(); }
    int get() const noexcept { return fd_; }
    void reset() noexcept {
        if (fd_ != -1)
            ::close(fd_);
        fd_ = -1;
    }

  private:
    int fd_;
};

// L0:open 的唯一出口。成功给 fd,失败给 errno 装箱,谁也不抛
static std::expected<unique_fd, std::error_code> open_checked(const char* path) {
    int fd = ::open(path, O_RDONLY);
    if (fd == -1)
        return std::unexpected(errno_code());
    return unique_fd{fd};
}

// L1 的实现体:循环读到 EOF(部分读是常态),EINTR 就地重试
static std::expected<std::string, std::error_code> drain(unique_fd fd) {
    std::string out;
    char buf[256];
    for (;;) {
        ssize_t n = ::read(fd.get(), buf, sizeof buf);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            return std::unexpected(errno_code());
        }
        if (n == 0)
            return out; // EOF
        out.append(buf, static_cast<size_t>(n));
    }
}

// L1:and_then 把「打开 -> 读完」串成一条链,fd 的移动语义照常工作
static std::expected<std::string, std::error_code> read_text(const char* path) {
    return open_checked(path).and_then(drain);
}

// L2:再叠一层解析。空文件不是 syscall 错误,errc::invalid_argument 一样进链
static std::expected<std::string, std::error_code> first_line(const char* path) {
    return read_text(path).and_then(
        [](std::string&& text) -> std::expected<std::string, std::error_code> {
            auto nl = text.find('\n');
            std::string line = (nl == std::string::npos) ? std::move(text) : text.substr(0, nl);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            return line;
        });
}

// L3:or_else 在错误路径上记一行日志,不改写、不吞,原样透传
static std::expected<std::string, std::error_code> first_line_logged(const char* path) {
    return first_line(path).or_else(
        [path](const std::error_code& ec) -> std::expected<std::string, std::error_code> {
            std::fprintf(stderr, "[config] first_line(\"%s\") failed: %d %s -- passing through\n",
                         path, ec.value(), ec.message().c_str());
            return std::unexpected(ec);
        });
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/tmp/errpar/e3.conf";
    std::printf("loading config from: %s\n", path);
    try {
        std::expected<std::string, std::error_code> r = first_line_logged(path);
        if (!r) {
            // 应用顶层:唯一的 throw 点,expected 在此升级成异常
            throw std::system_error(r.error(), std::string("config '") + path + "'");
        }
        std::printf("ok: first line = \"%s\"\n", r->c_str());
    } catch (const std::system_error& e) {
        std::printf("caught at top: %s\n", e.what());
        std::printf("    code: value = %d, category = %s\n", e.code().value(),
                    e.code().category().name());
    }
    return 0;
}
