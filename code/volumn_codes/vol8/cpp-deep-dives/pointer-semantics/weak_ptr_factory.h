#pragma once

#include "weak_flag.h"
#include "weak_ptr.h"

template <typename T> class WeakPtrFactory {
  public:
    explicit WeakPtrFactory(T* owner) : owner_(owner) { flag_ = new WeakFlag(); }

    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;
    WeakPtrFactory(WeakPtrFactory&&) = delete;
    WeakPtrFactory& operator=(WeakPtrFactory&&) = delete;

    WeakPtr<T> get_weak_ptr() { return WeakPtr<T>(owner_, flag_); }

    void invalidate_weak_ptrs() {
        if (flag_) {
            flag_->invalidate();
        }
    }

    ~WeakPtrFactory() {
        invalidate_weak_ptrs();
        if (flag_) {
            flag_->release();
        }
        flag_ = nullptr;
    }

  private:
    T* owner_;
    WeakFlag* flag_;
};
