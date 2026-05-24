#pragma once

// ⚠️ 教学用反模式实现，不要在生产代码中使用
// 演示 T* + raw Flag* 为什么不是可靠的 WeakPtr

struct Flag {
    bool alive = true;
};

template <typename T> class UnsafeWeakPtr {
  public:
    UnsafeWeakPtr(T* ptr, Flag* flag) : ptr_(ptr), flag_(flag) {}

    bool is_valid() const { return flag_ && flag_->alive; }

    T* get() const {
        if (is_valid()) {
            return ptr_;
        }
        return nullptr;
    }

    T& operator*() const { return *get(); }
    T* operator->() const { return get(); }

  private:
    T* ptr_;
    Flag* flag_;
};

template <typename T> class UnsafeWeakPtrFactory {
  public:
    explicit UnsafeWeakPtrFactory(T* owner) : owner_(owner) {}

    UnsafeWeakPtrFactory(const UnsafeWeakPtrFactory&) = delete;
    UnsafeWeakPtrFactory& operator=(const UnsafeWeakPtrFactory&) = delete;

    UnsafeWeakPtr<T> get_weak_ptr() { return UnsafeWeakPtr<T>(owner_, &flag_); }

    void invalidate() { flag_.alive = false; }

    ~UnsafeWeakPtrFactory() { flag_.alive = false; }

  private:
    T* owner_;
    Flag flag_;
};
