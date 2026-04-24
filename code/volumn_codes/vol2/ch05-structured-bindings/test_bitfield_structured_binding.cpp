// 验证结构化绑定支持位域（bit field）
// 修正文档中的错误断言

#include <iostream>
#include <cstdint>

struct BitFieldStruct {
    uint32_t value;
    uint32_t flags : 8;  // 位域
    uint32_t status : 1;
};

int main() {
    BitFieldStruct s{12345, 0xFF, 1};

    // 结构化绑定完全支持位域
    auto [v, f, st] = s;

    std::cout << "value: " << v << ", flags: " << (int)f << ", status: " << st << std::endl;

    return 0;
}
