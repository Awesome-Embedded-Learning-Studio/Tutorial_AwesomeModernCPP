// 配套 05-type-safe-any.md「any_cast 必须精确匹配 / void* 不安全」
// 对照:类型擦除若不存 type_info,拿 void* 强转就是定时炸弹;真 any 靠 typeid 精确比对拦下
// 编译运行:g++ -std=c++17 -Wall -Wextra any_cast_safety.cpp -o acs && ./acs
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>

// 不安全的「假 any」:只存 void*,什么都不记
class UnsafeAny {
    void* data_;

  public:
    template <typename T> explicit UnsafeAny(T value) : data_(new T(value)) {}
    ~UnsafeAny() {} // 不知道类型就没法正确析构,这里干脆泄漏

    // 危险:调用方想转什么就转什么,完全没有类型校验
    template <typename T> T get_as() const { return *static_cast<T*>(data_); }
};

// 安全的「真 any」:存 type_info,转型前先比对
class SafeAny {
    struct base {
        virtual ~base() = default;
        virtual const std::type_info& type() const noexcept = 0;
        virtual const void* untyped() const noexcept = 0;
    };
    template <typename T> struct holder final : base {
        T data;
        explicit holder(T v) : data(std::move(v)) {}
        const std::type_info& type() const noexcept override { return typeid(T); }
        const void* untyped() const noexcept override { return &data; }
    };
    std::unique_ptr<base> h_;

  public:
    template <typename T, typename D = std::decay_t<T>> explicit SafeAny(T&& v)
        : h_(std::make_unique<holder<D>>(std::forward<T>(v))) {}

    template <typename T> T cast() const {
        if (!h_ || h_->type() != typeid(T))
            throw std::bad_cast{};
        return *static_cast<const T*>(h_->untyped());
    }
};

int main() {
    std::cout << "=== 假 any:存 int,取成 double,程序不报错但胡说 ===" << std::endl;
    UnsafeAny bad(42);                 // 存的是 int,内存里是 0x0000002A
    double got = bad.get_as<double>(); // 当 double 的 8 字节位模式读
    std::cout << "  存 42,取成 double 得到: " << got << "\n";
    std::cout << "  (把 4 字节 int 当 8 字节 double 读,高位是垃圾,结果完全错误)\n";

    std::cout << "\n=== 真 any:type_info 比对,存 int 取 double 被拦下 ===" << std::endl;
    SafeAny good(42);
    try {
        (void)good.cast<double>();
    } catch (const std::bad_cast&) {
        std::cout << "  bad_cast:typeid(int) != typeid(double),转型前就挡住了\n";
    }

    std::cout << "\n=== typeid 精确匹配的边界 ===" << std::endl;
    std::cout << "  typeid(int) == typeid(int):        " << (typeid(int) == typeid(int)) << "\n";
    std::cout << "  typeid(int) == typeid(const int):  " << (typeid(int) == typeid(const int))
              << "  (顶层 const 被 typeid 忽略,可取)\n";
    std::cout << "  typeid(int) == typeid(long):       " << (typeid(int) == typeid(long))
              << "  (不同类型,拒)\n";
    std::cout << "  typeid(int) == typeid(unsigned):   " << (typeid(int) == typeid(unsigned))
              << "  (不同类型,拒)\n";
    std::cout << "  typeid(string) == typeid(const char*): "
              << (typeid(std::string) == typeid(const char*)) << "  (完全不同,拒)\n";

    std::cout << "\n  要点:any_cast 不做隐式转换,要精确类型对上。\n"
              << "  这和 dynamic_cast 那种多态转型是两回事:它不沿继承链走,只认同一个 type_info。\n";
}
