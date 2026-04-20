#include <iostream>

int main()
{
    int celsius = 25;
    // 公式：F = C * 9 / 5 + 32
    int fahrenheit = celsius * 9 / 5 + 32;
    std::cout << celsius << " C = " << fahrenheit << " F" << std::endl;
    return 0;
}
