# 嵌入式C++教程——std::expected

写一段可靠又不振聋发聩的错误处理代码，是每个 C++ 开发者的隐秘欲望。`std::expected`（C++23 标准中引入）像个温柔的中介：当你要么有一个值，要么有个错误，别再用 `throw`、别再滥用 `std::optional<T>` 或 `std::variant` 来凑合了——它专为这类场景设计。

------

## 开篇：为什么需要 expected

把错误当成“值”的一等公民，能让代码逻辑变得线性而明确。`std::optional<T>` 告诉你“有或者没有值”，但没有告诉你为什么没有；`std::variant<T, E>` 能表达“要么这个，要么那个”，但读者每次看到 variant 解包时都要心里默念三遍类型顺序。`expected<T, E>` 则直接表达出一种约定：**成功就给你 T，失败给你 E**。调用端可以显式检查、链式组合、或者优雅地传播错误——可读、可组合，还不会像异常那样把控制流抛到天外去。

------

## 一个简短、可用的 C++17 实现（精简版本，侧重可读性）

下面的实现不是标准库级别的工业化产品，但它能让你在 C++17 环境里体验 `expected` 的语义，并在博客或教学中直接运行与演示。

```cpp
#include <utility>
#include <type_traits>
#include <stdexcept>

// 用于构造错误分支的辅助类型
template <typename E>
struct unexpected {
    E value;
};

template <typename T, typename E>
class expected {
    bool has_value_;
    union {
        T val_;
        E err_;
    } storage_;

public:
    // 构造成功值
    expected(const T& v) : has_value_(true) { new(&storage_.val_) T(v); }
    expected(T&& v) : has_value_(true) { new(&storage_.val_) T(std::move(v)); }

    // 构造错误
    expected(unexpected<E> u) : has_value_(false) { new(&storage_.err_) E(std::move(u.value)); }

    // 默认构造为错误分支需要 E 有默认值——这里不提供默认构造

    // 析构
    ~expected() {
        if (has_value_) storage_.val_.~T(); else storage_.err_.~E();
    }

    bool has_value() const noexcept { return has_value_; }

    T& value() {
        if (!has_value_) throw std::runtime_error("bad expected access");
        return storage_.val_;
    }

    const E& error() const {
        if (has_value_) throw std::runtime_error("no error present");
        return storage_.err_;
    }

    // 简单的 value_or
    T value_or(T default_value) const {
        if (has_value_) return storage_.val_;
        return default_value;
    }

    // map：将成功值用函数 f 转换为另一个 expected
    template <typename F>
    auto map(F f) const -> expected<decltype(f(std::declval<T>())), E> {
        using U = decltype(f(std::declval<T>()));
        if (has_value_) return expected<U, E>(f(storage_.val_));
        return expected<U, E>(unexpected<E>{storage_.err_});
    }

    // and_then：链式调用，f 返回 expected<U, E>
    template <typename F>
    auto and_then(F f) const -> decltype(f(std::declval<T>())) {
        if (has_value_) return f(storage_.val_);
        return decltype(f(std::declval<T>()))(unexpected<E>{storage_.err_});
    }
};
```

这个实现省略了大量边角（拷贝/移动语义的细粒度控制、`noexcept`、`constexpr`、更友好的 `unexpected` API、复杂的 SFINAE），但足够清楚地表达 `expected` 的核心：**要么有值，要么有错误**。

------

## 使用示例：从解析到链式调用

来看个小例子：一个解析函数返回 `expected<int, std::string>`，调用端可以优雅地链式处理错误。

```cpp
#include <string>
#include <iostream>

expected<int, std::string> parse_int(const std::string& s) {
    try {
        size_t pos;
        int v = std::stoi(s, &pos);
        if (pos != s.size()) return unexpected<std::string>{"trailing chars"};
        return expected<int, std::string>(v);
    } catch (...) {
        return unexpected<std::string>{"not a number"};
    }
}

int main(){
    auto r = parse_int("123");
    if (r.has_value()) std::cout << "value=" << r.value() << "\n";
    else std::cerr << "error: " << r.error() << "\n";

    // 链式示例（伪代码风格）
    auto final = parse_int("42").and_then([](int x){
        return expected<double, std::string>(x / 2.0);
    }).map([](double d){ return d * 3.0; });
}
```

链式写法的魅力在于：每一步只关心成功分支，错误会自动穿透并最终被处理或返回给上层逻辑。

- 错误类型 `E`：推荐使用小而可拷贝的类型（错误码、短字符串、结构体）。把整个 `std::exception_ptr` 或重对象当作错误会让 `expected` 变得笨重。
- 异常 vs 返回值：`expected` 非常适合库边界或性能敏感场景。在你不想用异常控制流、又希望调用 site 显式处理失败的地方，它是理想选择。
- 链式风格：`and_then`、`map` 让你像写函数式代码那样组织逻辑。只要注意被链的函数签名匹配。
- 与 `std::optional` 的区别：`optional<T>` 只是“可能没有值”，并不提供错误信息。`expected<T, E>` 则更具语义性，能承载失败原因。

把错误当成值来处理，会让你的代码少些异常的惊吓，多些明确的控制流。C++23 的 `std::expected` 是标准对这一路线的肯定；而在不得不用 C++17 的时代，动手实现一个小巧的 `expected`，既能提升代码可读性，也能为团队带来更一致的错误处理风格。

