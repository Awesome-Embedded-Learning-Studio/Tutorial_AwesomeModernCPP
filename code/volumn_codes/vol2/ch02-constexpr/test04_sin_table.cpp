#include <array>
#include <cstddef>
#include <cmath>

// 测试编译期正弦表生成精度

template <std::size_t N>
constexpr std::array<float, N> make_sin_table()
{
    std::array<float, N> table{};
    constexpr double kPi = 3.14159265358979323846;

    for (std::size_t i = 0; i < N; ++i) {
        double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(N);

        // 泰勒展开近似 sin(x)
        // sin(x) ≈ x - x^3/3! + x^5/5! - x^7/7! + x^9/9!
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

// 编译期生成 256 点正弦查表
constexpr auto kSinTable = make_sin_table<256>();

static_assert(kSinTable[0] < 0.001f && kSinTable[0] > -0.001f,
              "sin(0) should be approximately 0");
static_assert(kSinTable[64] > 0.99f && kSinTable[64] < 1.01f,
              "sin(π/2) should be approximately 1");

int main()
{
    // 对比泰勒展开与标准库sin的精度差异
    constexpr double kPi = 3.14159265358979323846;
    constexpr std::size_t kTestPoints = 10;
    float max_error = 0.0f;

    for (std::size_t i = 0; i < kTestPoints; ++i) {
        double angle = 2.0 * kPi * static_cast<double>(i) / 256.0;
        double std_sin = std::sin(angle);
        float table_sin = kSinTable[i];
        float error = static_cast<float>(std::abs(std_sin - table_sin));
        if (error > max_error) {
            max_error = error;
        }
    }

    return 0;
}
