// c_api_compatible.cpp - 演示 std::array 与 C API 的兼容性
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

// 模拟典型的 C API
extern "C" {
    // 传统 C 风格 API
    void c_api_process(uint8_t* buffer, size_t length);

    // const 正确的 C API
    void c_api_read_only(const uint8_t* data, size_t length);
}

void c_api_process(uint8_t* buffer, size_t length) {
    std::cout << "C API processing " << length << " bytes:\n";
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = static_cast<uint8_t>(buffer[i] * 2);
    }
}

void c_api_read_only(const uint8_t* data, size_t length) {
    std::cout << "C API reading " << length << " bytes:\n";
    for (size_t i = 0; i < length; ++i) {
        std::cout << std::hex << int(data[i]) << ' ';
        if ((i + 1) % 16 == 0) std::cout << '\n';
    }
    std::cout << std::dec << '\n';
}

int main() {
    // C 风格数组隐式退化为指针
    uint8_t c_array[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    c_api_process(c_array, 16);

    // std::array 不会隐式退化，必须使用 .data()
    std::array<uint8_t, 16> cpp_array = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    // 正确方式：使用 .data() 和 .size()
    c_api_process(cpp_array.data(), cpp_array.size());

    // 只读访问：.data() 返回指向元素的指针
    c_api_read_only(cpp_array.data(), cpp_array.size());

    // 对比：std::vector 也使用相同模式
    // std::vector<uint8_t> vec(16, 42);
    // c_api_process(vec.data(), vec.size());

    std::cout << "After processing:\n";
    for (size_t i = 0; i < cpp_array.size(); ++i) {
        std::cout << "cpp_array[" << i << "] = " << int(cpp_array[i]) << '\n';
    }

    return 0;
}
