// 配套 05-type-safe-any.md「in_place + 完美转发避免多余 move」
// 对比两种构造路径,数 move 次数:in_place 把构造参数直接转发,held 对象就地长出来
// 编译运行:g++ -std=c++17 -Wall -Wextra in_place_forward.cpp -o ipf && ./ipf
#include <iostream>
#include <memory>
#include <string>
#include <utility>

struct Tracked {
    std::string payload;
    static int move_count;
    static int copy_count;
    Tracked() : payload("default") {}
    explicit Tracked(std::string s) : payload(std::move(s)) {
        std::cout << "  [Tracked(string)] 直接构造, payload=" << payload << "\n";
    }
    Tracked(const Tracked& o) : payload(o.payload) {
        ++copy_count;
        std::cout << "  [Tracked(const&)] 拷贝 #" << copy_count << "\n";
    }
    Tracked(Tracked&& o) noexcept : payload(std::move(o.payload)) {
        ++move_count;
        std::cout << "  [Tracked(&&)] 移动 #" << move_count << "\n";
    }
};
int Tracked::move_count = 0;
int Tracked::copy_count = 0;

class Any {
    struct base {
        virtual ~base() = default;
    };
    template <typename T> struct holder final : base {
        T data;
        template <typename... A> explicit holder(A&&... a) : data(std::forward<A>(a)...) {}
    };
    std::unique_ptr<base> h_;

  public:
    Any() = default;

    // 方式 1:接受现成的 T,转发进 holder
    template <typename T, typename D = std::decay_t<T>> Any(T&& v)
        : h_(std::make_unique<holder<D>>(std::forward<T>(v))) {}

    // 方式 2:in_place,构造参数直接转发给 holder 内部的 data
    template <typename T, typename... Args> explicit Any(std::in_place_type_t<T>, Args&&... args)
        : h_(std::make_unique<holder<T>>(std::forward<Args>(args)...)) {}
};

int main() {
    std::cout << "=== 方式 1:先有 Tracked 临时对象,再 move 进 any ===" << std::endl;
    Tracked::move_count = Tracked::copy_count = 0;
    {
        Any a = Tracked{"hello"}; // 临时 Tracked 先构造,再 move 进 holder
    }
    std::cout << "  方式 1 总移动次数 = " << Tracked::move_count
              << ", 总拷贝次数 = " << Tracked::copy_count << "\n";

    std::cout << "\n=== 方式 2:in_place 把构造参数直接转发 ===" << std::endl;
    Tracked::move_count = Tracked::copy_count = 0;
    {
        // string("hello") 直接转发给 Tracked 的构造,Tracked 在 holder 内部就地构造
        Any a(std::in_place_type<Tracked>, std::string("hello"));
    }
    std::cout << "  方式 2 总移动次数 = " << Tracked::move_count
              << ", 总拷贝次数 = " << Tracked::copy_count << "\n";
    std::cout << "  方式 2 没有 Tracked 的额外 move,对象在 holder 里直接长出来。\n";
    std::cout << "  这正是 std::any::emplace 和 make_unique 风格工厂用 in_place 的原因。\n";
}
