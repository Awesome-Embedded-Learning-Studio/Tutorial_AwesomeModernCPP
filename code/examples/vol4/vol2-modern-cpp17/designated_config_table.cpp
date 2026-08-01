// 配套 06-designated-initializers.md「嵌入式实战:constexpr 配置表与寄存器映射」
// 指定初始化器 + constexpr 是嵌入式「配置表」模式的标准搭子,编译期定死、运行时零开销
// 编译运行:g++ -std=c++20 -Wall -Wextra designated_config_table.cpp -o dct && ./dct
#include <array>
#include <cstdint>
#include <cstdio>

enum class GpioMode : std::uint8_t { Input, Output, Alternate, Analog };
enum class GpioPull : std::uint8_t { None, Up, Down };

struct PinCfg {
    std::uint8_t pin;
    GpioMode mode;
    GpioPull pull;
    std::uint8_t alternate;
};

// 编译期配置表:每个引脚的配置一目了然,不依赖位置
constexpr std::array<PinCfg, 4> kUartPins = {{
    {.pin = 9, .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
    {.pin = 10, .mode = GpioMode::Alternate, .pull = GpioPull::Up, .alternate = 7},
    {.pin = 2, .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
    {.pin = 3, .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
}};

struct RegMap {
    const char* name;
    std::uint32_t offset;
    bool read_only;
};

constexpr std::array<RegMap, 4> kUartRegs = {{
    {.name = "SR", .offset = 0x00, .read_only = true},
    {.name = "DR", .offset = 0x04, .read_only = false},
    {.name = "BRR", .offset = 0x08, .read_only = false},
    {.name = "CR1", .offset = 0x0C, .read_only = false},
}};

int main() {
    std::printf("UART 引脚配置表:\n");
    for (const auto& p : kUartPins) {
        std::printf("  P%d  mode=%d pull=%d af=%d\n", p.pin, static_cast<int>(p.mode),
                    static_cast<int>(p.pull), p.alternate);
    }

    std::printf("\nUART 寄存器映射:\n");
    for (const auto& r : kUartRegs) {
        std::printf("  %-4s @0x%02X  %s\n", r.name, r.offset, r.read_only ? "RO" : "RW");
    }
}
