// 08_function_basics.cpp
// 函数基础综合演练：声明、定义、递归阶乘、奇偶判断

#include <iostream>
#include <string>

int add(int a, int b);
int max_of(int a, int b);
int factorial(int n);
bool is_even(int n);
void print_result(const std::string& label, int value);

int main() {
    int sum = add(15, 27);
    print_result("15 + 27", sum);

    int bigger = max_of(42, 17);
    print_result("max(42, 17)", bigger);

    int fact = factorial(6);
    print_result("6!", fact);

    int test_values[] = {0, 1, 2, 7, 10};
    for (int val : test_values) {
        std::cout << val << " 是" << (is_even(val) ? "偶数" : "奇数") << std::endl;
    }

    return 0;
}

int add(int a, int b) {
    return a + b;
}

int max_of(int a, int b) {
    return (a > b) ? a : b;
}

int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

bool is_even(int n) {
    return n % 2 == 0;
}

void print_result(const std::string& label, int value) {
    std::cout << label << " = " << value << std::endl;
}
