// 配套 05-type-safe-any.md「路线 A:虚函数风格的 mini any」
// 用「指向基类的 unique_ptr + 模板派生 data_holder」实现类型擦除,演示存取/转型/拷贝
// 编译运行:g++ -std=c++17 -Wall -Wextra mini_any.cpp -o m && ./m
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

class Any {
  private:
    // 外层只看到一个统一的「概念基类」,不知道内部到底存了什么类型
    struct concept_any_base {
        virtual ~concept_any_base() = default;
        virtual const std::type_info& type() const noexcept = 0;     // 类型问询
        virtual std::unique_ptr<concept_any_base> clone() const = 0; // 虚拷贝
        virtual const void* untyped() const noexcept = 0;            // 交出内部指针
    };

    // 模板派生类才知道真正存了 T,基类那一组虚函数在这里落实
    template <typename T> struct data_holder final : concept_any_base {
        T data;
        template <typename... Args> explicit data_holder(Args&&... args)
            : data(std::forward<Args>(args)...) {}

        const std::type_info& type() const noexcept override { return typeid(T); }
        std::unique_ptr<concept_any_base> clone() const override {
            return std::make_unique<data_holder<T>>(data); // 拷贝 held 对象
        }
        const void* untyped() const noexcept override { return &data; }
    };

    std::unique_ptr<concept_any_base> holder_;

  public:
    Any() = default;

    // 从任意值构造:decay 去掉引用/const,完美转发一份给 data_holder
    template <typename T, typename DT = std::decay_t<T>,
              typename = std::enable_if_t<!std::is_same_v<DT, Any>>>
    Any(T&& value) : holder_(std::make_unique<data_holder<DT>>(std::forward<T>(value))) {}

    // in_place 构造:任意构造参数直接转发给 T 的构造,不多一次 move
    template <typename T, typename... Args> explicit Any(std::in_place_type_t<T>, Args&&... args)
        : holder_(std::make_unique<data_holder<T>>(std::forward<Args>(args)...)) {}

    // 拷贝:走虚 clone,要求 held 类型可拷贝(否则 clone 实例化时报错)
    Any(const Any& other) : holder_(other.holder_ ? other.holder_->clone() : nullptr) {}
    Any& operator=(const Any& other) {
        if (this != &other) {
            holder_ = other.holder_ ? other.holder_->clone() : nullptr;
        }
        return *this;
    }
    Any(Any&&) noexcept = default;
    Any& operator=(Any&&) noexcept = default;

    bool has_value() const noexcept { return holder_ != nullptr; }
    const std::type_info& type() const noexcept { return holder_ ? holder_->type() : typeid(void); }

    // 友元:any_cast 需要访问私有 holder_ 和 untyped()
    template <typename T> friend const T* any_cast(const Any*) noexcept;
    template <typename T> friend T any_cast(const Any&);
};

// 指针重载:不匹配返回 nullptr,不抛异常
template <typename T> const T* any_cast(const Any* a) noexcept {
    if (!a || !a->has_value() || a->type() != typeid(T)) {
        return nullptr;
    }
    // type 已确认匹配,这里 static_cast 是安全的:untyped() 返回的就是 data_holder<T>::data
    return static_cast<const T*>(a->holder_->untyped());
}

// 值重载:不匹配抛 bad_cast(标准库用 bad_any_cast)
template <typename T> T any_cast(const Any& a) {
    const T* p = any_cast<T>(&a);
    if (!p)
        throw std::bad_cast{};
    return *p;
}

struct Point {
    int x, y;
};

int main() {
    std::cout << "=== 存取基础类型 ===" << std::endl;
    Any a = 42;
    Any b = std::string("hello");
    Any c = Point{1, 2};

    std::cout << "any_cast<int>(a)    = " << any_cast<int>(a) << "\n";
    std::cout << "any_cast<string>(b) = " << any_cast<std::string>(b) << "\n";
    Point p = any_cast<Point>(c);
    std::cout << "any_cast<Point>(c)  = {" << p.x << ", " << p.y << "}\n";

    std::cout << "\n=== 类型不匹配 -> 抛异常 ===" << std::endl;
    try {
        (void)any_cast<double>(a); // 存 int 取 double
    } catch (const std::bad_cast&) {
        std::cout << "  bad_cast:存 int 取 double,被 type_info 比对拦下\n";
    }

    std::cout << "\n=== 指针重载:不匹配返回 nullptr ===" << std::endl;
    const Any* pa = &a;
    std::cout << "  any_cast<int>(pa)  非空? " << (any_cast<int>(pa) != nullptr) << "\n";
    std::cout << "  any_cast<long>(pa) 非空? " << (any_cast<long>(pa) != nullptr) << "\n";

    std::cout << "\n=== 深拷贝:两个 any 独立 ===" << std::endl;
    Any d = a;                  // 虚 clone 拷了一份新的 int
    d = std::string("changed"); // 改 d 不影响 a
    std::cout << "  d 改成 string 后,a 仍是 int = " << any_cast<int>(a) << "\n";

    std::cout << "\n=== in_place 构造 ===" << std::endl;
    Any e(std::in_place_type<std::string>, 4, 'x'); // string(4, 'x') => "xxxx"
    std::cout << "  e = \"" << any_cast<std::string>(e) << "\"\n";
}
