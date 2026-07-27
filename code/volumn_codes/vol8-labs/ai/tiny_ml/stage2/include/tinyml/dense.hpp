#pragma once

#include <cstddef>
#include <span>

#include "tinyml/tensor.hpp"

namespace tamcpp::tinyml {

template <std::size_t In, std::size_t Out, typename StorageType = float>
struct Dense {
    static constexpr std::size_t kIn = In;
    static constexpr std::size_t kOut = Out;

    static_assert(In > 0 && Out > 0, "We haven't seen a tensor with 0 size");

    using Bias_t = Vector<Out, StorageType>;
    using BiasView_t = std::span<const StorageType, Out>;
    using Weight_t = Tensor<Out, In, StorageType>;
    using WeightView_t = std::span<const StorageType, Out * In>;

    constexpr Dense(const Weight_t& weight, const Bias_t& bias) noexcept
        : weight_(weight.view()), bias_(bias.view()) {}

    constexpr Vector<Out, StorageType>
    forward(const Vector<In, StorageType>& vec_in) const noexcept {
        Vector<Out, StorageType> result{};
        // Vector = Tensor<1, N>,只有第 0 行;行下标必须是 0,
        // 否则 internals_[1*N + j] 越过 std::array 边界(运行期 UB,
        // 编译期还会让 forward 无法 constexpr 求值)。
        for (std::size_t vec_out_index = 0; vec_out_index < Out; ++vec_out_index) {
            StorageType& acc = result(0, vec_out_index); // 取引用,省掉每次 += 的下标重算
            acc = bias_[vec_out_index];
            for (std::size_t vec_in_index = 0; vec_in_index < In; ++vec_in_index) {
                acc += vec_in(0, vec_in_index) * weight_[vec_out_index * In + vec_in_index];
            }
        }
        return result;
    }

    constexpr std::span<const StorageType, Out * In> weight() const noexcept { return weight_; }
    constexpr std::span<const StorageType, Out> bias() const noexcept { return bias_; }

  private:
    WeightView_t weight_;
    BiasView_t bias_;
};

} // namespace tamcpp::tinyml
