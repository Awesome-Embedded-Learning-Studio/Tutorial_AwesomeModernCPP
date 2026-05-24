#pragma once

#include "weak_flag.h"

template <typename T> class WeakPtr {
  public:
    WeakPtr() : ptr_(nullptr), flag_(nullptr) {}

    WeakPtr(T* ptr, WeakFlag* flag) : ptr_(ptr), flag_(flag) {
        if (flag_) {
            flag_->add_ref();
        }
    }

    WeakPtr(const WeakPtr& other) : ptr_(other.ptr_), flag_(other.flag_) {
        if (flag_) {
            flag_->add_ref();
        }
    }

    WeakPtr(WeakPtr&& other) noexcept : ptr_(other.ptr_), flag_(other.flag_) {
        other.ptr_ = nullptr;
        other.flag_ = nullptr;
    }

    WeakPtr& operator=(const WeakPtr& other) {
        if (this != &other) {
            if (flag_) {
                flag_->release();
            }
            ptr_ = other.ptr_;
            flag_ = other.flag_;
            if (flag_) {
                flag_->add_ref();
            }
        }
        return *this;
    }

    WeakPtr& operator=(WeakPtr&& other) noexcept {
        if (this != &other) {
            if (flag_) {
                flag_->release();
            }
            ptr_ = other.ptr_;
            flag_ = other.flag_;
            other.ptr_ = nullptr;
            other.flag_ = nullptr;
        }
        return *this;
    }

    ~WeakPtr() {
        if (flag_) {
            flag_->release();
        }
    }

    bool is_valid() const { return flag_ && flag_->is_valid(); }

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
    T* ptr_;
    WeakFlag* flag_;
};
