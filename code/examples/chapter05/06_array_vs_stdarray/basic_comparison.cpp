#include <iostream>
#include <array>
#include <algorithm>
#include <cstring>
#include <iterator>

// std::array vs C数组的比较

void c_array_demo() {
    std::cout << "=== C Array Demo ===\n\n";

    // 声明和初始化
    int arr1[5] = {1, 2, 3, 4, 5};
    int arr2[] = {10, 20, 30};  // 自动推断大小
    int arr3[10] = {0};         // 零初始化

    std::cout << "arr1: ";
    for (int i = 0; i < 5; ++i) {
        std::cout << arr1[i] << " ";
    }
    std::cout << "\n";

    // 数组大小
    std::cout << "sizeof(arr1) = " << sizeof(arr1) << "\n";
    std::cout << "Element count = " << sizeof(arr1) / sizeof(arr1[0]) << "\n";

    // 数组退化（危险！）
    int* ptr = arr1;  // 退化为指针
    std::cout << "ptr points to: " << ptr << "\n";
    std::cout << "sizeof(ptr) = " << sizeof(ptr) << " (lost size info!)\n";

    // 边界检查：无！
    // arr1[10] = 99;  // 未定义行为，越界访问

    // 不能直接复制
    int arr4[5];
    // arr4 = arr1;     // 编译错误！
    std::memcpy(arr4, arr1, sizeof(arr1));  // 需要memcpy

    // 不能直接比较
    // if (arr4 == arr1) { }  // 比较的是地址，不是内容
}

void std_array_demo() {
    std::cout << "\n=== std::array Demo ===\n\n";

    // 声明和初始化
    std::array<int, 5> arr1 = {1, 2, 3, 4, 5};
    std::array<int, 3> arr2 = {10, 20, 30};
    std::array<int, 10> arr3{};  // 零初始化

    std::cout << "arr1: ";
    for (size_t i = 0; i < arr1.size(); ++i) {
        std::cout << arr1[i] << " ";
    }
    std::cout << "\n";

    // 数组大小
    std::cout << "arr1.size() = " << arr1.size() << "\n";
    std::cout << "arr1.max_size() = " << arr1.max_size() << "\n";
    std::cout << "sizeof(arr1) = " << sizeof(arr1) << "\n";
    std::cout << "empty? " << (arr1.empty() ? "yes" : "no") << "\n";

    // 不退化，保持类型信息
    auto& ref = arr1;
    std::cout << "sizeof(ref) = " << sizeof(ref) << " (preserved!)\n";

    // 边界检查（at方法）
    try {
        std::cout << "arr1.at(2) = " << arr1.at(2) << "\n";
        std::cout << "arr1.at(10) = ";  // 越界
        std::cout << arr1.at(10) << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "caught exception: " << e.what() << "\n";
    }

    // 可以直接复制
    std::array<int, 5> arr4 = arr1;
    std::cout << "\narr4 (copy of arr1): ";
    for (int v : arr4) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    // 可以直接比较
    std::array<int, 5> arr5 = {1, 2, 3, 4, 5};
    std::cout << "arr4 == arr5? " << (arr4 == arr5 ? "yes" : "no") << "\n";

    // 可以赋值
    arr4 = {10, 20, 30, 40, 50};
    std::cout << "After assignment, arr4: ";
    for (int v : arr4) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}

void iteration_demo() {
    std::cout << "\n=== Iteration Comparison ===\n\n";

    // C数组
    int c_arr[] = {1, 2, 3, 4, 5};

    std::cout << "C array:\n";
    std::cout << "  Index loop: ";
    for (size_t i = 0; i < sizeof(c_arr) / sizeof(c_arr[0]); ++i) {
        std::cout << c_arr[i] << " ";
    }
    std::cout << "\n";

    std::cout << "  Range-based for (C++11): ";
    for (int v : c_arr) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    // std::array
    std::array<int, 5> std_arr = {1, 2, 3, 4, 5};

    std::cout << "\nstd::array:\n";
    std::cout << "  Index loop: ";
    for (size_t i = 0; i < std_arr.size(); ++i) {
        std::cout << std_arr[i] << " ";
    }
    std::cout << "\n";

    std::cout << "  Range-based for: ";
    for (int v : std_arr) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    std::cout << "  Iterator: ";
    for (auto it = std_arr.begin(); it != std_arr.end(); ++it) {
        std::cout << *it << " ";
    }
    std::cout << "\n";

    std::cout << "  Reverse iterator: ";
    for (auto it = std_arr.rbegin(); it != std_arr.rend(); ++it) {
        std::cout << *it << " ";
    }
    std::cout << "\n";

    std::cout << "  std::for_each: ";
    std::for_each(std_arr.begin(), std_arr.end(), [](int v) {
        std::cout << v << " ";
    });
    std::cout << "\n";
}

