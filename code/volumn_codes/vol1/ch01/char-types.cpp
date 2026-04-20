#include <iostream>

int main()
{
    char c = 'A';
    signed char sc = -1;
    unsigned char uc = 255;

    std::cout << "char 'A' 的整数值: " << static_cast<int>(c) << std::endl;
    std::cout << "signed char -1 的整数值: " << static_cast<int>(sc)
              << std::endl;
    std::cout << "unsigned char 255 的整数值: " << static_cast<int>(uc)
              << std::endl;

    return 0;
}
