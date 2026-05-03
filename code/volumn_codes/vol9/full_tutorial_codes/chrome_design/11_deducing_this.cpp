// Deducing this / 显式对象参数 (C++23)
// 来源：OnceCallback 前置知识（六）(pre-06)
// 编译：g++ -std=c++23 -Wall -Wextra 11_deducing_this.cpp -o 11_deducing_this

#include <iostream>
#include <type_traits>
#include <utility>

// --- 基本推导规则验证 ---

struct Check {
    void test(this auto&& self) {
        using Self = decltype(self);
        if constexpr (std::is_lvalue_reference_v<Self>) {
            if constexpr (std::is_const_v<std::remove_reference_t<Self>>) {
                std::cout << "  const lvalue reference\n";
            } else {
                std::cout << "  lvalue reference\n";
            }
        } else {
            std::cout << "  rvalue (not a reference)\n";
        }
    }
};

// --- 模拟 OnceCallback::run() 的左值拦截 ---

struct SimpleCallback {
    void run(this auto&& self) {
        static_assert(!std::is_lvalue_reference_v<decltype(self)>,
            "SimpleCallback::run() must be called on an rvalue. "
            "Use std::move(cb).run() instead.");
        std::cout << "  callback executed!\n";
    }
};

// --- 常量传播与 const 方法区分 ---

struct Value {
    int v = 0;

    void get(this const auto& self) {
        std::cout << "  const access: " << self.v << "\n";
    }

    void mutate(this auto&& self) {
        if constexpr (!std::is_const_v<std::remove_reference_t<decltype(self)>>) {
            self.v++;
            std::cout << "  mutated to: " << self.v << "\n";
        } else {
            std::cout << "  cannot mutate const object\n";
        }
    }
};

int main() {
    std::cout << "=== deducing this 推导规则 ===\n";
    {
        Check c;
        c.test();                  // lvalue reference
        std::move(c).test();       // rvalue
        std::as_const(c).test();   // const lvalue reference
    }

    std::cout << "\n=== 模拟左值拦截 ===\n";
    {
        SimpleCallback cb;
        // cb.run();              // 编译错误！左值调用被拦截
        std::move(cb).run();      // OK：右值调用
        SimpleCallback().run();   // OK：临时对象也是右值
    }

    std::cout << "\n=== const/mutate 区分 ===\n";
    {
        Value v{10};
        v.get();      // const access
        v.mutate();   // mutated to 11
        v.mutate();   // mutated to 12

        const Value cv{99};
        cv.get();     // const access
        // cv.mutate();  // prints "cannot mutate const object"
    }

    return 0;
}
