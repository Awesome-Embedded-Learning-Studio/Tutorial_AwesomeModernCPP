// simple_vector_solution.cpp -- 练习参考答案
// Standard: C++17

#include <iostream>
#include <algorithm>
#include <utility>

class SimpleVector
{
    int* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    SimpleVector() : data_(nullptr), size_(0), capacity_(0) {}

    explicit SimpleVector(std::size_t cap)
        : data_(cap > 0 ? new int[cap] : nullptr)
        , size_(0)
        , capacity_(cap)
    {
    }

    ~SimpleVector()
    {
        delete[] data_;
    }

    // 拷贝构造：深拷贝
    SimpleVector(const SimpleVector& other)
        : data_(other.capacity_ > 0 ? new int[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) {
            std::copy(other.data_, other.data_ + other.size_, data_);
        }
    }

    // 移动构造：指针转移
    SimpleVector(SimpleVector&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // 拷贝赋值
    SimpleVector& operator=(const SimpleVector& other)
    {
        if (this != &other) {
            delete[] data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            data_ = capacity_ > 0 ? new int[capacity_] : nullptr;
            if (data_) {
                std::copy(other.data_, other.data_ + size_, data_);
            }
        }
        return *this;
    }

    // 移动赋值
    SimpleVector& operator=(SimpleVector&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    void push_back(int value)
    {
        if (size_ >= capacity_) {
            std::size_t new_cap = capacity_ == 0 ? 4 : capacity_ * 2;
            int* new_data = new int[new_cap];
            std::copy(data_, data_ + size_, new_data);
            delete[] data_;
            data_ = new_data;
            capacity_ = new_cap;
        }
        data_[size_++] = value;
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    const int* data() const { return data_; }

    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }
};

int main()
{
    SimpleVector a;
    for (int i = 0; i < 10; ++i) {
        a.push_back(i * i);
    }

    std::cout << "a: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";
    std::cout << "  a.size()=" << a.size() << ", a.capacity()=" << a.capacity() << "\n\n";

    SimpleVector b = a;   // 拷贝构造
    std::cout << "b (拷贝构造): ";
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << "\n\n";

    SimpleVector c = std::move(a);  // 移动构造
    std::cout << "c (移动构造): ";
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::cout << c[i] << " ";
    }
    std::cout << "\n";
    std::cout << "  a 移动后: size=" << a.size()
              << ", capacity=" << a.capacity() << "\n\n";

    // 验证移动后的 a 可以安全使用
    a = SimpleVector(5);  // 移动赋值一个新对象
    a.push_back(999);
    std::cout << "a 重新赋值后: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";

    return 0;
}
