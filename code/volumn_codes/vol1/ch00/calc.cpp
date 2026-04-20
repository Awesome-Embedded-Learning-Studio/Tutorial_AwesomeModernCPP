#include <iostream>

int main() {
    int a = 0;
    int b = 0;

    std::cout << "请输入第一个数字: ";
    std::cin >> a;

    std::cout << "请输入第二个数字: ";
    std::cin >> b;

    int sum = a + b;
    std::cout << a << " + " << b << " = " << sum << std::endl;

    return 0;
}
