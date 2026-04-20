#include <iostream>
#include <string>

const std::string& get_prefix()
{
    std::string prefix = "user_";
    return prefix;
}

int main()
{
    std::string name = get_prefix() + "admin";
    std::cout << name << std::endl;
    return 0;
}