void stl_integration_demo() {
    std::cout << "\n=== STL Integration ===\n\n";

    std::array<int, 10> arr = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};

    std::cout << "Original: ";
    for (int v : arr) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    // 排序
    std::sort(arr.begin(), arr.end());
    std::cout << "Sorted:   ";
    for (int v : arr) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    // 查找
    auto it = std::find(arr.begin(), arr.end(), 7);
    if (it != arr.end()) {
        std::cout << "Found 7 at index " << (it - arr.begin()) << "\n";
    }

    // 二分查找
    bool found = std::binary_search(arr.begin(), arr.end(), 5);
    std::cout << "Binary search for 5: " << (found ? "found" : "not found") << "\n";

    // 计数
    std::array<int, 10> arr2 = {1, 2, 2, 3, 2, 4, 2, 5, 2, 6};
    size_t count = std::count(arr2.begin(), arr2.end(), 2);
    std::cout << "Count of 2 in arr2: " << count << "\n";

    // 填充
    std::array<int, 5> fill_arr;
    fill_arr.fill(42);
    std::cout << "Filled array: ";
    for (int v : fill_arr) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    // 交换
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b = {10, 20, 30, 40, 50};
    std::cout << "\nBefore swap:\n";
    std::cout << "  a: ";
    for (int v : a) std::cout << v << " ";
    std::cout << "\n  b: ";
    for (int v : b) std::cout << v << " ";
    std::cout << "\n";

    a.swap(b);
    std::cout << "After swap:\n";
    std::cout << "  a: ";
    for (int v : a) std::cout << v << " ";
    std::cout << "\n  b: ";
    for (int v : b) std::cout << v << " ";
    std::cout << "\n";
}

void data_access_demo() {
    std::cout << "\n=== Data Access ===\n\n";

    std::array<int, 5> arr = {10, 20, 30, 40, 50};

    // data() 返回原始指针
    int* ptr = arr.data();
    std::cout << "Via data(): ";
    for (size_t i = 0; i < arr.size(); ++i) {
        std::cout << ptr[i] << " ";
    }
    std::cout << "\n";

    // front() 和 back()
    std::cout << "front() = " << arr.front() << "\n";
    std::cout << "back() = " << arr.back() << "\n";

    // 与C API互操作
    std::array<char, 64> buf;
    std::strcpy(buf.data(), "Hello, std::array!");
    std::cout << "String: " << buf.data() << "\n";

    // 使用memcpy
    std::array<int, 5> src = {1, 2, 3, 4, 5};
    std::array<int, 5> dst;
    std::memcpy(dst.data(), src.data(), sizeof(src));
    std::cout << "Copied: ";
    for (int v : dst) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}

template<size_t N>
void print_array_size(const std::array<int, N>&) {
    std::cout << "Array size (template param): " << N << "\n";
}

void constexpr_demo() {
    std::cout << "\n=== Compile-Time Features ===\n\n";

    // 编译期构造
    constexpr std::array<int, 5> fib = [] {
        std::array<int, 5> arr{};
        arr[0] = 0;
        arr[1] = 1;
        for (size_t i = 2; i < arr.size(); ++i) {
            arr[i] = arr[i-1] + arr[i-2];
        }
        return arr;
    }();

    std::cout << "Compile-time Fibonacci: ";
    for (int v : fib) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    // 编译期大小检查
    static_assert(fib.size() == 5, "Size must be 5");

    // 作为模板参数
    std::array<int, 10> arr;
    print_array_size(arr);
}

int main() {
    c_array_demo();
    std_array_demo();
    iteration_demo();
    stl_integration_demo();
    data_access_demo();
    constexpr_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. std::array provides size info, doesn't decay to pointer\n";
    std::cout << "2. Boundary checking with at() method\n";
    std::cout << "3. Copyable, comparable, assignable\n";
    std::cout << "4. Full STL algorithm support\n";
    std::cout << "5. Zero overhead compared to C arrays\n";
    std::cout << "6. Prefer std::array except in low-level startup code\n";

    return 0;
}
