// 配套 06-designated-initializers.md「基本语法 + 必须按声明顺序」
// C++20 指定初始化器用 .field = value,designator 必须按成员声明顺序(这点和 C99 不同)
// 编译运行:g++ -std=c++20 -Wall -Wextra designated_basics.cpp -o db && ./db
// 复现乱序报错:g++ -std=c++20 -Wall -Wextra -DOUT_OF_ORDER designated_basics.cpp -o db_oob
#include <cstdint>
#include <cstdio>

struct UartConfig {
    std::uint32_t baudrate = 0;
    std::uint8_t data_bits = 8;
    std::uint8_t parity = 0; // 0=None 1=Odd 2=Even
    std::uint8_t stop_bits = 1;
};

int main() {
    // 按声明顺序逐个指定,自解释,不依赖位置
    UartConfig cfg{
        .baudrate = 115200,
        .data_bits = 8,
        .parity = 0,
        .stop_bits = 1,
    };
    std::printf("cfg: baud=%u bits=%u parity=%u stop=%u\n", cfg.baudrate, cfg.data_bits, cfg.parity,
                cfg.stop_bits);

    // 部分初始化:未指定的成员用默认成员初始化器(data_bits=8, stop_bits=1)
    UartConfig partial{.baudrate = 921600, .parity = 2};
    std::printf("partial: baud=%u data_bits=%u(默认8) parity=%u stop=%u(默认1)\n", partial.baudrate,
                partial.data_bits, partial.parity, partial.stop_bits);

#ifdef OUT_OF_ORDER
    // C++20 编译失败:designator 顺序必须和成员声明顺序一致
    UartConfig bad{.stop_bits = 1, .baudrate = 115200};
    (void)bad;
#endif
}
