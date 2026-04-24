// simple_vector_exercise.cpp -- 练习：支持移动的动态数组
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
        : data_(new int[cap])
        , size_(0)
        , capacity_(cap)
    {
    }

    // TODO: 实现析构函数
    // TODO: 实现拷贝构造函数（深拷贝）
    // TODO: 实现移动构造函数（指针转移 + 源对象置空）
    // TODO: 实现拷贝赋值运算符
    // TODO: 实现移动赋值运算符

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

    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }
};

int main()
{
    // 测试代码
    SimpleVector a;
    for (int i = 0; i < 10; ++i) {
        a.push_back(i * i);
    }

    std::cout << "a: ";
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::cout << a[i] << " ";
    }
    std::cout << "\n";

    // 测试拷贝构造
    SimpleVector b = a;
    std::cout << "b (拷贝): ";
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::cout << b[i] << " ";
    }
    std::cout << "\n";

    // 测试移动构造
    SimpleVector c = std::move(a);
    std::cout << "c (移动): ";
    for (std::size_t i = 0; i < c.size(); ++i) {
        std::cout << c[i] << " ";
    }
    std::cout << "\n";
    std::cout << "a 移动后: size=" << a.size()
              << ", capacity=" << a.capacity() << "\n";

    return 0;
}
