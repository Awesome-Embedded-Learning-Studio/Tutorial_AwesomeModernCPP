#include <iostream>

/// @brief 交换两个整数的值
/// @param a 第一个整数
/// @param b 第二个整数
void swap_values(int& a, int& b)
{
    int temp = a;
    a = b;
    b = temp;
}

int main()
{
    int x = 3;
    int y = 7;
    swap_values(x, y);
    std::cout << "x = " << x << ", y = " << y << std::endl;
    return 0;
}
