#include <iostream>

int main()
{
    int arr[5] = {10, 20, 30, 40, 50};
    int* p = arr;  // 合法！数组名可以直接赋给指针

    std::cout << "arr 的地址:  " << arr << "\n";
    std::cout << "p 的值:      " << p << "\n";
    std::cout << "arr[0] 的地址: " << &arr[0] << "\n";
    std::cout << "*p:          " << *p << "\n";

    return 0;
}
