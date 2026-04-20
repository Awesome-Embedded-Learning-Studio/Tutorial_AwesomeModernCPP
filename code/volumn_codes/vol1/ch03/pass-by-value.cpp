#include <iostream>

void add_ten(int x)
{
    x += 10;
    std::cout << "函数内 x = " << x << std::endl;
}

int main()
{
    int value = 5;
    add_ten(value);
    std::cout << "函数外 value = " << value << std::endl;
    return 0;
}
