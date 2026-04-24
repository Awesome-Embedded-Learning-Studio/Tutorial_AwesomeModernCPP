// 对应文档：02-constexpr-ctor.md
// 演示 constexpr 构造函数与字面类型
// 编译成功即表示所有 static_assert 通过

#include <cstddef>
#include <cstdint>

// --- 字面类型示例 ---

struct Point {
    float x;
    float y;

    constexpr Point(float x_, float y_) : x(x_), y(y_) {}
};

constexpr Point kOrigin{0.0f, 0.0f};
static_assert(kOrigin.x == 0.0f);
static_assert(kOrigin.y == 0.0f);

// --- Color 类型 ---

struct Color {
    std::uint8_t r, g, b, a;

    constexpr Color(std::uint8_t r_, std::uint8_t g_,
                    std::uint8_t b_, std::uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
};

constexpr Color kRed{255, 0, 0};
constexpr Color kGreen{0, 255, 0};
constexpr Color kTransparentBlack{0, 0, 0, 0};

static_assert(kRed.r == 255);
static_assert(kTransparentBlack.a == 0);

// --- BCD 编码转换 ---

struct BcdDecimal {
    unsigned char bcd;

    constexpr explicit BcdDecimal(int decimal) : bcd(0)
    {
        int remainder = decimal;
        int shift = 0;
        while (remainder > 0) {
            bcd |= (remainder % 10) << shift;
            remainder /= 10;
            shift += 4;
        }
    }

    constexpr int to_decimal() const
    {
        int result = 0;
        int multiplier = 1;
        unsigned char temp = bcd;
        while (temp > 0) {
            result += (temp & 0x0F) * multiplier;
            temp >>= 4;
            multiplier *= 10;
        }
        return result;
    }
};

constexpr BcdDecimal kDec42{42};
static_assert(kDec42.bcd == 0x42, "BCD of 42 should be 0x42");
static_assert(kDec42.to_decimal() == 42, "Round-trip conversion should work");

// --- 编译期复数类 ---

struct Complex {
    float real;
    float imag;

    constexpr Complex(float r = 0.0f, float i = 0.0f) : real(r), imag(i) {}

    constexpr Complex operator+(const Complex& other) const
    {
        return Complex{real + other.real, imag + other.imag};
    }

    constexpr Complex operator-(const Complex& other) const
    {
        return Complex{real - other.real, imag - other.imag};
    }

    constexpr Complex operator*(const Complex& other) const
    {
        return Complex{
            real * other.real - imag * other.imag,
            real * other.imag + imag * other.real
        };
    }

    constexpr float magnitude_squared() const
    {
        return real * real + imag * imag;
    }

    constexpr bool operator==(const Complex& other) const
    {
        return real == other.real && imag == other.imag;
    }
};

constexpr Complex kI{0.0f, 1.0f};
constexpr Complex kI_Squared = kI * kI;
static_assert(kI_Squared == Complex{-1.0f, 0.0f}, "i^2 should equal -1");

// 编译期生成复数序列（例如 FFT 的旋转因子）
template <std::size_t N>
constexpr Complex compute_twiddle_factor(std::size_t k)
{
    constexpr double kPi = 3.14159265358979323846;
    double angle = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(N);
    double cos_val = 1.0 - angle * angle / 2.0 + angle*angle*angle*angle / 24.0;
    double sin_val = angle - angle*angle*angle / 6.0 + angle*angle*angle*angle*angle / 120.0;
    return Complex{static_cast<float>(cos_val), static_cast<float>(sin_val)};
}

constexpr Complex kTwiddle = compute_twiddle_factor<8>(1);
static_assert(kTwiddle.magnitude_squared() > 0.99f, "Twiddle factor should be on unit circle");

// --- 编译期日期类 ---

struct Date {
    int year;
    int month;
    int day;

    constexpr Date(int y, int m, int d) : year(y), month(m), day(d) {}

    constexpr bool is_leap_year() const
    {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    constexpr int days_in_month() const
    {
        constexpr int kDays[] = {
            0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
        };
        if (month == 2 && is_leap_year()) {
            return 29;
        }
        return kDays[month];
    }

    constexpr bool is_valid() const
    {
        if (month < 1 || month > 12) return false;
        if (day < 1 || day > days_in_month()) return false;
        if (year < 0) return false;
        return true;
    }
};

constexpr Date kEpoch{1970, 1, 1};
static_assert(kEpoch.is_valid());
static_assert(!kEpoch.is_leap_year());

constexpr Date kY2K{2000, 1, 1};
static_assert(kY2K.is_leap_year(), "2000 is a leap year (divisible by 400)");

constexpr Date kLeapDay{2024, 2, 29};
static_assert(kLeapDay.is_valid(), "2024-02-29 is valid (2024 is a leap year)");

// --- C++14 风格：构造函数体可以有逻辑 ---

struct NewStyle {
    int values[4];
    int sum;

    constexpr NewStyle(int base) : values{}, sum(0)
    {
        for (int i = 0; i < 4; ++i) {
            values[i] = base + i;
            sum += values[i];
        }
    }
};

constexpr NewStyle kObj{10};
static_assert(kObj.values[0] == 10);
static_assert(kObj.values[3] == 13);
static_assert(kObj.sum == 46);  // 10+11+12+13=46

// --- 编译期字符串包装 ---

struct ConstString {
    const char* data;
    std::size_t length;

    template <std::size_t N>
    constexpr ConstString(const char (&str)[N]) : data(str), length(N - 1) {}

    constexpr char operator[](std::size_t i) const
    {
        return i < length ? data[i] : '\0';
    }

    constexpr bool starts_with(char c) const
    {
        return length > 0 && data[0] == c;
    }

    constexpr bool equals(const ConstString& other) const
    {
        if (length != other.length) return false;
        for (std::size_t i = 0; i < length; ++i) {
            if (data[i] != other.data[i]) return false;
        }
        return true;
    }
};

constexpr ConstString kHello{"Hello"};
static_assert(kHello.length == 5);
static_assert(kHello[0] == 'H');
static_assert(kHello.starts_with('H'));
static_assert(kHello.equals(ConstString{"Hello"}));

// --- 嵌入式 UART 配置 ---

enum class Parity { kNone, kEven, kOdd };
enum class StopBits { kOne, kTwo };

struct UartConfig {
    std::uint32_t baud_rate;
    std::uint8_t data_bits;
    StopBits stop_bits;
    Parity parity;

    constexpr UartConfig(std::uint32_t baud, std::uint8_t data,
                         StopBits stop, Parity par)
        : baud_rate(baud), data_bits(data), stop_bits(stop), parity(par) {}

    constexpr bool is_valid() const
    {
        if (baud_rate == 0) return false;
        if (data_bits < 5 || data_bits > 9) return false;
        return true;
    }

    constexpr std::uint32_t compute_brr(std::uint32_t clock_freq) const
    {
        return clock_freq / baud_rate;
    }
};

constexpr UartConfig kDebugUart{115200, 8, StopBits::kOne, Parity::kNone};
constexpr UartConfig kGpsUart{9600, 8, StopBits::kOne, Parity::kNone};

static_assert(kDebugUart.is_valid());
static_assert(kDebugUart.compute_brr(72000000) == 625);  // 72MHz / 115200

int main()
{
    return 0;
}
