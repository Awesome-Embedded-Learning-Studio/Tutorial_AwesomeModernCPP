/**
 * @file ex10_friend_iterator.cpp
 * @brief 练习：友元类迭代器
 *
 * IntBuffer 持有固定 int 数组，IntBufferIterator 作为友元类
 * 遍历 IntBuffer 的内部数组。
 */

#include <iostream>

class IntBuffer {
private:
    static constexpr int kCapacity = 8;
    int data_[kCapacity];
    int size_;

public:
    IntBuffer() : data_{}, size_(0) {}

    // 添加元素
    bool push_back(int value) {
        if (size_ >= kCapacity) {
            return false;
        }
        data_[size_] = value;
        ++size_;
        return true;
    }

    int size() const { return size_; }
    int capacity() const { return kCapacity; }

    // 声明友元类：允许 IntBufferIterator 访问私有成员
    friend class IntBufferIterator;
};

class IntBufferIterator {
private:
    const IntBuffer& buffer_;
    int index_;

public:
    IntBufferIterator(const IntBuffer& buffer, int index)
        : buffer_(buffer), index_(index) {}

    // 解引用：通过友元访问 buffer_.data_
    int operator*() const {
        return buffer_.data_[index_];
    }

    // 前置递增
    IntBufferIterator& operator++() {
        ++index_;
        return *this;
    }

    // 不相等比较
    bool operator!=(const IntBufferIterator& other) const {
        return index_ != other.index_;
    }
};

// 提供 begin/end 函数
IntBufferIterator begin(const IntBuffer& buf) {
    return IntBufferIterator(buf, 0);
}

IntBufferIterator end(const IntBuffer& buf) {
    return IntBufferIterator(buf, buf.size());
}

int main() {
    std::cout << "===== 友元类迭代器 =====\n\n";

    // 创建并填充 IntBuffer
    IntBuffer buf;
    buf.push_back(10);
    buf.push_back(20);
    buf.push_back(30);
    buf.push_back(40);
    buf.push_back(50);

    std::cout << "IntBuffer 内容 (size=" << buf.size()
              << ", capacity=" << buf.capacity() << "):\n";

    // 使用自定义迭代器遍历
    std::cout << "  手动迭代: ";
    for (auto it = begin(buf); it != end(buf); ++it) {
        std::cout << *it << " ";
    }
    std::cout << "\n\n";

    // 基于 range-for 遍历（依赖 begin/end 函数）
    std::cout << "  range-for: ";
    for (int val : buf) {
        std::cout << val << " ";
    }
    std::cout << "\n\n";

    // 空缓冲区
    IntBuffer empty;
    std::cout << "空缓冲区:\n";
    std::cout << "  内容: ";
    for (int val : empty) {
        std::cout << val << " ";
    }
    std::cout << "(空)\n\n";

    std::cout << "要点:\n";
    std::cout << "  friend class 让 IntBufferIterator 访问 IntBuffer 私有成员\n";
    std::cout << "  迭代器需要 operator*, operator++, operator!=\n";
    std::cout << "  提供 begin/end 全局函数可支持 range-for 循环\n";

    return 0;
}
