// auto_pitfalls.cpp - auto使用的常见陷阱
#include <iostream>
#include <vector>
#include <type_traits>
#include <initializer_list>
#include <array>
#include <functional>

void pitfall_1_unexpected_copy() {
    std::cout << "=== 陷阱1：意外的拷贝 ===\n\n";

    struct LargeData {
        std::array<int, 1000> data{};
        int id{};

        LargeData() {
            data.fill(42);
        }

        // 拷贝构造函数（带日志）
        LargeData(const LargeData& other) : data(other.data), id(other.id) {
            std::cout << "  [拷贝构造] LargeData拷贝了！\n";
        }

        // 移动构造函数（带日志）
        LargeData(LargeData&& other) noexcept
            : data(std::move(other.data)), id(other.id) {
            std::cout << "  [移动构造] LargeData移动了\n";
        }
    };

    std::vector<LargeData> container;
    container.emplace_back();
    container[0].id = 1;

    std::cout << "1. 使用auto（按值拷贝）:\n";
    for (auto item : container) {
        std::cout << "  处理数据ID: " << item.id << '\n';
    }

    std::cout << "\n2. 使用const auto&（避免拷贝）:\n";
    for (const auto& item : container) {
        std::cout << "  处理数据ID: " << item.id << '\n';
    }

    std::cout << "\n3. 使用auto&（需要修改时）:\n";
    for (auto& item : container) {
        item.id *= 10;
    }
    std::cout << "  修改后第一个元素ID: " << container[0].id << '\n';
}

void pitfall_2_proxy_types() {
    std::cout << "\n=== 陷阱2：代理类型（vector<bool>） ===\n\n";

    std::vector<bool> flags = {true, false, true, false, true};

    std::cout << "1. 尝试使用auto&修改:\n";
    // auto& bit = flags[0];  // 编译错误！
    std::cout << "  auto& 编译错误：vector<bool>::operator[]返回代理类型\n";

    std::cout << "\n2. 使用auto（拷贝）:\n";
    auto bit = flags[0];  // bit是一个新的bool值
    std::cout << "  bit = " << std::boolalpha << bit << '\n';
    bit = false;
    std::cout << "  修改bit后，flags[0] = " << flags[0] << "（未改变）\n";

    std::cout << "\n3. 正确修改vector<bool>:\n";
    flags[0] = false;
    std::cout << "  直接赋值后，flags[0] = " << flags[0] << '\n';
}

void pitfall_3_initializer_list() {
    std::cout << "\n=== 陷阱3：初始化列表推导 ===\n\n";

    std::cout << "1. auto x = {1, 2, 3};\n";
    auto x = {1, 2, 3};  // std::initializer_list<int>
    std::cout << "  类型: std::initializer_list<int>\n";
    std::cout << "  大小: " << x.size() << '\n';

    std::cout << "\n2. auto y{1, 2, 3}; (C++17)\n";
    // auto y{1, 2, 3};  // C++17+: 错误！多个元素
    std::cout << "  C++17+中，auto{多个元素}是错误\n";

    std::cout << "\n3. auto z{42}; (C++17)\n";
    auto z{42};  // C++17+: int（不是initializer_list）
    std::cout << "  C++17+: 类型是int\n";
    std::cout << "  值: " << z << '\n';

    // 函数参数问题
    std::cout << "\n4. 函数返回initializer_list:\n";
    auto get_list = []() -> std::initializer_list<int> {
        return {1, 2, 3};  // 返回std::initializer_list<int>
    };
    auto list = get_list();
    std::cout << "  获取的列表大小: " << list.size() << '\n';

    // 陷阱：悬空引用
    std::cout << "\n5. 初始化列表的生命周期:\n";
    // 错误示例（注释掉）：
    // const auto& danger = std::vector<int>{1, 2, 3}[0];  // 临时对象销毁
    // std::cout << danger;  // 未定义行为
    std::cout << "  注意：绑定到临时对象的引用可能悬空\n";
}

