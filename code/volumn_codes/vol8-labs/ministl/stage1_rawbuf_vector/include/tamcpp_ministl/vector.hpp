#pragma once
#include "tamcpp_ministl/helper/check.hpp"
#include "tamcpp_ministl/memory_helper.hpp"
#include "tamcpp_ministl/raw_buffer.hpp"
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <utility>

namespace tamcpp::ministl {

template <typename Sources> class Vector {
  public:
    Vector() = default;
    explicit Vector(std::size_t capacity) : buffer_(capacity) {}
    Vector(std::initializer_list<Sources> init_lists_src) {
        // 触发 Move 进来
        const auto sz = init_lists_src.size();
        reserve(sz);

        for (const Sources& v : init_lists_src) {
            emplace_back(v); // 往里进！往里进！
        }
    }

    ~Vector() { helper::DestroySources(buffer_.data(), buffer_.data() + current_cnt_); }

    Vector(const Vector& other) {
        // 是 capacity 还是走默认大小看情况，标准库是已经使用的大小，我们对齐
        reserve(other.current_cnt_);
        for (size_t i = 0; i < other.current_cnt_; ++i) {
            std::construct_at(buffer_.data() + i, other.buffer_.data()[i]);
        }
        current_cnt_ = other.current_cnt_;
    }
    Vector& operator=(const Vector& other) {
        // 来，玩一个小trick: copy and swap
        Vector other_{other};
        swap(other_);
        return *this;
    }

    // noexcept 是保证咱们搬动内存的时候不出事情：move_if_noexcept！
    Vector(Vector&& other) noexcept
        : buffer_(std::move(other.buffer_)), current_cnt_(other.current_cnt_) {
        other.current_cnt_ = 0;
    }

    Vector& operator=(Vector&& other) noexcept {
        // 照抄 trick:move and swap —— 先把对方偷到手,再换身份;
        Vector looter(std::move(other));
        swap(looter);
        return *this;
    }

    Sources* begin() { return buffer_.data(); }
    Sources* end() { return buffer_.data() + current_cnt_; }
    const Sources* begin() const { return buffer_.data(); }
    const Sources* end() const { return buffer_.data() + current_cnt_; }

    [[nodiscard]] std::size_t size() const { return current_cnt_; }
    [[nodiscard]] std::size_t capacity() const { return buffer_.capacity(); }
    [[nodiscard]] bool empty() const { return current_cnt_ == 0; }

    Sources& operator[](std::size_t i) { return buffer_[i]; }
    const Sources& operator[](std::size_t i) const { return buffer_[i]; }

    void push_back(const Sources& v) { emplace_back(v); }
    void push_back(Sources&& v) { emplace_back(std::move(v)); }

    void pop_back() {
        --current_cnt_;
        // 好了，析构掉，内存先不着急还给libc给的池子
        buffer_[current_cnt_].~Sources();
    }

    void clear() {
        helper::DestroySources(buffer_.data(), buffer_.data() + current_cnt_);
        current_cnt_ = 0;
    }

    void reserve(size_t new_cap) {
        if (new_cap <= buffer_.capacity())
            return;
        grow_to(new_cap);
    }

    template <typename... Args> Sources& emplace_back(Args&&... args) {
        if (current_cnt_ == buffer_.capacity()) {
            /* 为什么给4，实际上是随意给的，我们这里没有做严肃的profile */
            grow_to(std::max<size_t>(4, buffer_.capacity() * 2));
        }

        // construct_at 是 C++20 以来，我们自己进入标准的对placement new的调用
        // 那说啥了，咱们就用起来！
        Sources* p = std::construct_at(buffer_.data() + current_cnt_, std::forward<Args>(args)...);
        ++current_cnt_;
        return *p;
    }

    void swap(Vector& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(current_cnt_, other.current_cnt_);
    }

    Sources& visit_at(std::size_t index) {
        debug::Check(index < current_cnt_, "visiting overflowed index is never good idea");
        return buffer_.visit_at(index);
    }

    const Sources& visit_at(std::size_t index) const {
        debug::Check(index < current_cnt_, "visiting overflowed index is never good idea");
        return buffer_.visit_at(index);
    }

    void resize(std::size_t new_size)
        requires(std::default_initializable<Sources>)
    {
        if (new_size >= current_cnt_) {
            // 搬家！
            reserve(new_size);
            for (auto index = current_cnt_; index < new_size; index++) {
                std::construct_at(buffer_.data() + index);
            }
            current_cnt_ = new_size;
        } else {
            // 好了兄弟们干掉他们！
            for (auto index = new_size; index < current_cnt_; index++) {
                buffer_[index].~Sources();
            }
            current_cnt_ = new_size;
        }
    }

  private:
    void grow_to(std::size_t new_cap) {
        /* 我们的这个实现粗糙的非常非常离谱，但是能用！ */
        RawBuffer<Sources> new_buf_(new_cap);
        helper::Relocate(buffer_.data(), buffer_.data() + current_cnt_, new_buf_.data());
        buffer_ = std::move(new_buf_);
    }

  private:
    RawBuffer<Sources> buffer_;
    std::size_t current_cnt_{0};
};
} // namespace tamcpp::ministl
