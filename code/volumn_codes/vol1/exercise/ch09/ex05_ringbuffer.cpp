/**
 * @file ex05_ringbuffer.cpp
 * @brief 练习：非类型模板参数 — 实现环形缓冲区 RingBuffer<T, kCapacity>
 *
 * 使用 std::array<T, kCapacity> 作为底层存储，维护读写两个索引，
 * 通过取模运算实现环形回绕。提供 push/pop/full/empty/size 接口。
 */

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

/**
 * @brief 固定容量的环形缓冲区
 *
 * @tparam T         元素类型
 * @tparam kCapacity 缓冲区容量（编译期常量）
 */
template <typename T, std::size_t kCapacity>
class RingBuffer
{
public:
    /// @brief 默认构造
    RingBuffer() : read_index_(0), write_index_(0), count_(0) {}

    /// @brief 向缓冲区尾部写入一个元素
    /// @throws std::runtime_error 缓冲区已满时抛出异常
    void push(const T& value)
    {
        if (full()) {
            throw std::runtime_error("RingBuffer::push(): buffer is full");
        }
        buffer_[write_index_] = value;
        write_index_ = (write_index_ + 1) % kCapacity;
        ++count_;
    }

    /// @brief 从缓冲区头部读取并移除一个元素
    /// @throws std::runtime_error 缓冲区为空时抛出异常
    T pop()
    {
        if (empty()) {
            throw std::runtime_error("RingBuffer::pop(): buffer is empty");
        }
        T value = buffer_[read_index_];
        read_index_ = (read_index_ + 1) % kCapacity;
        --count_;
        return value;
    }

    /// @brief 判断缓冲区是否已满
    bool full() const { return count_ == kCapacity; }

    /// @brief 判断缓冲区是否为空
    bool empty() const { return count_ == 0; }

    /// @brief 返回当前元素数量
    std::size_t size() const { return count_; }

    /// @brief 返回缓冲区总容量
    constexpr std::size_t capacity() const { return kCapacity; }

private:
    std::array<T, kCapacity> buffer_;
    std::size_t read_index_;   // 读位置
    std::size_t write_index_;  // 写位置
    std::size_t count_;        // 当前元素数量
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== ex05: RingBuffer<T, N> 环形缓冲区 =====\n\n";

    // --- RingBuffer<int, 4> 基本测试 ---
    {
        RingBuffer<int, 4> rb;
        std::cout << "RingBuffer<int, 4> 测试:\n";
        std::cout << "  初始: empty=" << std::boolalpha << rb.empty()
                  << ", full=" << rb.full()
                  << ", size=" << rb.size() << "\n";

        // 写入 3 个元素
        rb.push(10);
        rb.push(20);
        rb.push(30);
        std::cout << "  push 10, 20, 30 后: empty=" << rb.empty()
                  << ", full=" << rb.full()
                  << ", size=" << rb.size() << "\n";

        // 读取 1 个
        int val = rb.pop();
        std::cout << "  pop => " << val
                  << ", size=" << rb.size() << "\n";

        // 再写入 2 个（测试回绕）
        rb.push(40);
        rb.push(50);
        std::cout << "  push 40, 50 后: full=" << rb.full()
                  << ", size=" << rb.size() << "\n\n";
    }

    // --- RingBuffer<std::string, 3> 回绕与异常测试 ---
    {
        RingBuffer<std::string, 3> rb;
        std::cout << "RingBuffer<std::string, 3> 回绕测试:\n";

        // 填满
        rb.push(std::string("alpha"));
        rb.push(std::string("bravo"));
        rb.push(std::string("charlie"));
        std::cout << "  push alpha, bravo, charlie => full="
                  << std::boolalpha << rb.full() << "\n";

        // 满时再 push 应抛异常
        try {
            rb.push(std::string("overflow"));
        } catch (const std::runtime_error& e) {
            std::cout << "  满时 push 捕获异常: " << e.what() << "\n";
        }

        // 弹出全部并验证顺序
        std::cout << "  依次 pop: ";
        while (!rb.empty()) {
            std::cout << "\"" << rb.pop() << "\" ";
        }
        std::cout << "\n";

        // 空时再 pop 应抛异常
        try {
            rb.pop();
        } catch (const std::runtime_error& e) {
            std::cout << "  空时 pop 捕获异常: " << e.what() << "\n\n";
        }
    }

    // --- RingBuffer<double, 5> 回绕验证 ---
    {
        RingBuffer<double, 5> rb;
        std::cout << "RingBuffer<double, 5> 回绕验证:\n";

        // 写 5 个，读 3 个，再写 3 个（触发写指针回绕）
        rb.push(1.1);
        rb.push(2.2);
        rb.push(3.3);
        rb.push(4.4);
        rb.push(5.5);

        std::cout << "  pop: " << rb.pop() << "\n";
        std::cout << "  pop: " << rb.pop() << "\n";
        std::cout << "  pop: " << rb.pop() << "\n";

        rb.push(6.6);
        rb.push(7.7);
        rb.push(8.8);

        std::cout << "  剩余元素依次 pop: ";
        while (!rb.empty()) {
            std::cout << rb.pop() << " ";
        }
        std::cout << "\n";
    }

    return 0;
}
