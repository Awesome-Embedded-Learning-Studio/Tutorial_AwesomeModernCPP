#pragma once

#include <atomic>
#include <memory>

struct AtomicFlag {
    std::atomic<bool> alive{true};

    void invalidate() { alive.store(false, std::memory_order_release); }
    bool is_alive() const { return alive.load(std::memory_order_acquire); }
};

template <typename T> class SimpleWeakPtr {
  public:
    SimpleWeakPtr() = default;

    SimpleWeakPtr(T* ptr, std::shared_ptr<AtomicFlag> flag) : ptr_(ptr), flag_(std::move(flag)) {}

    bool is_valid() const { return flag_ && flag_->is_alive(); }

    T* get() const {
        if (is_valid()) {
            return ptr_;
        }
        return nullptr;
    }

    T& operator*() const { return *get(); }
    T* operator->() const { return get(); }
    explicit operator bool() const { return get() != nullptr; }

  private:
    T* ptr_ = nullptr;
    std::shared_ptr<AtomicFlag> flag_;
};

template <typename T> class SimpleWeakPtrFactory {
  public:
    explicit SimpleWeakPtrFactory(T* owner)
        : owner_(owner), flag_(std::make_shared<AtomicFlag>()) {}

    SimpleWeakPtrFactory(const SimpleWeakPtrFactory&) = delete;
    SimpleWeakPtrFactory& operator=(const SimpleWeakPtrFactory&) = delete;

    SimpleWeakPtr<T> get_weak_ptr() { return SimpleWeakPtr<T>(owner_, flag_); }

    void invalidate() {
        if (flag_) {
            flag_->invalidate();
        }
    }

    ~SimpleWeakPtrFactory() { invalidate(); }

  private:
    T* owner_;
    std::shared_ptr<AtomicFlag> flag_;
};
