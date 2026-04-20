#include <iostream>

int main()
{
    bool flag = true;
    int count = flag + flag + flag;

    std::cout << "true + true + true = " << count << std::endl;
    std::cout << "sizeof(bool) = " << sizeof(bool) << std::endl;

    return 0;
}
