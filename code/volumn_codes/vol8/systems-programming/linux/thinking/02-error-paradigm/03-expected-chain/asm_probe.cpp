// E3 旁证:-O2 下,纯值域场景里 expected 的 monadic 链与手写分支是否生成同一份代码
// 编译:g++ -std=c++23 -O2 -S e3_asm_probe.cpp -o e3_asm_probe.s
// 对照:sink_manual 与 sink_monadic 的函数体(见 README 的 diff 摘录)
//
// 场景刻意收窄:「入参 -> 可能失败的一步 -> 失败给 -1」,
// 错误值只进 error_code,不做 IO、不跨不透明调用边界,
// 这样测的是 monadic 机制本身的开销,而不是 IO 的噪声。
#include <expected>
#include <system_error>

static int manual(int x) {
    if (x > 0)
        return x * 2;
    return -1;
}

static int monadic(int x) {
    std::expected<int, std::error_code> e{x};
    auto m = e.and_then([](int v) -> std::expected<int, std::error_code> {
        if (v > 0)
            return v * 2;
        return std::unexpected(std::error_code{static_cast<int>(std::errc::invalid_argument),
                                               std::generic_category()});
    });
    return m.value_or(-1);
}

// 包装一层,给汇编写出稳定的名字
int sink_manual(int x) {
    return manual(x);
}
int sink_monadic(int x) {
    return monadic(x);
}
