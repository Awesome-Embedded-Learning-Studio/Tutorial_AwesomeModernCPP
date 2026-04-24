// 验证函数组合实现的正确性
#include <iostream>
#include <string>
#include <utility>

// compose：f(g(x))
auto compose = [](auto f, auto g) {
    return [f = std::move(f), g = std::move(g)](auto&&... args) {
        return f(g(std::forward<decltype(args)>(args)...));
    };
};

// pipe：先 g 后 f（语义更直觉）
auto pipe = [](auto g, auto f) {
    return [g = std::move(g), f = std::move(f)](auto&&... args) {
        return f(g(std::forward<decltype(args)>(args)...));
    };
};

// 多函数组合：从右到左依次应用
template<typename F>
auto compose_all(F f) {
    return f;
}

template<typename F, typename... Fs>
auto compose_all(F f, Fs... rest) {
    return [f = std::move(f), ...rest = std::move(rest)](auto&&... args) {
        return f(compose_all(rest...)(std::forward<decltype(args)>(args)...));
    };
}

// pipe_all：从左到右依次应用（更直觉）
template<typename F>
auto pipe_all(F f) {
    return f;
}

template<typename F, typename... Fs>
auto pipe_all(F f, Fs... rest) {
    return [f = std::move(f), ...rest = std::move(rest)](auto&&... args) {
        return pipe_all(rest...)(f(std::forward<decltype(args)>(args)...));
    };
}

int main() {
    auto double_it = [](int x) { return x * 2; };
    auto add_one = [](int x) { return x + 1; };
    auto negate_it = [](int x) { return -x; };
    auto to_string = [](int x) { return std::to_string(x); };

    std::cout << "=== Testing compose ===\n";
    // compose(add_one, double_it)(5) = add_one(double_it(5)) = add_one(10) = 11
    auto composed = compose(add_one, double_it);
    std::cout << "compose(add_one, double_it)(5) = " << composed(5) << "\n";    // 11

    // 多层组合
    auto pipeline = compose(to_string, compose(add_one, double_it));
    std::cout << "compose(to_string, compose(add_one, double_it))(5) = "
              << pipeline(5) << "\n";    // "11"

    std::cout << "\n=== Testing pipe ===\n";
    auto piped = pipe(double_it, add_one);
    std::cout << "pipe(double_it, add_one)(5) = " << piped(5) << "\n";

    std::cout << "\n=== Testing pipe_all ===\n";
    // pipe: 5 -> add_one -> double_it -> negate_it
    // 5 -> 6 -> 12 -> -12
    auto multi_pipeline = pipe_all(add_one, double_it, negate_it);
    std::cout << "pipe_all(add_one, double_it, negate_it)(5) = "
              << multi_pipeline(5) << "\n";   // -12

    return 0;
}
