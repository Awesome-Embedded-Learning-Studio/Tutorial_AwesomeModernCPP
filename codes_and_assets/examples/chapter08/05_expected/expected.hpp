// expected.hpp
// 简化的 std::expected 实现（C++17）

#pragma once
#include <utility>
#include <type_traits>
#include <stdexcept>
#include <string>

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

    // 拷贝/移动构造（简化版）
    expected(const expected& other) : has_value_(other.has_value_) {
        if (has_value_) {
            new(&storage_.val_) T(other.storage_.val_);
        } else {
            new(&storage_.err_) E(other.storage_.err_);
        }
    }

    expected(expected&& other) noexcept : has_value_(other.has_value_) {
        if (has_value_) {
            new(&storage_.val_) T(std::move(other.storage_.val_));
        } else {
            new(&storage_.err_) E(std::move(other.storage_.err_));
        }
    }

    // 析构
    ~expected() {
        if (has_value_) storage_.val_.~T(); else storage_.err_.~E();
    }

    // 赋值运算符
    expected& operator=(const expected& other) {
        if (this != &other) {
            if (has_value_) storage_.val_.~T(); else storage_.err_.~E();
            has_value_ = other.has_value_;
            if (has_value_) {
                new(&storage_.val_) T(other.storage_.val_);
            } else {
                new(&storage_.err_) E(other.storage_.err_);
            }
        }
        return *this;
    }

    bool has_value() const noexcept { return has_value_; }
    explicit operator bool() const noexcept { return has_value_; }

    T& value() {
        if (!has_value_) throw std::runtime_error("bad expected access");
        return storage_.val_;
    }

    const T& value() const {
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
