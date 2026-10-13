// e1_recon_trait_probe.cpp —— 篇2 e1:两套近亲工具的逐项对照(编译期+运行期双探针)
//
// 对照对象(两边都照抄原文定义,出处标在注释里,一字不改语义):
//   net::  networking 子卷的 UniqueFd + SysError
//          出处 documents/vol8-domains/networking/01-modern-socket-wrapping.md
//          与 code/volumn_codes/vol8/networking/01-modern-socket/unique_fd.hpp
//   sysp:: 本子卷(思维基石)的 unique_fd + errno_code/sys_call
//          出处 documents/vol8-domains/systems-programming/thinking/01、02
//
// 探针分两层:trait 对拍(类型系统的口径)+ 全链路对拍(同一个任务的两种写法,
// 各触发一次 ENOENT,看错误对象各自交出什么)。Linux 侧编译运行(两套近亲的
// 定义都是 Linux-only,Windows 面的 unique_handle 不是本次对照对象)。
//
// 编译: g++ -std=c++23 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2
//       (expected 是 C++23 起可用,与思维基石 02 的口径一致)
#include <cerrno>
#include <cstdio>
#include <expected>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

// =====================================================================
// net:: —— networking 侧原文照录(unique_fd.hpp + echo_server.cpp)
// =====================================================================
namespace net {

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

struct SysError {
    int errno_value;
    std::string context;
};

} // namespace net

// =====================================================================
// sysp:: —— 本子卷思维基石原文照录(thinking/01 的骨架 + 02 的错误工具)
// =====================================================================
namespace sysp {

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
    void swap(unique_fd& other) noexcept { std::swap(fd_, other.fd_); }
    friend void swap(unique_fd& a, unique_fd& b) noexcept { a.swap(b); }
    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

  private:
    int fd_;
};

inline std::error_code errno_code() noexcept {
    return std::error_code{errno, std::generic_category()};
}

} // namespace sysp

// =====================================================================
// trait 探针:detection idiom + noexcept 探测 + 标准 trait 对拍
// =====================================================================
template <class T, class = void> struct has_reset_with_arg : std::false_type {};
template <class T> struct has_reset_with_arg<T, std::void_t<decltype(std::declval<T&>().reset(0))>>
    : std::true_type {};

template <class T, class = void> struct has_swap : std::false_type {};
template <class T>
struct has_swap<T, std::void_t<decltype(std::declval<T&>().swap(std::declval<T&>()))>>
    : std::true_type {};

template <class T, class = void> struct has_release : std::false_type {};
template <class T> struct has_release<T, std::void_t<decltype(std::declval<T&>().release())>>
    : std::true_type {};

template <class T> static bool get_is_noexcept() {
    return noexcept(std::declval<T&>().get());
}
template <class T> static bool reset_is_noexcept() {
    return noexcept(std::declval<T&>().reset());
}

template <class T> static void row(const char* trait, T value) {
    if constexpr (std::is_same_v<T, bool>)
        std::printf("  %-34s %-8s %s\n", trait, value ? "yes" : "NO", "");
    else
        std::printf("  %-34s %zu\n", trait, static_cast<std::size_t>(value));
}

