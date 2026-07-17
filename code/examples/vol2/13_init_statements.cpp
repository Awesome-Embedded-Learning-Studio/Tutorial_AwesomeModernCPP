// 13_init_statements.cpp
// if/switch 初始化器：把变量的生存范围卡在它真正有用的那几行

#include <iostream>
#include <map>
#include <mutex>
#include <string>

struct Command {
    int type;
    int arg;
};

Command read_command(int raw) {
    return {raw / 10, raw % 10};
}

int main() {
    std::cout << "=== if/switch 初始化器演示 ===\n\n";

    std::map<int, std::string> cache = {{1, "one"}, {2, "two"}};

    // 1. map 查找：迭代器被关在 if/else 内，出了块就不可见
    std::cout << "1. map 查找 (if init):\n";
    if (auto it = cache.find(2); it != cache.end()) {
        std::cout << "  找到: " << it->second << "\n";
    } else {
        std::cout << "  没找到\n";
    }
    std::cout << "\n";

    // 2. insert + 结构化绑定：声明、判断、使用一行搞定
    std::cout << "2. insert + 结构化绑定:\n";
    if (auto [it, ok] = cache.insert({3, "three"}); ok) {
        std::cout << "  插入成功: " << it->second << "\n";
    } else {
        std::cout << "  已存在: " << it->second << "\n";
    }
    std::cout << "\n";

    // 3. 锁守卫：锁的持有范围精确匹配条件块（CTAD 省去模板参数）
    std::cout << "3. 锁守卫:\n";
    std::mutex mtx;
    bool ready = true;
    if (std::lock_guard lock(mtx); ready) {
        std::cout << "  持锁状态下执行\n";
    }
    std::cout << "\n";

    // 4. switch 初始化器：init 里准备数据，switch 里分发
    std::cout << "4. switch 初始化器:\n";
    for (int raw : {12, 25, 31}) {
        switch (auto cmd = read_command(raw); cmd.type) {
            case 1:
                std::cout << "  cmd " << raw << " -> start(" << cmd.arg << ")\n";
                break;
            case 2:
                std::cout << "  cmd " << raw << " -> stop(" << cmd.arg << ")\n";
                break;
            default:
                std::cout << "  cmd " << raw << " -> unknown\n";
                break;
        }
    }

    return 0;
}
