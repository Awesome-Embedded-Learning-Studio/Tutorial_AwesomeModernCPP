#include <iostream>
#include <memory>
#include <string>

struct Player
{
    std::string name;
    int level;

    Player(const std::string& n, int lv) : name(n), level(lv)
    {
        std::cout << name << " 登场！\n";
    }

    ~Player() { std::cout << name << " 退场。\n"; }

    void show_status() const
    {
        std::cout << name << " Lv." << level << "\n";
    }
};

int main()
{
    {
        auto hero = std::make_unique<Player>("Alice", 5);
        hero->show_status();   // -> 访问成员，和裸指针一样
        std::cout << (*hero).name << "\n";  // * 解引用也行
    }
    // hero 在这里离开作用域，自动 delete

    std::cout << "继续执行...\n";
    return 0;
}
