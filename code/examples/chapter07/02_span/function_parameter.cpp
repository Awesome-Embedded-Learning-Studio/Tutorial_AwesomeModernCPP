// function_parameter.cpp - 函数参数最佳实践演示
#include <span>
#include <vector>
#include <array>
#include <iostream>

// 推荐方式：明确表达不修改数据的意图
void process(std::span<const int> data) {
    std::cout << "Processing " << data.size() << " elements: ";
    for (auto v : data) {
        std::cout << v << ' ';
    }
    std::cout << '\n';
}

// 明确表达会修改数据的意图
void mutate(std::span<int> data) {
    std::cout << "Mutating " << data.size() << " elements\n";
    for (auto& v : data) {
        v *= 2;
    }
}

// 对比：传统 C 风格 API（不推荐）
void old_c_style_api(const int* data, size_t length) {
    std::cout << "Old style: " << length << " elements: ";
    for (size_t i = 0; i < length; ++i) {
        std::cout << data[i] << ' ';
    }
    std::cout << '\n';
}

// 对比：模板泛型（会导致代码膨胀）
template<class Container>
void template_api(const Container& c) {
    std::cout << "Template API: " << c.size() << " elements: ";
    for (auto v : c) {
        std::cout << v << ' ';
    }
    std::cout << '\n';
}

int main() {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::array<int, 4> arr = {10, 20, 30, 40};
    int c_array[] = {100, 200, 300};

    std::cout << "=== Using std::span API (recommended) ===\n";
    process(vec);
    process(arr);
    process(c_array);

    std::cout << "\n=== After mutation ===\n";
    mutate(vec);
    process(vec);

    std::cout << "\n=== Using old C style API ===\n";
    old_c_style_api(vec.data(), vec.size());
    old_c_style_api(arr.data(), arr.size());

    std::cout << "\n=== Using template API (code bloat) ===\n";
    template_api(vec);
    template_api(arr);

    std::cout << "\n=== Advantages of std::span ===\n";
    std::cout << "- Single function implementation (no code bloat)\n";
    std::cout << "- Type and size information bundled together\n";
    std::cout << "- No copying of data\n";
    std::cout << "- Clear const-correctness\n";

    return 0;
}
