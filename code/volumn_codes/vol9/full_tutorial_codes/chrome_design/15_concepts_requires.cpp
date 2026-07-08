// concepts 与 requires:WeakPtr 的转换构造约束 + 成员函数 const 重载
// 来源:WeakPtr 前置知识(四):concepts 与 requires 在 WeakPtr 里的应用 (pre-04)
// 编译:g++ -std=c++20 -Wall -Wextra 15_concepts_requires.cpp -o 15_concepts_requires

#include <concepts>
#include <iostream>
#include <type_traits>
#include <typeinfo>

namespace {

struct Base {
    virtual ~Base() = default;
    virtual int kind() const { return 0; }
};
struct Derived : Base {
    int kind() const override { return 1; }
};

// 简化版 WeakPtr,只保留转换构造 + const 重载这两个 concept 用法
template <typename T> class MiniWeakPtr {
  public:
    MiniWeakPtr() = default;

    // 向上转型转换构造:requires(std::convertible_to<U*, T*>)
    template <typename U>
        requires(std::convertible_to<U*, T*>)
    MiniWeakPtr(const MiniWeakPtr<U>&) noexcept {
        std::cout << "    [转换构造] WeakPtr<" << typeid(U).name() << "> -> WeakPtr<"
                  << typeid(T).name() << ">\n";
    }
};

// 简化版 WeakPtrFactory:GetWeakPtr 的 const/非 const 重载用 requires(!is_const_v<T>)
template <typename T> class MiniFactory {
  public:
    explicit MiniFactory(T* p) : ptr_(p) {}

    // const factory → WeakPtr<const T>
    void get() const { std::cout << "    [const 重载] 返回 WeakPtr<const T>\n"; }
    // 非 const factory → WeakPtr<T>,仅在 T 非 const 时存在
    void get()
        requires(!std::is_const_v<T>)
    {
        std::cout << "    [非 const 重载] 返回 WeakPtr<T>(可变)\n";
    }

  private:
    T* ptr_;
};

} // namespace

int main() {
    std::cout << "=== 转换构造:向上转型合法 ===\n";
    MiniWeakPtr<Derived> wd;
    MiniWeakPtr<Base> wb = wd; // ✓ Derived* → Base* 满足 convertible_to
    std::cout << "  Derived -> Base: OK\n";
    // MiniWeakPtr<Derived> wd2 = wb;   // ✗ Base* -> Derived* 不满足,编译错(constraints not
    // satisfied)

    std::cout << "\n=== const 正确性:成员函数 requires ===\n";
    Derived d;

    std::cout << "  MiniFactory<Derived>:\n";
    MiniFactory<Derived> fac(&d);
    const MiniFactory<Derived> cf(&d);
    std::cout << "    非 const .get(): ";
    fac.get();
    std::cout << "    const     .get(): ";
    cf.get();

    std::cout << "\n  MiniFactory<const Derived>(T 已是 const → 非 const 重载被 "
                 "requires(!is_const_v) 干掉):\n";
    MiniFactory<const Derived> cfac(&d);
    std::cout << "    .get(): ";
    cfac.get(); // 只剩 const 重载
    return 0;
}