int main() {
    using net::UniqueFd;
    using sysp::unique_fd;

    std::printf("== A. 尺寸与移动语义(两侧同构) ==\n");
    std::printf("  %-34s net:%zu  sysp:%zu\n", "sizeof", sizeof(UniqueFd), sizeof(unique_fd));
    std::printf("  %-34s net:%d  sysp:%d\n", "is_nothrow_move_constructible",
                std::is_nothrow_move_constructible_v<UniqueFd>,
                std::is_nothrow_move_constructible_v<unique_fd>);
    std::printf("  %-34s net:%d  sysp:%d\n", "is_nothrow_move_assignable",
                std::is_nothrow_move_assignable_v<UniqueFd>,
                std::is_nothrow_move_assignable_v<unique_fd>);
    std::printf("  %-34s net:%d  sysp:%d\n", "is_default_constructible",
                std::is_default_constructible_v<UniqueFd>,
                std::is_default_constructible_v<unique_fd>);

    std::printf("== B. 接口面差异(trait 探针) ==\n");
    std::printf("  %-34s net:%d  sysp:%d\n", "reset 带参重载 reset(int)",
                has_reset_with_arg<UniqueFd>::value, has_reset_with_arg<unique_fd>::value);
    std::printf("  %-34s net:%d  sysp:%d\n", "成员 swap()", has_swap<UniqueFd>::value,
                has_swap<unique_fd>::value);
    std::printf("  %-34s net:%d  sysp:%d\n", "std::is_swappable_v", std::is_swappable_v<UniqueFd>,
                std::is_swappable_v<unique_fd>);
    std::printf("  %-34s net:%d  sysp:%d\n", "release()", has_release<UniqueFd>::value,
                has_release<unique_fd>::value);
    std::printf("  %-34s net:%d  sysp:%d\n", "get() 标注 noexcept", get_is_noexcept<UniqueFd>(),
                get_is_noexcept<unique_fd>());
    std::printf("  %-34s net:%d  sysp:%d\n", "reset() 标注 noexcept", reset_is_noexcept<UniqueFd>(),
                reset_is_noexcept<unique_fd>());

    std::printf("== C. 错误模型对拍(SysError vs error_code) ==\n");
    std::printf("  %-34s net:%zu  sysp:%zu\n", "sizeof(错误类型)", sizeof(net::SysError),
                sizeof(std::error_code));
    // 同一次 ENOENT,两种装箱
    int probe_fd = ::open("/recon/no_such_file", O_RDONLY);
    int saved_errno = (probe_fd == -1) ? errno : 0;
    net::SysError se{saved_errno, "open"};
    std::error_code ec = std::error_code{saved_errno, std::generic_category()};
    std::printf("  两侧错误值同源(errno=%d)\n", saved_errno);
    std::printf("  net::SysError  携带上下文: \"%s\"\n", se.context.c_str());
    std::printf("  sysp error_code message(): \"%s\"\n", ec.message().c_str());
    std::printf("  error_code == errc::no_such_file_or_directory : %s\n",
                ec == std::errc::no_such_file_or_directory ? "true" : "false");
    {
        std::printf("  SysError errno_value 与 errc 判等(裸 int 比较枚举):\n");
        // SysError 的 int 可以拿去和 errc 的枚举值比,但那是裸数字对枚举,
        // 编不过就说明这条路要手写;error_code 的判等走 category 映射
    }

    std::printf("== D. 全链路对拍:open+read 一个不存在的文件 ==\n");
    auto net_open = [](const char* p) -> std::expected<UniqueFd, net::SysError> {
        int r = ::open(p, O_RDONLY);
        if (r < 0)
            return std::unexpected(net::SysError{errno, "open"});
        return UniqueFd{r};
    };
    auto sysp_open = [](const char* p) -> std::expected<unique_fd, std::error_code> {
        int r = ::open(p, O_RDONLY);
        if (r == -1)
            return std::unexpected(sysp::errno_code());
        return unique_fd{r};
    };
    auto n = net_open("/recon/missing_a");
    auto s = sysp_open("/recon/missing_b");
    if (!n)
        std::printf("  [net ] 失败: context=\"%s\" errno=%d (\"哪一步\"在错误对象里)\n",
                    n.error().context.c_str(), n.error().errno_value);
    if (!s)
        std::printf("  [sysp] 失败: value=%d message=\"%s\" (\"哪一步\"要在调用点/日志层补)\n",
                    s.error().value(), s.error().message().c_str());

    // 成功路径:两副骨架各自走一遍移动/换手
    {
        int raw = ::open("/dev/null", O_RDONLY);
        UniqueFd nf{raw};
        UniqueFd nm = std::move(nf); // net 版移动
        std::printf("== E. 成功路径换手(/dev/null) ==\n");
        std::printf("  net  moved: src_empty=%d dst_valid=%d\n", nf ? 0 : 1, nm ? 1 : 0);
        int raw2 = ::open("/dev/null", O_RDONLY);
        unique_fd sf{raw2};
        unique_fd sm = std::move(sf); // sysp 版移动
        unique_fd sw;
        sw.swap(sm); // sysp 版独有:swap 换手
        std::printf("  sysp moved: src_empty=%d dst_valid=%d  after swap: sw=%d sm=%d\n",
                    sf ? 0 : 1, sm ? 1 : 0, sw ? 1 : 0, sm ? 1 : 0);
    }
    return 0;
}
