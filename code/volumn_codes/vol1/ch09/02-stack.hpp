// 02-stack.hpp
#pragma once

#include <stdexcept>
#include <vector>

template <typename T>
class Stack
{
public:
    void push(const T& value) { data_.push_back(value); }

    void pop()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::pop(): stack is empty");
        }
        data_.pop_back();
    }

    T& top()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    const T& top() const
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    bool empty() const { return data_.empty(); }
    std::size_t size() const { return data_.size(); }

private:
    std::vector<T> data_;
};
