/**
 * @file ex07_id_generator.cpp
 * @brief 练习：UniqueIdGenerator 类
 *
 * 使用静态成员实现递增 ID 生成器。
 * generate() 返回递增整数，reset() 重置起始值。
 * 不需要创建实例即可使用。
 */

#include <iostream>

class UniqueIdGenerator {
public:
    // 删除构造函数，禁止实例化
    UniqueIdGenerator() = delete;

    // 生成一个唯一递增 ID
    static int generate() {
        return current_id_++;
    }

    // 重置 ID 起始值
    static void reset(int start) {
        current_id_ = start;
    }

    // 查看当前 ID（不递增）
    static int peek() {
        return current_id_;
    }

private:
    static int current_id_;
};

// 静态成员定义
int UniqueIdGenerator::current_id_ = 1;

int main() {
    std::cout << "===== UniqueIdGenerator 类 =====\n\n";

    // 默认从 1 开始生成
    std::cout << "从默认起始值生成 ID:\n";
    int id1 = UniqueIdGenerator::generate();
    int id2 = UniqueIdGenerator::generate();
    int id3 = UniqueIdGenerator::generate();
    std::cout << "  ID 1: " << id1 << "\n";
    std::cout << "  ID 2: " << id2 << "\n";
    std::cout << "  ID 3: " << id3 << "\n";
    std::cout << "  下一个: " << UniqueIdGenerator::peek() << "\n\n";

    // 重置到 100
    std::cout << "重置到 100 后:\n";
    UniqueIdGenerator::reset(100);
    std::cout << "  ID 4: " << UniqueIdGenerator::generate() << "\n";
    std::cout << "  ID 5: " << UniqueIdGenerator::generate() << "\n";
    std::cout << "  ID 6: " << UniqueIdGenerator::generate() << "\n\n";

    // 重置到 0
    std::cout << "重置到 0 后:\n";
    UniqueIdGenerator::reset(0);
    for (int i = 0; i < 5; ++i) {
        std::cout << "  生成: " << UniqueIdGenerator::generate() << "\n";
    }

    std::cout << "\n要点:\n";
    std::cout << "  静态成员 current_id_ 在所有调用间共享\n";
    std::cout << "  删除构造函数防止创建实例\n";
    std::cout << "  类名::方法名 直接调用静态方法\n";

    return 0;
}