void pitfall_4_function_template_deduction() {
    std::cout << "\n=== 陷阱4：函数模板推导冲突 ===\n\n";

    std::vector<int> v = {1, 2, 3};

    std::cout << "1. auto vs 函数参数推导:\n";
    auto x = v[0];           // int（拷贝）
    const auto& y = v[0];    // const int&（引用）

    std::cout << "  auto: int\n";
    std::cout << "  const auto&: const int&\n";

    // 模板函数
    auto process = [](auto t) {
        std::cout << "    类型: " << typeid(t).name() << '\n';
        return t;
    };

    std::cout << "\n2. 模板函数推导:\n";
    process(v[0]);  // T推导为int

    std::cout << "\n3. 显式指定类型:\n";
    // 注意：泛型lambda不能直接显式指定模板参数
    // 需要用其他方式，比如包装成std::function或使用模板函数
    auto const_ref_process = [](const auto& t) {
        std::cout << "    类型: " << typeid(t).name() << '\n';
        return t;
    };
    const_ref_process(v[0]);  // 强制为const int&
}

void pitfall_5_decltype_auto_dangling() {
    std::cout << "\n=== 陷阱5：decltype(auto)悬空引用 ===\n\n";

    std::cout << "1. 危险：返回局部变量的引用\n";

    // 错误示例（编译时可能被捕获，也可能不被捕获）
    /*
    auto dangerous = []() -> decltype(auto) {
        int x = 42;
        return (x);  // 返回局部变量的引用！未定义行为
    };
    */

    std::cout << "  返回 (x) 而不是 x 会创建引用\n";

    std::cout << "\n2. 正确做法:\n";
    auto safe_by_value = []() -> int {
        int x = 42;
        return x;  // 返回int
    };

    auto safe_by_static_ref = []() -> decltype(auto) {
        static int x = 42;
        return (x);  // OK：静态变量
    };

    std::cout << "  safe_by_value: " << safe_by_value() << '\n';
    std::cout << "  safe_by_static_ref: " << safe_by_static_ref() << '\n';

    std::cout << "\n3. decltype(auto)的括号陷阱:\n";
    int value = 100;
    decltype(auto) ref1 = value;     // int
    decltype(auto) ref2 = (value);   // int&

    std::cout << "  decltype(auto) ref1 = value: int\n";
    std::cout << "  decltype(auto) ref2 = (value): int&\n";
}

void pitfall_6_std_function_vs_auto() {
    std::cout << "\n=== 陷阱6：std::function vs auto ===\n\n";

    auto lambda = [](int x) { return x * 2; };

    std::cout << "1. 类型差异:\n";
    auto lambda_copy = lambda;           // lambda的具体类型（未知类型名）
    std::function<int(int)> func = lambda;  // std::function包装

    std::cout << "  sizeof(lambda): " << sizeof(lambda) << " 字节\n";
    std::cout << "  sizeof(std::function): " << sizeof(func) << " 字节\n";
    std::cout << "  std::function有额外开销！\n";

    std::cout << "\n2. 性能考虑:\n";
    std::cout << "  auto: 零开销，直接内联\n";
    std::cout << "  std::function: 虚函数调用开销\n";

    std::cout << "\n3. 存储需求:\n";
    std::cout << "  auto: 必须在同一作用域内使用\n";
    std::cout << "  std::function: 可以存储在容器中\n";
}

int main() {
    pitfall_1_unexpected_copy();
    pitfall_2_proxy_types();
    pitfall_3_initializer_list();
    pitfall_4_function_template_deduction();
    pitfall_5_decltype_auto_dangling();
    pitfall_6_std_function_vs_auto();

    std::cout << "\n=== 总结 ===\n";
    std::cout << "\n使用auto的最佳实践:\n";
    std::cout << "1. 范围for循环优先用 const auto&\n";
    std::cout << "2. 需要修改时用 auto&\n";
    std::cout << "3. 注意初始化列表的特殊推导\n";
    std::cout << "4. 小心decltype(auto)的括号规则\n";
    std::cout << "5. 优先用auto而不是std::function（除非需要存储）\n";

    return 0;
}
