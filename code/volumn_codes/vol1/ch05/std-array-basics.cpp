#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> arr = {1, 2, 3, 4, 5};

    std::cout << "大小:     " << arr.size() << "\n";
    std::cout << "为空?     " << (arr.empty() ? "是" : "否") << "\n";
    std::cout << "最大大小: " << arr.max_size() << "\n";

    return 0;
}
