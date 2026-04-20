#include <iomanip>
#include <iostream>

int main()
{
    float a = 0.1f;
    float b = 0.2f;
    float c = a + b;

    // 用高精度输出，看清楚浮点数的真面目
    std::cout << std::setprecision(20);
    std::cout << "0.1f  = " << a << std::endl;
    std::cout << "0.2f  = " << b << std::endl;
    std::cout << "a + b = " << c << std::endl;
    std::cout << "0.3f  = " << 0.3f << std::endl;
    std::cout << std::endl;

    // 比较结果
    if (c == 0.3f) {
        std::cout << "a + b == 0.3f (相等)" << std::endl;
    }
    else {
        std::cout << "a + b != 0.3f (不相等!)" << std::endl;
        std::cout << "差值: " << (c - 0.3f) << std::endl;
    }

    return 0;
}
