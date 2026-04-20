#include <iostream>

int main()
{
    int scores[] = {90, 85, 78, 92, 88};

    // 传统 for 循环
    for (int i = 0; i < 5; ++i) {
        std::cout << scores[i] << " ";
    }
    std::cout << std::endl;

    // range-for 循环
    for (int score : scores) {
        std::cout << score << " ";
    }
    std::cout << std::endl;

    return 0;
}
