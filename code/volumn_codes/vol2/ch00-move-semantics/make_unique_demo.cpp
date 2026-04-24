#include <memory>
#include <string>

struct User {
    std::string name;
    int id;

    User(std::string n, int i) : name(std::move(n)), id(i) {}
};

int main()
{
    std::string name = "Alice";
    auto user = std::make_unique<User>(std::move(name), 42);
    // std::move(name) 是右值 → name 被移动进 User 的构造函数
    // 42 是右值 → int 没有"移动"的概念，就是值传递

    auto user2 = std::make_unique<User>("Bob", 100);
    // "Bob" 是 const char* 右值 → 用于构造 std::string 参数
    return 0;
}
