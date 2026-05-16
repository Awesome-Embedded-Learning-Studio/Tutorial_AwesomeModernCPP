#include <iostream>
#include <span>
#include <stdexcept>

template<typename T>
std::span<T> make_span_from_ptr(T* ptr, std::size_t size) {
    return std::span<T>(ptr, size);
}

template<typename T>
std::span<T> take_front(std::span<T> s, std::size_t n) {
    if (n > s.size()) throw std::out_of_range("take_front: n exceeds span size");
    return s.subspan(0, n);
}

template<typename T>
std::span<T> take_range(std::span<T> s, std::size_t offset, std::size_t count) {
    if (offset > s.size() || count > s.size() - offset)
        throw std::out_of_range("take_range: range out of bounds");
    return s.subspan(offset, count);
}

int main() {
    int data[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    auto full = make_span_from_ptr(data, 10);
    auto front3 = take_front(full, 3);
    std::cout << "front 3: ";
    for (auto v : front3) std::cout << v << " ";
    std::cout << "\n";
    auto mid = take_range(full, 2, 3);
    std::cout << "mid 3: ";
    for (auto v : mid) std::cout << v << " ";
    std::cout << "\n";
    try { auto bad = take_front(full, 20); }
    catch (const std::out_of_range& e) { std::cout << "caught: " << e.what() << "\n"; }
    return 0;
}
