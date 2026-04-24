#include <memory>
#include <iostream>

struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

int main() {
    std::cout << "sizeof(int*):                  " << sizeof(int*) << "\n";
    std::cout << "sizeof(unique_ptr<int>):        " << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>): "
              << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";

    // 函数指针作为删除器——有额外开销
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>): "
              << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n";
}
