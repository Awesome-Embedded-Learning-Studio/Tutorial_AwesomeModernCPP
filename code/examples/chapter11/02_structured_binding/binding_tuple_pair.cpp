// binding_tuple_pair.cpp - tuple和pair的解包应用
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include <algorithm>

void map_insert_binding() {
    std::cout << "=== map::insert返回值解包 ===\n\n";

    std::map<int, std::string> m;

    // 传统写法
    auto result_old = m.insert({1, "one"});
    if (result_old.second) {
        std::cout << "插入成功: " << result_old.first->second << '\n';
    }

    // 结构化绑定写法
    auto [it, success] = m.insert({2, "two"});
    if (success) {
        std::cout << "插入成功: " << it->second << '\n';
    }

    // 尝试重复插入
    auto [it2, success2] = m.insert({2, "TWO"});
    if (!success2) {
        std::cout << "插入失败: 键 " << it2->first << " 已存在\n";
    }
}

void map_iteration_binding() {
    std::cout << "\n=== map遍历解包 ===\n\n";

    std::map<std::string, int> scores = {
        {"Alice", 95},
        {"Bob", 87},
        {"Charlie", 92}
    };

    std::cout << "传统写法（使用迭代器）:\n";
    for (auto it = scores.begin(); it != scores.end(); ++it) {
        std::cout << "  " << it->first << ": " << it->second << '\n';
    }

    std::cout << "\n结构化绑定写法:\n";
    for (const auto& [name, score] : scores) {
        std::cout << "  " << name << ": " << score << '\n';
    }
}

void multiple_return_values() {
    std::cout << "\n=== 多返回值函数 ===\n\n";

    // 返回多个值的函数
    auto divide = [](int a, int b) {
        if (b == 0) {
            return std::tuple(0, 0.0, false);  // quotient, remainder, success
        }
        return std::tuple(a / b, static_cast<double>(a) / b, true);
    };

    auto [quotient, exact_quotient, success] = divide(10, 3);

    std::cout << "10 / 3:\n";
    std::cout << "  整数商: " << quotient << '\n';
    std::cout << "  精确商: " << exact_quotient << '\n';
    std::cout << "  成功: " << std::boolalpha << success << '\n';

    auto [q2, exact2, ok] = divide(10, 0);
    if (!ok) {
        std::cout << "\n10 / 0: 除零错误\n";
    }
}

void tuple_element_access() {
    std::cout << "\n=== tuple元素访问 ===\n\n";

    auto person = std::make_tuple("Alice", 30, "Engineer");

    // 传统方式
    std::cout << "传统方式:\n";
    std::cout << "  Name: " << std::get<0>(person) << '\n';
    std::cout << "  Age: " << std::get<1>(person) << '\n';
    std::cout << "  Job: " << std::get<2>(person) << '\n';

    // 结构化绑定
    std::cout << "\n结构化绑定:\n";
    auto& [name, age, job] = person;
    std::cout << "  Name: " << name << '\n';
    std::cout << "  Age: " << age << '\n';
    std::cout << "  Job: " << job << '\n';

    // 修改元素
    age = 31;
    std::cout << "\n修改age后:\n";
    std::cout << "  std::get<1>(person) = " << std::get<1>(person) << '\n';
}

void pair_algorithms() {
    std::cout << "\n=== pair算法应用 ===\n\n";

    // minmax_element返回pair
    std::vector<int> v = {3, 1, 4, 1, 5, 9, 2, 6};

    auto [min_it, max_it] = std::minmax_element(v.begin(), v.end());

    std::cout << "最小值: " << *min_it << '\n';
    std::cout << "最大值: " << *max_it << '\n';

    // equal_range返回pair
    std::map<int, std::string> m = {
        {1, "one"}, {2, "two"}, {3, "three"}, {4, "four"}
    };

    auto [lower, upper] = m.equal_range(3);
    std::cout << "\nequal_range(3):\n";
    for (auto it = lower; it != upper; ++it) {
        std::cout << "  " << it->first << " -> " << it->second << '\n';
    }
}

void nested_tuples() {
    std::cout << "\n=== 嵌套tuple ===\n\n";

    auto nested = std::make_tuple(
        std::make_tuple(1, 2),
        std::make_tuple(3, 4),
        5
    );

    auto& [pair1, pair2, value] = nested;
    auto& [a, b] = pair1;
    auto& [c, d] = pair2;

    std::cout << "嵌套tuple解包:\n";
    std::cout << "  pair1: (" << a << ", " << b << ")\n";
    std::cout << "  pair2: (" << c << ", " << d << ")\n";
    std::cout << "  value: " << value << '\n';
}

void custom_tuple_like() {
    std::cout << "\n=== 自定义tuple_like类型 ===\n\n";

    // 注意：完整实现自定义tuple_like类型需要在命名空间作用域中
    // 这里演示基本概念

    // 使用标准pair作为例子（已经支持结构化绑定）
    std::pair<std::string, int> entry = {"answer", 42};
    auto& [key, value] = entry;

    std::cout << "  Key: " << key << ", Value: " << value << '\n';
    std::cout << "  标准库的pair/tuple已经支持结构化绑定\n";
    std::cout << "  自定义类型需要实现get函数和tuple_size/tuple_element特化\n";
    std::cout << "  这些必须在命名空间作用域中定义（不能在函数内部）\n";
}

void structured_binding_with_if_init() {
    std::cout << "\n=== if初始化语句中的绑定 (C++17) ===\n\n";

    std::map<int, std::string> cache;

    // if初始化 + 结构化绑定
    if (auto [it, inserted] = cache.insert({5, "five"}); inserted) {
        std::cout << "插入新条目: " << it->second << '\n';
    } else {
        std::cout << "条目已存在: " << it->second << '\n';
    }

    // 再次插入相同的键
    if (auto [it, inserted] = cache.insert({5, "FIVE"}); !inserted) {
        std::cout << "插入失败，现有值: " << it->second << '\n';
    }
}

int main() {
    map_insert_binding();
    map_iteration_binding();
    multiple_return_values();
    tuple_element_access();
    pair_algorithms();
    nested_tuples();
    structured_binding_with_if_init();

    std::cout << "\n=== 演示结束 ===\n";

    std::cout << "\n总结:\n";
    std::cout << "1. map::insert返回值解包特别有用\n";
    std::cout << "2. 遍历map时代码更清晰\n";
    std::cout << "3. 多返回值函数更容易使用\n";
    std::cout << "4. 配合if初始化语句，代码更简洁\n";

    return 0;
}
