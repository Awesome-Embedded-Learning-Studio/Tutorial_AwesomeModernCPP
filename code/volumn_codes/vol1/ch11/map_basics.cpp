#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<std::string, int> scores;

    // 方式一：用 operator[] 赋值
    scores["Alice"] = 95;
    scores["Bob"] = 87;

    // 方式二：用 insert 插入 pair
    scores.insert({"Charlie", 72});

    // 方式三：用 emplace 原地构造（推荐）
    scores.emplace("Diana", 91);

    // 方式四：初始化列表
    std::map<std::string, int> ages = {
        {"Alice", 22}, {"Bob", 25}, {"Charlie", 20}
    };

    return 0;
}
