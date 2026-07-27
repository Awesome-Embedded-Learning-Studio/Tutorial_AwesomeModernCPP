#pragma once
#include <array>
#include <cstddef>
#include <expected>
#include <span>

namespace tamcpp::tinyml {

template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
class Tensor {
  public:
    enum class Error {
        kShapeMismatch,
        kOutOfRange,
    };

    constexpr std::expected<StorageType, Error> at(std::size_t i, std::size_t j) noexcept {
        if (i >= Rows || j >= Cols) {
            return std::unexpected{Error::kOutOfRange};
        }

        return internals_[i * Cols + j];
    }

    constexpr std::expected<const StorageType, Error> at(std::size_t i,
                                                         std::size_t j) const noexcept {
        if (i >= Rows || j >= Cols) {
            return std::unexpected{Error::kOutOfRange};
        }
        return internals_[i * Cols + j];
    }

    static_assert(Rows > 0 && Cols > 0, "We haven't seen a tensor with 0 size");

    // Q: why not noexcept?
    constexpr Tensor() = default; // Yeah, a default tensor
    constexpr Tensor(std::array<StorageType, Rows * Cols> internals)
        : internals_(std::move(internals)) {}

    /* These APIs are burden, yet we need this when we need to query the tensor metas */
    constexpr std::size_t row() const noexcept { return Rows; }
    constexpr std::size_t col() const noexcept { return Cols; }

    // noted by Charliechen114514, Actually array::size_type :_, but I got myself lazy
    constexpr std::size_t size() const noexcept { return internals_.size(); }

    // Hey! Get me!
    constexpr StorageType& operator()(std::size_t i, std::size_t j) noexcept {
        return internals_[i * Cols + j];
    }

    // Mark:
    // functions overload differs in type params and const decorators
    constexpr const StorageType& operator()(std::size_t i, std::size_t j) const noexcept {
        return internals_[i * Cols + j];
    }

    // span view
    constexpr std::span<const StorageType, Rows * Cols> view() const noexcept { return internals_; }
    constexpr std::span<StorageType, Rows * Cols> view() noexcept { return internals_; }

    constexpr std::array<StorageType, Rows * Cols>& storage() noexcept { return internals_; }
    constexpr std::array<const StorageType, Rows * Cols>& storage() const noexcept {
        return internals_;
    }

  private:
    /**
     * @brief Reason why we select one dimension is easy, cache friendly
     *
     */
    std::array<StorageType, Rows * Cols> internals_{}; // internal_arrays, one dimension
};

template <std::size_t Cols, typename StorageType = float>
using Vector = Tensor<1, Cols, StorageType>;

} // namespace tamcpp::tinyml
