// 对应文档：04-compile-time-practice.md
// 综合演示编译期查表、字符串哈希、状态机、策略模式
// 编译成功即表示所有 static_assert 通过

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// ============================================================
// 第一步：编译期查表
// ============================================================

// --- CRC-32 查找表 ---

constexpr std::array<std::uint32_t, 256> make_crc32_table()
{
    std::array<std::uint32_t, 256> table{};
    constexpr std::uint32_t kPolynomial = 0xEDB88320u;
    for (std::size_t i = 0; i < 256; ++i) {
        std::uint32_t crc = static_cast<std::uint32_t>(i);
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? ((crc >> 1) ^ kPolynomial) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto kCrc32Table = make_crc32_table();

static_assert(kCrc32Table[0] == 0x00000000u, "CRC table entry 0 should be 0");
static_assert(kCrc32Table[1] == 0x77073096u, "CRC table entry 1 mismatch");
static_assert(kCrc32Table[255] == 0x2D02EF8Du, "CRC table entry 255 mismatch");

constexpr std::uint32_t crc32(const std::uint8_t* data, std::size_t length)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        std::uint8_t index = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ kCrc32Table[index];
    }
    return crc ^ 0xFFFFFFFFu;
}

// --- 正弦函数查表 ---

template <std::size_t N>
constexpr std::array<float, N> make_sin_table()
{
    std::array<float, N> table{};
    constexpr double kPi = 3.14159265358979323846;
    for (std::size_t i = 0; i < N; ++i) {
        double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(N);
        double x = angle;
        double term = x;
        double sum = term;
        for (int n = 1; n <= 5; ++n) {
            term *= -x * x / static_cast<double>((2 * n) * (2 * n + 1));
            sum += term;
        }
        table[i] = static_cast<float>(sum);
    }
    return table;
}

constexpr auto kSinTable = make_sin_table<256>();

static_assert(kSinTable[0] < 0.001f && kSinTable[0] > -0.001f,
              "sin(0) should be approximately 0");
static_assert(kSinTable[64] > 0.99f && kSinTable[64] < 1.01f,
              "sin(pi/2) should be approximately 1");

constexpr float fast_sin_index(std::size_t index)
{
    return kSinTable[index & 0xFF];
}

// ============================================================
// 第二步：编译期字符串哈希
// ============================================================

constexpr std::uint32_t fnv1a32(const char* str, std::size_t len)
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

template <std::size_t N>
constexpr std::uint32_t str_hash(const char (&s)[N])
{
    return fnv1a32(s, N - 1);
}

constexpr auto kHashInit  = str_hash("INIT");
constexpr auto kHashStart = str_hash("START");
constexpr auto kHashStop  = str_hash("STOP");
constexpr auto kHashReset = str_hash("RESET");

static_assert(kHashInit != kHashStart, "Hash collision detected");
static_assert(kHashInit != kHashStop, "Hash collision detected");
static_assert(kHashStart != kHashStop, "Hash collision detected");
static_assert(kHashStart != kHashReset, "Hash collision detected");

// 运行时命令分派
void dispatch_command(const char* cmd)
{
    std::uint32_t h = fnv1a32(cmd, std::strlen(cmd));
    switch (h) {
        case kHashInit:  /* handle INIT */  break;
        case kHashStart: /* handle START */ break;
        case kHashStop:  /* handle STOP */  break;
        case kHashReset: /* handle RESET */ break;
        default: /* unknown command */ break;
    }
}

// ============================================================
// 第三步：编译期状态机
// ============================================================

enum class State : std::uint8_t { Idle, Debouncing, Pressed, Count };
enum class Event : std::uint8_t { Press, Release, Timeout, Count };

struct Transition {
    State from;
    Event trigger;
    State to;
};

constexpr std::array<Transition, 5> kDebounceTable = {{
    {State::Idle,       Event::Press,   State::Debouncing},
    {State::Debouncing, Event::Timeout, State::Pressed},
    {State::Debouncing, Event::Release, State::Idle},
    {State::Pressed,    Event::Release, State::Idle},
    {State::Pressed,    Event::Timeout, State::Idle},
}};

template <std::size_t N>
constexpr bool has_duplicate_transitions(const std::array<Transition, N>& table)
{
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (table[i].from == table[j].from &&
                table[i].trigger == table[j].trigger) {
                return true;
            }
        }
    }
    return false;
}

template <std::size_t N>
constexpr bool all_states_have_transitions(const std::array<Transition, N>& table)
{
    constexpr std::size_t kStateCount = static_cast<std::size_t>(State::Count);
    bool found[kStateCount] = {};
    for (std::size_t i = 0; i < N; ++i) {
        found[static_cast<std::size_t>(table[i].from)] = true;
    }
    for (std::size_t s = 0; s < kStateCount; ++s) {
        if (!found[s]) return false;
    }
    return true;
}

static_assert(!has_duplicate_transitions(kDebounceTable),
              "Duplicate (state, event) pairs found in transition table");
static_assert(all_states_have_transitions(kDebounceTable),
              "Some states have no outgoing transitions");

// 运行时状态机引擎
class DebounceFsm {
public:
    constexpr DebounceFsm() : state_(State::Idle) {}

    void handle(Event ev)
    {
        for (const auto& t : kDebounceTable) {
            if (t.from == state_ && t.trigger == ev) {
                state_ = t.to;
                return;
            }
        }
    }

