#include <iostream>

int main()
{
    int sum = 0;
    int value = 0;

    std::cout << "请输入数字（输入 0 结束）: ";
    std::cin >> value;

    while (value != 0) {
        sum += value;
        std::cout << "当前累加和: " << sum << std::endl;
        std::cout << "请继续输入（0 结束）: ";
        std::cin >> value;
    }

    std::cout << "最终结果: " << sum << std::endl;
    return 0;
}
