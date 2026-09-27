// uint8_promotion.cpp -- +id 触发整型提升，把 uint8_t 打印成数字而不是字符
// Standard: C++17

#include <cstdint>
#include <iostream>

int main() {
    std::uint8_t id = 65;

    std::cout << "without + (raw uint8_t): " << id << "\n";
    std::cout << "with +    (promoted)   : " << +id << "\n";

    return 0;
}
