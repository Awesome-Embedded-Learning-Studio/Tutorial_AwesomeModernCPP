// 30_stl_overview.cpp
// STL 三样角色配合演示：容器存数据、迭代器做接口、算法干活

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    // 容器：存数据
    std::vector<int> temps = {28, 31, 26, 33, 29, 30, 7}; // 手滑记错了一天

    // 算法：排序（通过迭代器拿到数据）
    std::sort(temps.begin(), temps.end());

    std::cout << "sorted: ";
    for (int t : temps) {
        std::cout << t << ' ';
    }
    std::cout << "\n";

    // 算法：按条件计数
    int hot = std::count_if(temps.begin(), temps.end(), [](int t) { return t >= 30; });
    std::cout << "days >= 30: " << hot << "\n";

    // 同一份 sort，对 vector<string> 照样成立
    std::vector<std::string> cities = {"Beijing", "Chengdu", "Anshan", "Dali"};
    std::sort(cities.begin(), cities.end());

    std::cout << "cities: ";
    for (const auto& c : cities) {
        std::cout << c << ' ';
    }
    std::cout << "\n";

    // at() 越界：抛 std::out_of_range，异常知识在这里兑现
    try {
        std::cout << temps.at(100) << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    return 0;
}
