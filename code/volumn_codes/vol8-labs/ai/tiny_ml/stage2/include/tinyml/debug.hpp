#pragma once

#include <cstddef>
#include <cstdio>
#include <format>
#include <string>

#include "tinyml/tensor.hpp"

namespace tamcpp::tinyml {
template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
inline void debug_tensor(const Tensor<Rows, Cols, StorageType>& tensor) {
    std::string buffer;
    buffer.reserve(Rows * Cols * 16);

    for (std::size_t row = 0; row < Rows; ++row) {
        buffer += "[ ";

        for (std::size_t col = 0; col < Cols; ++col) {
            if constexpr (std::is_floating_point_v<StorageType>) {
                std::format_to(std::back_inserter(buffer), "{:.6g}", tensor(row, col));
            } else {
                std::format_to(std::back_inserter(buffer), "{}", tensor(row, col));
            }

            if (col + 1 != Cols) {
                buffer += ", ";
            }
        }

        buffer += " ]\n";
    }

    std::fwrite(buffer.data(), 1, buffer.size(), stdout);
}
} // namespace tamcpp::tinyml
