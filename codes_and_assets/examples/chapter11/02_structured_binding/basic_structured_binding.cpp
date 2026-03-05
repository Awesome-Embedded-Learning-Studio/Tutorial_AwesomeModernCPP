// basic_structured_binding.cpp - 结构化绑定基础用法
#include <iostream>
#include <array>
#include <tuple>
#include <utility>

void array_binding() {
    std::cout << "=== 数组绑定 ===\n\n";

    int arr[3] = {1, 2, 3};
    auto [x, y, z] = arr;

    std::cout << "数组解包:\n";
    std::cout << "  x = " << x << '\n';
    std::cout << "  y = " << y << '\n';
    std::cout << "  z = " << z << '\n';

    // 修改绑定变量不影响原数组
    x = 100;
    std::cout << "\n修改x后:\n";
    std::cout << "  x = " << x << '\n';
    std::cout << "  arr[0] = " << arr[0] << "（未改变）\n";

    // 使用引用修改原数组
    auto& [rx, ry, rz] = arr;
    rx = 200;
    std::cout << "\n使用引用修改rx后:\n";
    std::cout << "  arr = {" << arr[0] << ", " << arr[1] << ", " << arr[2] << "}\n";
}

void struct_binding() {
    std::cout << "\n=== 结构体绑定 ===\n\n";

    struct Point {
        int x;
        int y;
        int z;
    };

    Point p{10, 20, 30};
    auto [px, py, pz] = p;

    std::cout << "Point结构体解包:\n";
    std::cout << "  px = " << px << '\n';
    std::cout << "  py = " << py << '\n';
    std::cout << "  pz = " << pz << '\n';

    // 使用引用修改结构体
    auto& [rx, ry, rz] = p;
    rx = 100;
    std::cout << "\n修改rx后:\n";
    std::cout << "  p.x = " << p.x << '\n';

    // const引用（只读访问）
    const auto& [cx, cy, cz] = p;
    std::cout << "  cx = " << cx << "（const引用）\n";
}

void tuple_binding() {
    std::cout << "\n=== 元组绑定 ===\n\n";

    auto t = std::make_tuple(42, "hello", 3.14);
    auto [id, name, value] = t;

    std::cout << "tuple解包:\n";
    std::cout << "  id = " << id << '\n';
    std::cout << "  name = " << name << '\n';
    std::cout << "  value = " << value << '\n';

    // 使用std::tie对比
    std::cout << "\n对比std::tie（C++11）:\n";
    int tie_id;
    double tie_value;
    std::string tie_name;
    std::tie(tie_id, tie_name, tie_value) = t;
    std::cout << "  传统方式需要先声明变量\n";

    // 引用修改
    auto& [rid, rname, rvalue] = t;
    rid = 100;
    std::cout << "\n修改rid后:\n";
    std::cout << "  std::get<0>(t) = " << std::get<0>(t) << '\n';
}

void pair_binding() {
    std::cout << "\n=== pair绑定 ===\n\n";

    auto p = std::make_pair("key", 42);
    auto [key, value] = p;

    std::cout << "pair解包:\n";
    std::cout << "  key = " << key << '\n';
    std::cout << "  value = " << value << '\n';

    // map::insert返回值
    std::cout << "\nmap::insert返回值:\n";
    // 示例（需要map头文件）
    // auto [it, success] = map.insert(...);
}

void binding_with_const_and_references() {
    std::cout << "\n=== const和引用 ===\n\n";

    struct Data {
        int a, b, c;
    };

    Data d{1, 2, 3};

    // 按值拷贝
    auto [v1, v2, v3] = d;
    std::cout << "1. 按值拷贝: 创建新变量\n";

    // 引用（可修改）
    auto& [r1, r2, r3] = d;
    r1 = 10;
    std::cout << "2. 引用: d.a = " << d.a << '\n';

    // const引用（只读，避免拷贝）
    const auto& [c1, c2, c3] = d;
    std::cout << "3. const引用: 只读访问\n";

    // 右值引用（移动语义）
    auto&& [rr1, rr2, rr3] = Data{4, 5, 6};
    std::cout << "4. 右值引用: 可以绑定临时对象\n";
}

void nested_structures() {
    std::cout << "\n=== 嵌套结构 ===\n\n";

    struct Inner {
        int x, y;
    };

    struct Outer {
        Inner inner;
        double z;
    };

    Outer o{{10, 20}, 3.14};

    // 先解包外层，再解包内层
    auto [inner_ref, z_val] = o;
    auto [x_val, y_val] = inner_ref;

    std::cout << "嵌套结构:\n";
    std::cout << "  x = " << x_val << '\n';
    std::cout << "  y = " << y_val << '\n';
    std::cout << "  z = " << z_val << '\n';
}

void binding_in_range_for() {
    std::cout << "\n=== 范围for中的绑定 ===\n\n";

    struct Point {
        int x, y;
    };

    std::array<Point, 3> points = {{
        {1, 2},
        {3, 4},
        {5, 6}
    }};

    std::cout << "遍历点数组:\n";
    for (const auto& [x, y] : points) {
        std::cout << "  (" << x << ", " << y << ")\n";
    }

    std::cout << "\n修改点的值:\n";
    for (auto& [x, y] : points) {
        x *= 10;
        y *= 10;
    }

    for (const auto& [x, y] : points) {
        std::cout << "  (" << x << ", " << y << ")\n";
    }
}

int main() {
    array_binding();
    struct_binding();
    tuple_binding();
    pair_binding();
    binding_with_const_and_references();
    nested_structures();
    binding_in_range_for();

    std::cout << "\n=== 演示结束 ===\n";

    std::cout << "\n总结:\n";
    std::cout << "1. 数组、结构体、tuple、pair都可以解包\n";
    std::cout << "2. auto按值，auto&引用，const auto&常量引用\n";
    std::cout << "3. 默认创建拷贝，用引用避免拷贝\n";
    std::cout << "4. 在范围for中特别有用\n";

    return 0;
}
