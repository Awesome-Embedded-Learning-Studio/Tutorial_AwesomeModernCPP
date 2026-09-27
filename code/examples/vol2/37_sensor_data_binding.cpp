// sensor_data_binding.cpp -- 自定义类型通过 tuple-like 协议支持结构化绑定
// Standard: C++17

#include <cstdint>
#include <iostream>
#include <utility>

class SensorData {
  public:
    SensorData(uint8_t id, float value) : id_(id), value_(value) {}

    template <std::size_t I> auto& get() {
        if constexpr (I == 0)
            return id_;
        else if constexpr (I == 1)
            return value_;
    }

    template <std::size_t I> const auto& get() const {
        if constexpr (I == 0)
            return id_;
        else if constexpr (I == 1)
            return value_;
    }

  private:
    uint8_t id_;
    float value_;
};

// 特化 tuple_size：告诉编译器有 2 个元素
template <> struct std::tuple_size<SensorData> : std::integral_constant<std::size_t, 2> {};

// 特化 tuple_element：告诉编译器每个元素的类型
template <> struct std::tuple_element<0, SensorData> {
    using type = uint8_t;
};

template <> struct std::tuple_element<1, SensorData> {
    using type = float;
};

int main() {
    SensorData data{5, 23.5f};
    auto [id, value] = data; // id = 5, value = 23.5

    std::cout << "id = " << +id << ", value = " << value << "\n";

    return 0;
}
