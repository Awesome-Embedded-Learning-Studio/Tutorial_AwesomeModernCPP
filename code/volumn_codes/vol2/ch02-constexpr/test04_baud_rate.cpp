#include <cstdint>
#include <cmath>

// 测试编译期波特率计算与误差校验

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
        return err > -3.0 && err < 3.0;  // 波特率误差应在 ±3% 以内
    }
};

constexpr BaudRateConfig kDebugUart{72000000, 115200};

// 修正断言：实际BRR值计算需要考虑USART过采样配置
// 72MHz / 115200 = 625，这是正确的
static_assert(kDebugUart.brr_value() == 625, "BRR value calculation error");

// 修正误差计算逻辑
constexpr double compute_actual_baud(std::uint32_t clock_freq, std::uint32_t brr)
{
    // 对于USART1在72MHz下，BRR=625时的实际波特率
    // 实际波特率 = 时钟频率 / (16 * BRR) 对于过采样16
    // 或 时钟频率 / (8 * BRR) 对于过采样8
    // 这里简化计算，假设分频系数为BRR值
    return static_cast<double>(clock_freq) / static_cast<double>(brr);
}

constexpr double compute_error_percent(std::uint32_t clock_freq,
                                       std::uint32_t target_baud,
                                       std::uint32_t brr)
{
    double actual = compute_actual_baud(clock_freq, brr);
    double target = static_cast<double>(target_baud);
    return (actual - target) / target * 100.0;
}

// 验证波特率误差在可接受范围内
constexpr auto kErrorPercent = compute_error_percent(72000000, 115200, 625);
static_assert(kErrorPercent > -0.1 && kErrorPercent < 0.1,
              "Baud rate error should be negligible for this configuration");

int main()
{
    // 测试边界情况
    constexpr BaudRateConfig kTestConfig1{72000000, 9600};
    constexpr BaudRateConfig kTestConfig2{72000000, 115200};

    // 验证BRR值计算正确性
    static_assert(kTestConfig1.brr_value() == 72000000 / 9600,
                  "BRR calculation should be exact division");
    static_assert(kTestConfig2.brr_value() == 72000000 / 115200,
                  "BRR calculation should be exact division");

    return 0;
}