    constexpr State current_state() const { return state_; }

private:
    State state_;
};

// ============================================================
// 第四步：编译期策略模式
// ============================================================

struct Crc32Strategy {
    static constexpr const char* name = "CRC-32";

    static constexpr std::uint32_t compute(const std::uint8_t* data, std::size_t len)
    {
        constexpr std::uint32_t kPoly = 0xEDB88320u;
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t idx = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFF);
            std::uint32_t entry = static_cast<std::uint32_t>(idx);
            for (int j = 0; j < 8; ++j) {
                entry = (entry & 1) ? ((entry >> 1) ^ kPoly) : (entry >> 1);
            }
            crc = (crc >> 8) ^ entry;
        }
        return crc ^ 0xFFFFFFFFu;
    }
};

struct Crc16CcittStrategy {
    static constexpr const char* name = "CRC-16-CCITT";

    static constexpr std::uint16_t compute(const std::uint8_t* data, std::size_t len)
    {
        constexpr std::uint16_t kPoly = 0x1021u;
        std::uint16_t crc = 0xFFFFu;
        for (std::size_t i = 0; i < len; ++i) {
            crc ^= static_cast<std::uint16_t>(data[i]) << 8;
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 0x8000) ? ((crc << 1) ^ kPoly) : (crc << 1);
            }
        }
        return crc;
    }
};

template <typename Strategy>
constexpr auto checksum(const std::uint8_t* data, std::size_t len)
{
    return Strategy::compute(data, len);
}

// --- XOR 校验 ---

constexpr std::uint8_t xor_checksum(const std::uint8_t* data, std::size_t len)
{
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < len; ++i) { sum ^= data[i]; }
    return sum;
}

constexpr std::uint8_t kTestData[] = {0x01, 0x02, 0x03, 0x04};
static_assert(xor_checksum(kTestData, 4) == 0x04, "XOR checksum mismatch");

// ============================================================
// 第五步：嵌入式实战
// ============================================================

// --- 编译期寄存器地址计算 ---

struct PeripheralBase {
    std::uint32_t address;

    constexpr explicit PeripheralBase(std::uint32_t addr) : address(addr) {}

    constexpr std::uint32_t offset(std::uint32_t off) const
    {
        return address + off;
    }
};

constexpr PeripheralBase kGpioA{0x40010800};
constexpr PeripheralBase kUsart1{0x40013800};
constexpr PeripheralBase kTimer1{0x40012C00};

struct GpioReg {
    static constexpr std::uint32_t kCrl  = 0x00;
    static constexpr std::uint32_t kCrh  = 0x04;
    static constexpr std::uint32_t kIdr  = 0x08;
    static constexpr std::uint32_t kOdr  = 0x0C;
};

constexpr std::uint32_t kGpioA_Crl = kGpioA.offset(GpioReg::kCrl);
constexpr std::uint32_t kGpioA_Odr = kGpioA.offset(GpioReg::kOdr);

static_assert(kGpioA_Crl == 0x40010800u);
static_assert(kGpioA_Odr == 0x4001080Cu);

// --- 编译期时钟配置校验 ---

struct ClockConfig {
    std::uint32_t hse_freq;
    std::uint32_t pll_mul;
    std::uint32_t ahb_div;
    std::uint32_t apb1_div;

    constexpr ClockConfig(std::uint32_t hse, std::uint32_t mul,
                          std::uint32_t ahb, std::uint32_t apb1)
        : hse_freq(hse), pll_mul(mul), ahb_div(ahb), apb1_div(apb1) {}

    constexpr std::uint32_t sys_clock() const { return hse_freq * pll_mul; }
    constexpr std::uint32_t ahb_clock() const { return sys_clock() / ahb_div; }
    constexpr std::uint32_t apb1_clock() const { return ahb_clock() / apb1_div; }

    constexpr bool is_valid() const
    {
        if (sys_clock() > 72000000u) return false;
        if (apb1_clock() > 36000000u) return false;
        if (pll_mul < 2 || pll_mul > 16) return false;
        return true;
    }
};

constexpr ClockConfig kStandardClock{8000000, 9, 1, 2};

static_assert(kStandardClock.is_valid(), "Invalid clock configuration");
static_assert(kStandardClock.sys_clock() == 72000000u);
static_assert(kStandardClock.apb1_clock() == 36000000u);

// --- 编译期波特率计算与误差校验 ---

struct BaudRateConfig {
    std::uint32_t clock_freq;
    std::uint32_t target_baud;

    constexpr BaudRateConfig(std::uint32_t clk, std::uint32_t baud)
        : clock_freq(clk), target_baud(baud) {}

    constexpr std::uint32_t brr_value() const
    {
        return clock_freq / target_baud;
    }

    constexpr double error_percent() const
    {
        double actual = static_cast<double>(clock_freq / brr_value());
        double target = static_cast<double>(target_baud);
        return (actual - target) / target * 100.0;
    }

    constexpr bool is_acceptable() const
    {
        double err = error_percent();
        return err > -3.0 && err < 3.0;
    }
};

constexpr BaudRateConfig kDebugUart{72000000, 115200};
static_assert(kDebugUart.brr_value() == 625);
static_assert(kDebugUart.is_acceptable(), "Baud rate error too large");

int main()
{
    return 0;
}
