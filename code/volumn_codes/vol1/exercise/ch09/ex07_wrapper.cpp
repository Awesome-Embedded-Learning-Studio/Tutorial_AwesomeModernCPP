/**
 * @file ex07_wrapper.cpp
 * @brief 练习：模板偏特化 — 指针感知容器 Wrapper<T>
 *
 * 主模板 Wrapper<T> 存储值，get() 返回值。
 * 偏特化 Wrapper<T*> 存储指针，get() 返回解引用值，
 * 额外提供 is_null() 判断指针是否为空。
 * 演示偏特化与接口一致性。
 */

#include <iostream>
#include <string>

/**
 * @brief 主模板——存储一个值
 *
 * @tparam T 值类型
 */
template <typename T>
class Wrapper
{
public:
    /// @brief 用值构造
    explicit Wrapper(const T& value) : value_(value) {}

    /// @brief 获取存储的值
    T& get() { return value_; }

    /// @brief 获取存储的值（只读）
    const T& get() const { return value_; }

    /// @brief 打印包装内容
    void print(const char* name) const
    {
        std::cout << "  " << name << " (值类型): " << value_ << "\n";
    }

private:
    T value_;
};

/**
 * @brief 偏特化——存储指针，提供指针感知接口
 *
 * @tparam T 指针所指向的类型
 */
template <typename T>
class Wrapper<T*>
{
public:
    /// @brief 用指针构造
    explicit Wrapper(T* ptr) : ptr_(ptr) {}

    /// @brief 获取解引用后的值
    T& get() { return *ptr_; }

    /// @brief 获取解引用后的值（只读）
    const T& get() const { return *ptr_; }

    /// @brief 判断指针是否为空
    bool is_null() const { return ptr_ == nullptr; }

    /// @brief 打印包装内容
    void print(const char* name) const
    {
        if (ptr_) {
            std::cout << "  " << name << " (指针类型): *" << *ptr_ << "\n";
        } else {
            std::cout << "  " << name << " (指针类型): nullptr\n";
        }
    }

private:
    T* ptr_;
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== ex07: Wrapper<T> 指针感知容器 =====\n\n";

    // --- 主模板 Wrapper<int>（值语义）---
    {
        Wrapper<int> w(42);
        std::cout << "Wrapper<int> 值语义测试:\n";
        w.print("w");
        std::cout << "  get() = " << w.get() << "\n";
        // 修改
        w.get() = 100;
        std::cout << "  修改后 get() = " << w.get() << "\n\n";
    }

    // --- 主模板 Wrapper<std::string>（值语义）---
    {
        Wrapper<std::string> w(std::string("hello"));
        std::cout << "Wrapper<std::string> 值语义测试:\n";
        w.print("w");
        std::cout << "  get() = \"" << w.get() << "\"\n\n";
    }

    // --- 偏特化 Wrapper<int*>（指针语义，非空）---
    {
        int x = 99;
        Wrapper<int*> w(&x);
        std::cout << "Wrapper<int*> 指针测试:\n";
        w.print("w");
        std::cout << "  get() = " << w.get() << "\n";
        std::cout << "  is_null() = " << std::boolalpha << w.is_null()
                  << "\n";

        // 通过 wrapper 修改所指值
        w.get() = 200;
        std::cout << "  修改后 x = " << x << "\n";
        std::cout << "  修改后 get() = " << w.get() << "\n\n";
    }

    // --- 偏特化 Wrapper<int*>（空指针）---
    {
        Wrapper<int*> w(nullptr);
        std::cout << "Wrapper<int*> 空指针测试:\n";
        w.print("w");
        std::cout << "  is_null() = " << std::boolalpha << w.is_null()
                  << "\n\n";
    }

    // --- 偏特化 Wrapper<std::string*>（指针语义）---
    {
        std::string s("world");
        Wrapper<std::string*> w(&s);
        std::cout << "Wrapper<std::string*> 指针测试:\n";
        w.print("w");
        std::cout << "  get() = \"" << w.get() << "\"\n";
        std::cout << "  is_null() = " << std::boolalpha << w.is_null()
                  << "\n";
    }

    return 0;
}
