// etl_vector_demo.cpp - ETL 固定容量向量示例
// 注意：此示例需要 ETL 库支持
// https://github.com/ETLCPP/etl

#include <iostream>

// 如果你有 ETL 库，取消下面的注释并调整路径
// #include <etl/vector.h>

// 模拟 ETL vector 接口（用于演示）
template<typename T, size_t N>
class EtlVectorDemo {
    T buffer_[N];
    size_t size_ = 0;

public:
    bool push_back(const T& value) {
        if (size_ >= N) return false;
        buffer_[size_++] = value;
        return true;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return N; }

    T& operator[](size_t index) { return buffer_[index]; }
    const T& operator[](size_t index) const { return buffer_[index]; }

    T* data() { return buffer_; }
    const T* data() const { return buffer_; }

    // 迭代器支持
    T* begin() { return buffer_; }
    T* end() { return buffer_ + size_; }
    const T* begin() const { return buffer_; }
    const T* end() const { return buffer_ + size_; }

    void insert(size_t pos, const T& value) {
        if (size_ >= N || pos > size_) return;
        for (size_t i = size_; i > pos; --i) {
            buffer_[i] = buffer_[i - 1];
        }
        buffer_[pos] = value;
        ++size_;
    }
};

int main() {
    std::cout << "=== ETL Vector Demo ===\n\n";

    // 最大容量 8 的静态向量，内存事先分配好（无动态分配）
    // etl::vector<int, 8> v;  // 使用真实 ETL 时的写法
    EtlVectorDemo<int, 8> v;

    std::cout << "Vector capacity: " << v.capacity() << '\n';

    std::cout << "\n=== Adding elements ===\n";
    for (int i = 0; i < 6; ++i) {
        if (v.push_back(i * 10)) {
            std::cout << "Added: " << i * 10 << ", size: " << v.size() << '\n';
        }
    }

    std::cout << "\n=== Contents ===\n";
    for (auto it = v.begin(); it != v.end(); ++it) {
        std::cout << *it << ' ';
    }
    std::cout << '\n';

    std::cout << "\n=== Insert at position 2 ===\n";
    v.insert(2, 99);
    for (const auto& val : v) {
        std::cout << val << ' ';
    }
    std::cout << '\n';

    std::cout << "\n=== Filling to capacity ===\n";
    for (int i = 0; i < 10; ++i) {
        if (!v.push_back(i * 100)) {
            std::cout << "Failed to add " << i * 100 << " (buffer full)\n";
            break;
        }
    }

    std::cout << "\nFinal size: " << v.size() << '/' << v.capacity() << '\n';

    std::cout << "\n=== Key benefits ===\n";
    std::cout << "- No heap allocation\n";
    std::cout << "- Deterministic memory usage\n";
    std::cout << "- Compile-time capacity specification\n";
    std::cout << "- STL-compatible interface\n";

    return 0;
}
