#include <iostream>

int main()
{
    int a = 10;
    int b = 3;
    double c = a / b;
    double d = static_cast<double>(a) / b;

    std::cout << c << std::endl;
    std::cout << d << std::endl;

    unsigned int x = 10;
    int y = -1;
    std::cout << (x > y ? "x > y" : "x <= y") << std::endl;

    return 0;
}
