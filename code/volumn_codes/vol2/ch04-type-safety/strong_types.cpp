#include <cstdint>
#include <functional>
#include <iostream>
#include <type_traits>

// === StrongInt 强类型包装器 ===

template <typename Tag, typename Rep = int>
class StrongInt {
public:
    using ValueType = Rep;

    constexpr explicit StrongInt(Rep value = Rep{}) : value_(value) {}
    constexpr Rep get() const noexcept { return value_; }

    constexpr StrongInt& operator++() noexcept { ++value_; return *this; }
    constexpr StrongInt operator++(int) noexcept {
        StrongInt tmp = *this;
        ++value_;
        return tmp;
    }
    constexpr StrongInt& operator--() noexcept { --value_; return *this; }
    constexpr StrongInt operator--(int) noexcept {
        StrongInt tmp = *this;
        --value_;
        return tmp;
    }

    constexpr StrongInt& operator+=(const StrongInt& other) noexcept {
        value_ += other.value_;
        return *this;
    }
    constexpr StrongInt& operator-=(const StrongInt& other) noexcept {
        value_ -= other.value_;
        return *this;
    }

    constexpr StrongInt operator+(const StrongInt& other) const noexcept {
        return StrongInt(value_ + other.value_);
    }
    constexpr StrongInt operator-(const StrongInt& other) const noexcept {
        return StrongInt(value_ - other.value_);
    }

    constexpr bool operator==(const StrongInt& other) const noexcept {
        return value_ == other.value_;
    }
    constexpr bool operator!=(const StrongInt& other) const noexcept {
        return value_ != other.value_;
    }
    constexpr bool operator<(const StrongInt& other) const noexcept {
        return value_ < other.value_;
    }
    constexpr bool operator<=(const StrongInt& other) const noexcept {
        return value_ <= other.value_;
    }
    constexpr bool operator>(const StrongInt& other) const noexcept {
        return value_ > other.value_;
    }
    constexpr bool operator>=(const StrongInt& other) const noexcept {
        return value_ >= other.value_;
    }

private:
    Rep value_;
};

template <typename Tag, typename Rep>
std::ostream& operator<<(std::ostream& os, const StrongInt<Tag, Rep>& v)
{
    os << v.get();
    return os;
}

// === 单位系统标签 ===

struct MetersTag {};
struct KilometersTag {};
struct CelsiusTag {};
struct FahrenheitTag {};
struct SecondsTag {};
struct MillisecondsTag {};

using Meters        = StrongInt<MetersTag, double>;
using Kilometers    = StrongInt<KilometersTag, double>;
using Celsius       = StrongInt<CelsiusTag, double>;
using Fahrenheit    = StrongInt<FahrenheitTag, double>;
using Seconds       = StrongInt<SecondsTag, double>;
using Milliseconds  = StrongInt<MillisecondsTag, int64_t>;

constexpr Kilometers to_kilometers(Meters m) noexcept
{
    return Kilometers(m.get() / 1000.0);
}

constexpr Meters to_meters(Kilometers km) noexcept
{
    return Meters(km.get() * 1000.0);
}

constexpr Milliseconds to_milliseconds(Seconds s) noexcept
{
    return Milliseconds(static_cast<int64_t>(s.get() * 1000.0));
}

// === ID 类型标签 ===

struct UserIdTag {};
struct OrderIdTag {};
struct ProductIdTag {};

using UserId    = StrongInt<UserIdTag, uint64_t>;
using OrderId   = StrongInt<OrderIdTag, uint64_t>;
using ProductId = StrongInt<ProductIdTag, uint64_t>;

class OrderService {
public:
    OrderId create_order(UserId user, ProductId product, int quantity)
    {
        std::cout << "Created order for user " << user.get()
                  << ", product " << product.get()
                  << ", qty=" << quantity << "\n";
        return OrderId(next_id_++);
    }

    void cancel_order(OrderId id)
    {
        std::cout << "Cancelled order " << id.get() << "\n";
    }

private:
    uint64_t next_id_ = 1;
};

// === 嵌入式寄存器地址标签 ===

struct GpioRegTag {};
struct UartRegTag {};

using GpioRegAddr = StrongInt<GpioRegTag, uint32_t>;
using UartRegAddr = StrongInt<UartRegTag, uint32_t>;

void gpio_write(GpioRegAddr addr, uint32_t value)
{
    std::printf("GPIO write: addr=0x%08X, value=0x%08X\n",
                static_cast<unsigned>(addr.get()), value);
}

void uart_write(UartRegAddr addr, uint32_t value)
{
    std::printf("UART write: addr=0x%08X, value=0x%08X\n",
                static_cast<unsigned>(addr.get()), value);
}

int main()
{
    // 单位系统
    Meters distance(5000.0);
    Kilometers km = to_kilometers(distance);
    std::cout << distance.get() << " m = " << km.get() << " km\n";

    Seconds duration(2.5);
    Milliseconds ms = to_milliseconds(duration);
    std::cout << duration.get() << " s = " << ms.get() << " ms\n";

    // ID 类型安全
    std::cout << "\n--- Order Service ---\n";
    OrderService service;
    UserId user(42);
    ProductId product(100);

    OrderId order = service.create_order(user, product, 3);
    service.cancel_order(order);

    // 寄存器地址类型安全
    std::cout << "\n--- Register Access ---\n";
    gpio_write(GpioRegAddr(0x40010800), 0x01);
    uart_write(UartRegAddr(0x40011000), 0x55);

    // 验证零开销
    static_assert(sizeof(Meters) == sizeof(double), "no overhead");
    static_assert(sizeof(UserId) == sizeof(uint64_t), "no overhead");

    return 0;
}
