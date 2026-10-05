// e2_negative.cpp
// 负例: 一个「只会等、不会交」的残缺后端,验证 concept 在编译期如实拦下它。
//
//   LazyBackend 有 submit 有 wait,唯独没有 take —— 收割这一环缺失。
//   文件里两处检查:
//     1. static_assert(!AsyncBackend<LazyBackend>) —— 这一行应该编过:
//        concept 检查如实回答「不满足」,这是正例 static_assert 的镜像。
//     2. main 里把 LazyBackend 递给受约束的泛型函数 —— 这一行必须编不过:
//        约束不满足,编译在实例化之前就被拦下。编译器输出原样存档
//        (e2_negative_linux.err / e2_negative_windows.err),报错里指名缺的
//        就是 take 这一项。
//
// 编译: 预期失败。命令与两侧编译器版本见 .err 文件头部注记。

#include <concepts>
#include <cstdint>
#include <cstdio>
#include <optional>

struct completion {
    std::uint64_t id;
    std::size_t bytes;
    int error;
};

struct read_request {
    std::uint64_t id;
    std::uintptr_t target;
    void* buf;
    std::size_t len;
    unsigned long long offset;
};

template <typename B>
concept AsyncBackend = requires(B& b, typename B::request r, int timeout_ms) {
    typename B::completion;
    typename B::request;
    { b.submit(r) } -> std::same_as<bool>;
    { b.wait(timeout_ms) } -> std::same_as<int>;
    { b.take() } -> std::same_as<std::optional<typename B::completion>>;
};

// 残缺后端: 没有 take()。
struct LazyBackend {
    using completion = ::completion;
    using request = ::read_request;
    bool submit(request) { return true; }
    int wait(int) { return 0; }
};

// 检查一: 镜像断言,应当通过(不满足就是「不满足」,没有含糊)。
static_assert(!AsyncBackend<LazyBackend>,
              "LazyBackend 不许满足 AsyncBackend —— 缺 take 编译期就该现形");

// 与 e1 相同的受约束驱动。
template <AsyncBackend B> int drive(B& backend) {
    return backend.wait(1000) > 0 ? 0 : 1;
}

int main() {
    LazyBackend b;
    // 检查二: 这里必须编译失败 —— 约束不满足,泛型函数不许实例化。
    return drive(b);
}
