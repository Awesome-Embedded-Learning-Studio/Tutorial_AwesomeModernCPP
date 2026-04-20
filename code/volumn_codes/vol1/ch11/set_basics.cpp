#include <iostream>
#include <set>

int main()
{
    std::set<int> s = {5, 3, 1, 4, 2, 3, 1};

    // 重复元素被自动忽略，且元素已排序
    // s: {1, 2, 3, 4, 5}

    s.insert(6);        // 插入
    s.emplace(0);       // 原地构造插入
    s.erase(3);         // 按 key 删除

    // 查找
    if (s.contains(4)) {            // C++20
        std::cout << "4 is in the set\n";
    }

    if (s.count(2)) {               // 所有 C++ 版本通用
        std::cout << "2 is in the set\n";
    }

    auto it = s.find(1);
    if (it != s.end()) {
        std::cout << "Found: " << *it << "\n";
    }

    return 0;
}
