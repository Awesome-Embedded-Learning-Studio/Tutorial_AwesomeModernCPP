#include <iostream>

int main()
{
    int arr[5] = {10, 20, 30, 40, 50};

    // 指针遍历
    std::cout << "指针遍历: ";
    for (int* p = arr; p != arr + 5; ++p) {
        std::cout << *p << " ";
    }
    std::cout << "\n";

    // 下标遍历
    std::cout << "下标遍历: ";
    for (int i = 0; i < 5; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << "\n";

    // range-for 遍历
    std::cout << "range-for: ";
    for (int x : arr) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    return 0;
}
