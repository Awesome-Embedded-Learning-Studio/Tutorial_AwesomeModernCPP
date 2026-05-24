#pragma once

#include <cstddef>

template <typename T> class ObserverPtr {
  public:
    ObserverPtr() noexcept : ptr_(nullptr) {}
    ObserverPtr(std::nullptr_t) noexcept : ptr_(nullptr) {}
    explicit ObserverPtr(T* ptr) noexcept : ptr_(ptr) {}

    ObserverPtr(const ObserverPtr&) = default;
    ObserverPtr& operator=(const ObserverPtr&) = default;
    ObserverPtr(ObserverPtr&&) = default;
    ObserverPtr& operator=(ObserverPtr&&) = default;

    void reset(T* ptr = nullptr) noexcept { ptr_ = ptr; }

    T* release() noexcept {
        T* old = ptr_;
        ptr_ = nullptr;
        return old;
    }

    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void swap(ObserverPtr& other) noexcept {
        T* tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }

  private:
    T* ptr_;
};

template <typename T, typename U>
bool operator==(const ObserverPtr<T>& a, const ObserverPtr<U>& b) noexcept {
    return a.get() == b.get();
}

template <typename T> bool operator==(const ObserverPtr<T>& a, std::nullptr_t) noexcept {
    return !a;
}

template <typename T> ObserverPtr<T> make_observer(T* ptr) noexcept {
    return ObserverPtr<T>(ptr);
}
