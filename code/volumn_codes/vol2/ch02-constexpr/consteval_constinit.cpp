// 对应文档：03-consteval-constinit.md
// 演示 C++20 consteval 和 constinit
// 编译成功即表示所有 static_assert 通过
// 需要 C++20 编译器

#include <array>
#include <cstddef>
#include <cstdint>

// --- consteval：强制编译期求值 ---

consteval int square(int x)
{
    return x * x;
}

constexpr int kResult = square(8);
static_assert(kResult == 64);

// --- consteval 编译期哈希 ---

consteval std::uint32_t fnv1a32(const char* str, std::size_t len)
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

template <std::size_t N>
consteval std::uint32_t command_id(const char (&s)[N])
{
    return fnv1a32(s, N - 1);
}

constexpr auto kIdStart = command_id("START");
constexpr auto kIdStop  = command_id("STOP");
constexpr auto kIdReset = command_id("RESET");

static_assert(kIdStart != kIdStop);
static_assert(kIdStart != kIdReset);
static_assert(kIdStop != kIdReset);

// --- consteval 编译期配置校验 ---

consteval int validate_buffer_size(int size)
{
    return size > 0 && size <= 4096 && (size & (size - 1)) == 0
        ? size
        : throw "Buffer size must be a power of 2 between 1 and 4096";
}

constexpr int kBufferSize = validate_buffer_size(1024);
static_assert(kBufferSize == 1024);

// --- consteval 传播规则 ---

consteval int forced_compile_time(int x) { return x * x; }

consteval int double_square(int x)
{
    return forced_compile_time(x) * 2;
}

constexpr auto kVal = double_square(3);
static_assert(kVal == 18);

// --- consteval 类型标签 ---

struct PeripheralTag {
    const char* name;
    std::uint32_t base_address;
    std::uint32_t clock_mask;

    consteval PeripheralTag(const char* n, std::uint32_t addr, std::uint32_t clk)
        : name(n), base_address(addr), clock_mask(clk) {}
};

consteval PeripheralTag make_usart1_tag()
{
    return PeripheralTag{"USART1", 0x40013800, 0x00004000};
}

constexpr auto kUsart1Tag = make_usart1_tag();
static_assert(kUsart1Tag.base_address == 0x40013800);

// --- constinit：编译期初始化 ---

constinit std::array<int, 4> g_table = {1, 2, 3, 4};

constexpr int compute_value() { return 42; }
constinit int g_value = compute_value();

// constinit vs constexpr
constexpr int kConstVal = 42;
constinit int gMutableVal = 42;

// --- consteval 哈希函数 ---

consteval std::uint32_t hash_string(const char* s)
{
    std::uint32_t h = 0x811c9dc5u;
    while (*s) {
        h ^= static_cast<std::uint8_t>(*s++);
        h *= 0x01000193u;
    }
    return h;
}

constexpr auto kHashStart = hash_string("START");
constexpr auto kHashStop  = hash_string("STOP");

// --- consteval 配置校验 ---

consteval bool check_config(int baud_rate, int data_bits)
{
    if (baud_rate <= 0 || baud_rate > 4000000) return false;
    if (data_bits < 5 || data_bits > 9) return false;
    return true;
}

static_assert(check_config(115200, 8), "Invalid UART config");

int main()
{
    return 0;
}
