// 验证 lock_guard CTAD 和 if 初始化器的正确用法
// 修正文档中的错误示例

#include <mutex>
#include <map>
#include <iostream>

int main() {
    std::mutex mtx;
    std::map<int, int> data_store = {{1, 100}, {2, 200}};
    int id = 1;

    // ✓ 正确：CTAD 可以用于 lock_guard
    if (std::lock_guard lock(mtx); true) {
        std::cout << "Lock held\n";
    }

    // ✓ 正确：在条件中使用 count
    if (std::lock_guard lock(mtx); data_store.count(id) > 0) {
        std::cout << "Found: " << data_store.at(id) << '\n';
    }

    // ✗ 错误：不能在 if 初始化器中结合 lock_guard 和结构化绑定
    // if (std::lock_guard lock(mtx); auto [it, ok] = data_store.emplace(id, 0); !ok) {
    //     std::cout << "Already exists: " << it->second << '\n';
    // }
    // 原因：结构化绑定声明不能作为条件，需要单独的语句

    // ✓ 正确：使用嵌套 if 语句
    if (std::lock_guard lock(mtx); true) {
        if (auto it = data_store.find(id); it != data_store.end()) {
            std::cout << "Found: " << it->second << '\n';
        }
    }

    // ✓ 正确：使用代码块限制锁的作用域
    {
        std::lock_guard lock(mtx);
        if (auto it = data_store.find(id); it != data_store.end()) {
            std::cout << "Found: " << it->second << '\n';
        }
    }

    return 0;
}
