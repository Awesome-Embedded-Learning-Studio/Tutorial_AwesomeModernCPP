/**
 * @file raw_buffer.hpp
 * @author charliechen114514@tamcpp
 * @brief Raw Buffer for every stuff
 * @version 0.1
 * @date 2026-08-30
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "tamcpp_ministl/helper/check.hpp"
#include "tamcpp_ministl/helper/helpful_macro.hpp"
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

namespace tamcpp::ministl {
/**
 * @brief   这里是裸内存，我们只是套上一些好用的，语义化的接口
 *          如果您是从C时代来的朋友，可能会直接套用void*，但是这里是C++，别这样做，好用的语法用！起！来！
 *          Sources 标记着我们这篇区域到底存啥
 */
template <typename Sources> struct RawBuffer {
    RawBuffer() = default;
    // 你说他是一个普通的封装了 new T[]，那不对，我们没构造对象，纯纯讨饭内存来的
    explicit RawBuffer(std::size_t capacity) : capacity_(capacity) {
        // 乘法先验溢出：回绕会骗过 operator new，圈的地比要的小，后面全是越界
        // （Chromium 镜用 CheckMul 防的就是这一手）
        debug::Check(capacity <= std::numeric_limits<std::size_t>::max() / sizeof(Sources),
                     "capacity * sizeof(Sources) overflows");
        // 我们构造 Capacity * Sources大小的连续空间
        // 因为 new 只是返回一段void*的指针，我们要强转一下~
        raw_buffer_begin_ = static_cast<Sources*>(::operator new(capacity * sizeof(Sources)));
    }

    /**
     * @brief 拿来吧你！
     *
     * @param other_raw_buffer
     */
    RawBuffer(RawBuffer&& other_raw_buffer) noexcept {
        // 这里是移交所属权
        raw_buffer_begin_ = other_raw_buffer.raw_buffer_begin_;
        capacity_ = other_raw_buffer.capacity_;

        // 会有人问，直接置空嘛？对的，这样的话对面这个移动的对象就在语义上失效了
        // 理论上，你不应该再使用这个other对象
        other_raw_buffer.raw_buffer_begin_ = nullptr;
        other_raw_buffer.capacity_ = 0;
    }

    RawBuffer& operator=(RawBuffer&& other_raw_buffer) noexcept {
        if (this == &other_raw_buffer) {
            // 兄弟们这很简单，我万一要是搞出来
            // MySelf = std::move(MySelf)，这里一通搞自己把自己置空了
            // 所以让他麻溜点，不要假设不会出现，你的用户只会问你为什么炸了，你的老板会diss你不会写代码
            // :(
            return *this;
        }

        //
        ::operator delete(raw_buffer_begin_);

        raw_buffer_begin_ = other_raw_buffer.raw_buffer_begin_;
        capacity_ = other_raw_buffer.capacity_;

        // 会有人问，直接置空嘛？对的，这样的话对面这个移动的对象就在语义上失效了
        // 理论上，你不应该再使用这个other对象
        other_raw_buffer.raw_buffer_begin_ = nullptr;
        other_raw_buffer.capacity_ = 0;

        return *this;
    }

    ~RawBuffer() {
        // Remove the buffer!
        ::operator delete(raw_buffer_begin_); // 它不管对象死活，杀干净是 Vector 的义务
    }

    // 一些快速接口
    [[nodiscard("Dont throw away the ptr, or why you call this?")]]
    Sources* data() const noexcept {
        return raw_buffer_begin_;
    }

    [[nodiscard("Dont throw away the result, or why you call this?")]]
    std::size_t capacity() const noexcept {
        return capacity_;
    }

    Sources& visit_at(const std::size_t index) {
        debug::Check(index < capacity_, "Overflow index visited happens in RawBuffer");
        // 使用一下数组的语法糖给我们偷懒一下
        return raw_buffer_begin_[index];
    }

    const Sources& visit_at(const std::size_t index) const {
        debug::Check(index < capacity_, "Overflow index visited happens in RawBuffer");
        // 使用一下数组的语法糖给我们偷懒一下
        return raw_buffer_begin_[index];
    }

    Sources& operator[](const size_t index) noexcept { return raw_buffer_begin_[index]; }
    const Sources& operator[](const size_t index) const noexcept {
        return raw_buffer_begin_[index];
    }

  private:
    DISABLE_COPY(RawBuffer);

  private:
    Sources* raw_buffer_begin_{nullptr}; ///< 我们内存的起始点
    std::size_t capacity_{0};
};

} // namespace tamcpp::ministl
