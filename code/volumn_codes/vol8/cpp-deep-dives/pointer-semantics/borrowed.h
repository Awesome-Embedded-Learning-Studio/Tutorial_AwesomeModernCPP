#pragma once

#include <cassert>
#include <type_traits>

template <typename T> class Borrowed {
  public:
    explicit Borrowed(T& ref) noexcept : ptr_(&ref) {}

    Borrowed(T&&) = delete;

    Borrowed(std::nullptr_t) = delete;

    explicit Borrowed(T* ptr) noexcept : ptr_(ptr) {
        assert(ptr != nullptr && "Borrowed<T> requires a non-null pointer");
    }

    Borrowed(const Borrowed&) = default;
    Borrowed& operator=(const Borrowed&) = default;
    Borrowed(Borrowed&&) = default;
    Borrowed& operator=(Borrowed&&) = default;

    T& get() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }

    template <typename U> operator Borrowed<const U>() const noexcept {
        return Borrowed<const U>(*ptr_);
    }

  private:
    T* ptr_;
};

template <typename T> Borrowed<T> borrow(T& ref) noexcept {
    return Borrowed<T>(ref);
}
