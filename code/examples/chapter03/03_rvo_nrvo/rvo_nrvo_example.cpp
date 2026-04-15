// RVO (Return Value Optimization) 和 NRVO (Named Return Value Optimization) 示例

#include <iostream>
#include <cstdint>
#include <string>

// ==================== 测试 RVO/NRVO 的类 ====================
class Buffer
{
public:
    Buffer() {
        std::cout << "Buffer: 默认构造" << std::endl;
    }

    Buffer(size_t size) : size_(size) {
        std::cout << "Buffer: 带参构造, size=" << size_ << std::endl;
    }

    // 拷贝构造
    Buffer(const Buffer& other) : size_(other.size_) {
        std::cout << "Buffer: 拷贝构造" << std::endl;
    }

    // 移动构造
    Buffer(Buffer&& other) noexcept : size_(other.size_) {
        std::cout << "Buffer: 移动构造" << std::endl;
        other.size_ = 0;
    }

    // 拷贝赋值
    Buffer& operator=(const Buffer& other) {
        size_ = other.size_;
        std::cout << "Buffer: 拷贝赋值" << std::endl;
        return *this;
    }

    // 移动赋值
    Buffer& operator=(Buffer&& other) noexcept {
        size_ = other.size_;
        other.size_ = 0;
        std::cout << "Buffer: 移动赋值" << std::endl;
        return *this;
    }

    ~Buffer() {
        if (size_ > 0) {
            std::cout << "Buffer: 析构, size=" << size_ << std::endl;
        } else {
            std::cout << "Buffer: 析构 (已移动)" << std::endl;
        }
    }

    size_t size() const { return size_; }

private:
    size_t size_;
};

// ==================== 返回临时对象（RVO）====================
Buffer create_buffer_temporary(size_t size) {
    return Buffer(size);  // RVO: 直接在调用点构造
}

// ==================== 返回命名对象（NRVO）====================
Buffer create_buffer_named(size_t size) {
    Buffer buf(size);
    return buf;  // NRVO: 直接在调用点构造
}

// ==================== 条件返回（可能阻止优化）====================
Buffer create_buffer_conditional(bool use_large) {
    Buffer small(64);
    Buffer large(1024);

    if (use_large) {
        return large;  // 可能阻止 NRVO
    } else {
        return small;
    }
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== RVO/NRVO 示例 ===" << std::endl;

    // 1. RVO - 返回临时对象
    std::cout << "\n--- RVO - 返回临时对象 ---" << std::endl;
    std::cout << "创建 buf1..." << std::endl;
    Buffer buf1 = create_buffer_temporary(256);
    std::cout << "buf1 size: " << buf1.size() << std::endl;

    // 2. NRVO - 返回命名对象
    std::cout << "\n--- NRVO - 返回命名对象 ---" << std::endl;
    std::cout << "创建 buf2..." << std::endl;
    Buffer buf2 = create_buffer_named(512);
    std::cout << "buf2 size: " << buf2.size() << std::endl;

    // 3. 条件返回（可能阻止优化）
    std::cout << "\n--- 条件返回 ---" << std::endl;
    std::cout << "创建 buf3..." << std::endl;
    Buffer buf3 = create_buffer_conditional(true);
    std::cout << "buf3 size: " << buf3.size() << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. RVO: 返回临时对象时，直接在调用点构造" << std::endl;
    std::cout << "2. NRVO: 返回命名对象时，直接在调用点构造" << std::endl;
    std::cout << "3. C++17 起，保证了复制省略（guaranteed elision）" << std::endl;
    std::cout << "4. 多个返回点可能阻止优化，但编译器仍然很聪明" << std::endl;
    std::cout << "5. 不要写 return std::move(local_var)，会阻止 RVO！" << std::endl;

    return 0;
}
