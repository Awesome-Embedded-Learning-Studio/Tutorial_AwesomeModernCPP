#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> arr = {10, 20, 30, 40, 50};

    std::cout << "arr[0]     = " << arr[0] << "\n";      // 无边界检查
    std::cout << "arr.at(2)  = " << arr.at(2) << "\n";   // 越界抛异常
    std::cout << "front      = " << arr.front() << "\n";
    std::cout << "back       = " << arr.back() << "\n";

    int* p = arr.data();                                   // 获取裸指针
    std::cout << "data()[3]  = " << p[3] << "\n";

    return 0;
}
