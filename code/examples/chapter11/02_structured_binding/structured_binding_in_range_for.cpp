// structured_binding_in_range_for.cpp - 范围for循环中的结构化绑定
#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>
#include <array>
#include <string>

void map_iteration() {
    std::cout << "=== map遍历 ===\n\n";

    std::map<std::string, int> scores = {
        {"Alice", 95},
        {"Bob", 87},
        {"Charlie", 92},
        {"Diana", 88}
    };

    std::cout << "传统写法（迭代器）:\n";
    for (auto it = scores.begin(); it != scores.end(); ++it) {
        std::cout << "  " << it->first << ": " << it->second << '\n';
    }

    std::cout << "\n结构化绑定写法:\n";
    for (const auto& [name, score] : scores) {
        std::cout << "  " << name << ": " << score << '\n';
    }
}

void modify_map_values() {
    std::cout << "\n=== 修改map值 ===\n\n";

    std::map<int, std::string> id_to_name = {
        {1, "One"},
        {2, "Two"},
        {3, "Three"}
    };

    std::cout << "修改前:\n";
    for (const auto& [id, name] : id_to_name) {
        std::cout << "  " << id << " -> " << name << '\n';
    }

    // 使用引用修改
    for (auto& [id, name] : id_to_name) {
        name = "Modified_" + name;
    }

    std::cout << "\n修改后:\n";
    for (const auto& [id, name] : id_to_name) {
        std::cout << "  " << id << " -> " << name << '\n';
    }
}

void vector_of_pairs() {
    std::cout << "\n=== pair vector遍历 ===\n\n";

    std::vector<std::pair<std::string, int>> items = {
        {"Apple", 5},
        {"Banana", 3},
        {"Orange", 7}
    };

    for (const auto& [name, count] : items) {
        std::cout << "  " << name << ": " << count << '\n';
    }
}

void vector_of_structs() {
    std::cout << "\n=== 结构体vector遍历 ===\n\n";

    struct Point {
        int x, y;
        std::string label;
    };

    std::vector<Point> points = {
        {10, 20, "A"},
        {30, 40, "B"},
        {50, 60, "C"}
    };

    std::cout << "按值拷贝（不推荐）:\n";
    for (auto [x, y, label] : points) {
        std::cout << "  " << label << ": (" << x << ", " << y << ")\n";
    }

    std::cout << "\nconst引用（推荐）:\n";
    for (const auto& [x, y, label] : points) {
        std::cout << "  " << label << ": (" << x << ", " << y << ")\n";
    }
}

void array_iteration() {
    std::cout << "\n=== 数组遍历 ===\n\n";

    std::array<std::pair<int, int>, 3> ranges = {{
        {1, 10},
        {20, 30},
        {40, 50}
    }};

    for (const auto& [start, end] : ranges) {
        std::cout << "  范围: [" << start << ", " << end << "]\n";
    }
}

void nested_iteration() {
    std::cout << "\n=== 嵌套遍历 ===\n\n";

    std::map<std::string, std::vector<int>> data = {
        {"Evens", {2, 4, 6, 8}},
        {"Odds", {1, 3, 5, 7}}
    };

    for (const auto& [category, numbers] : data) {
        std::cout << category << ": ";
        for (int n : numbers) {
            std::cout << n << ' ';
        }
        std::cout << '\n';
    }
}

void unordered_map_iteration() {
    std::cout << "\n=== unordered_map遍历 ===\n\n";

    std::unordered_map<std::string, std::string> capitals = {
        {"France", "Paris"},
        {"Germany", "Berlin"},
        {"Italy", "Rome"},
        {"Spain", "Madrid"}
    };

    for (const auto& [country, capital] : capitals) {
        std::cout << "  " << country << " -> " << capital << '\n';
    }
}

void filter_with_binding() {
    std::cout << "\n=== 条件过滤 ===\n\n";

    std::map<std::string, int> inventory = {
        {"Widget", 5},
        {"Gadget", 0},
        {"Tool", 12},
        {"Device", 3}
    };

    std::cout << "库存不足的项目:\n";
    for (const auto& [item, quantity] : inventory) {
        if (quantity < 5) {
            std::cout << "  " << item << ": " << quantity << '\n';
        }
    }
}

void calculate_with_binding() {
    std::cout << "\n=== 计算处理 ===\n\n";

    struct Measurement {
        std::string sensor;
        double value;
        std::string unit;
    };

    std::vector<Measurement> readings = {
        {"Temperature", 25.5, "C"},
        {"Humidity", 60.2, "%"},
        {"Pressure", 1013.25, "hPa"}
    };

    for (const auto& [sensor, value, unit] : readings) {
        std::cout << "  " << sensor << ": " << value << ' ' << unit << '\n';
    }
}

void emplace_with_binding() {
    std::cout << "\n=== emplace返回值 ===\n\n";

    std::map<int, std::string> m;

    // 使用结构化绑定检查emplace结果
    auto print_emplace = [&](int key, const std::string& value) {
        auto [it, success] = m.emplace(key, value);

        if (success) {
            std::cout << "  插入成功: " << key << " -> " << value << '\n';
        } else {
            std::cout << "  插入失败: 键 " << key << " 已存在 -> " << it->second << '\n';
        }
    };

    print_emplace(1, "one");
    print_emplace(2, "two");
    print_emplace(1, "ONE");  // 失败
}

void nested_structures_in_range_for() {
    std::cout << "\n=== 嵌套结构遍历 ===\n\n";

    struct Inner {
        int a, b;
    };

    struct Outer {
        Inner inner;
        std::string name;
    };

    std::vector<Outer> data = {
        {{1, 2}, "First"},
        {{3, 4}, "Second"},
        {{5, 6}, "Third"}
    };

    for (const auto& [inner, name] : data) {
        auto [a, b] = inner;  // 再次解包
        std::cout << "  " << name << ": (" << a << ", " << b << ")\n";
    }
}

void performance_consideration() {
    std::cout << "\n=== 性能考虑 ===\n\n";

    struct LargeData {
        std::array<int, 100> data{};
        int id;
    };

    std::vector<LargeData> container = {
        {LargeData{{}, 1}},
        {LargeData{{}, 2}},
        {LargeData{{}, 3}}
    };

    std::cout << "1. 按值拷贝 (auto) - 每次循环拷贝整个结构体:\n";
    std::cout << "   for (auto [data, id] : container) { ... }\n";

    std::cout << "\n2. const引用 (const auto&) - 零拷贝:\n";
    std::cout << "   for (const auto& [data, id] : container) { ... }\n";

    // 实际演示
    for (const auto& [data, id] : container) {
        std::cout << "  ID: " << id << " (零拷贝访问)\n";
    }
}

int main() {
    map_iteration();
    modify_map_values();
    vector_of_pairs();
    vector_of_structs();
    array_iteration();
    nested_iteration();
    unordered_map_iteration();
    filter_with_binding();
    calculate_with_binding();
    emplace_with_binding();
    nested_structures_in_range_for();
    performance_consideration();

    std::cout << "\n=== 演示结束 ===\n";

    std::cout << "\n总结:\n";
    std::cout << "1. 结构化绑定让范围for循环更清晰\n";
    std::cout << "2. 特别适合遍历map、pair、结构体\n";
    std::cout << "3. 使用const auto&避免拷贝大型结构\n";
    std::cout << "4. 可以直接修改元素（使用auto&）\n";
    std::cout << "5. 配合条件语句实现过滤逻辑\n";

    return 0;
}
