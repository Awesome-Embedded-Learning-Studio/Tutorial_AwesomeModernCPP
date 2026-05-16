#include <iostream>
#include <stdexcept>
#include <cstddef>

template<typename T>
class checked_span {
    T* ptr_;
    std::size_t size_;
public:
    checked_span(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}
    T& operator[](std::ptrdiff_t index) {
        if (index < 0) throw std::out_of_range("negative index");
        if (static_cast<std::size_t>(index) >= size_) throw std::out_of_range("index out of range");
        return ptr_[index];
    }
    const T& operator[](std::ptrdiff_t index) const {
        if (index < 0) throw std::out_of_range("negative index");
        if (static_cast<std::size_t>(index) >= size_) throw std::out_of_range("index out of range");
        return ptr_[index];
    }
    std::size_t size() const { return size_; }
};

template<typename T>
checked_span(T*, std::size_t) -> checked_span<T>;

int main() {
    int data[] = {1, 2, 3, 4, 5};
    checked_span s(data, 5);
    std::cout << s[2] << "\n";
    try { std::cout << s[10] << "\n"; }
    catch (const std::out_of_range& e) { std::cout << "caught: " << e.what() << "\n"; }
    try { auto val = s[-10]; (void)val; }
    catch (const std::out_of_range& e) { std::cout << "caught: " << e.what() << "\n"; }
    return 0;
}
