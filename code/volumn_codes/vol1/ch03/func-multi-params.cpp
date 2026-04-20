#include <iostream>
#include <string>

void print_info(const std::string& name, int age, double height)
{
    std::cout << name << ", " << age << " 岁, "
              << height << " cm" << std::endl;
}

int main()
{
    // 按位置传递，顺序不能搞错
    print_info("Alice", 20, 165.5);
    return 0;
}
