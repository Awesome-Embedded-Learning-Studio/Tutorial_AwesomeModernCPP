#include <memory>
#include <iostream>

struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

struct StatefulDeleter {
    int extra_data = 0;
    void operator()(int* p) noexcept { delete p; }
};

int main() {
    std::cout << "sizeof(int*):                              "
              << sizeof(int*) << "\n";
    std::cout << "sizeof(unique_ptr<int>):                    "
              << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>):      "
              << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";
    std::cout << "sizeof(unique_ptr<int, StatefulDeleter>):   "
              << sizeof(std::unique_ptr<int, StatefulDeleter>) << "\n";
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>):     "
              << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n";
}
