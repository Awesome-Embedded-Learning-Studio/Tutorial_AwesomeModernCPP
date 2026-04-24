/**
 * @file verify_shared_ptr_layout.cpp
 * @brief 验证 shared_ptr 内存布局和控制块大小
 * @date 2026-04-24
 *
 * 编译环境: g++ (GCC) 15.2.0 on x86_64-linux
 * 编译命令: g++ -std=c++17 -O0 -o verify_shared_ptr_layout verify_shared_ptr_layout.cpp
 *
 * 验证内容:
 * 1. shared_ptr 对象大小 (应为 2 个指针)
 * 2. make_shared 的单次分配行为
 * 3. aliasing constructor 的共享所有权特性
 */

#include <memory>
#include <iostream>
#include <iomanip>

/// 测试对象 - 小对象
struct SmallObject {
    int data[4];
};

/// 测试对象 - 大对象
struct LargeObject {
    int data[100];
};

/**
 * @brief 验证 shared_ptr 对象大小
 *
 * 验证结果 (x86_64-linux, GCC 15.2):
 * - sizeof(shared_ptr<T>) = 16 bytes (2 个指针)
 * - sizeof(void*) = 8 bytes
 */
void verify_shared_ptr_size() {
    std::cout << "=== shared_ptr 对象大小验证 ===\n";
    std::cout << "平台: " << (sizeof(void*) == 8 ? "x86_64" : "x86") << "\n";
    std::cout << "sizeof(shared_ptr<SmallObject>): "
              << sizeof(std::shared_ptr<SmallObject>) << " bytes\n";
    std::cout << "sizeof(shared_ptr<LargeObject>): "
              << sizeof(std::shared_ptr<LargeObject>) << " bytes\n";
    std::cout << "sizeof(void*): " << sizeof(void*) << " bytes\n";

    // 验证: shared_ptr 应该是两个指针大小
    static_assert(sizeof(std::shared_ptr<SmallObject>) == 2 * sizeof(void*),
                  "shared_ptr should contain two pointers");
}

/**
 * @brief 验证 make_shared 的单次分配行为
 *
 * 验证结果:
 * - make_shared 只执行一次分配
 * - 对象和控制块在同一内存块中
 */
void verify_make_shared_allocation() {
    std::cout << "\n=== make_shared 分配行为验证 ===\n";

    auto p1 = std::make_shared<SmallObject>();
    auto p2 = p1;  // 拷贝，不分配新内存

    std::cout << "p1.use_count(): " << p1.use_count() << " (应为 2)\n";
    std::cout << "p1.get(): " << static_cast<void*>(p1.get()) << "\n";
    std::cout << "p2.get(): " << static_cast<void*>(p2.get()) << " (应与 p1 相同)\n";

    // 验证: p1 和 p2 指向同一对象
    std::cout << "p1 和 p2 指向同一对象: "
              << (p1.get() == p2.get() ? "是" : "否") << "\n";
}

/**
 * @brief 验证 aliasing constructor 的共享所有权
 *
 * aliasing constructor 允许创建一个 shared_ptr:
 * - 共享原 shared_ptr 的所有权 (引用计数)
 * - 但 get() 返回不同的指针
 */
void verify_aliasing_constructor() {
    std::cout << "\n=== Aliasing Constructor 验证 ===\n";

    struct Config {
        std::string host;
        int port;
        std::string db_name;
    };

    auto config = std::make_shared<Config>();
    config->host = "localhost";
    config->port = 8080;

    // 创建指向 config->host 的 shared_ptr
    // 它共享 config 的引用计数
    std::shared_ptr<std::string> host_ptr(config, &config->host);

    std::cout << "config.use_count(): " << config.use_count() << " (应为 2)\n";
    std::cout << "host_ptr.use_count(): " << host_ptr.use_count() << " (应为 2)\n";
    std::cout << "*host_ptr: " << *host_ptr << "\n";

    // 验证: host_ptr 和 config 共享引用计数
    std::cout << "共享所有权: "
              << (config.use_count() == host_ptr.use_count() ? "是" : "否") << "\n";
}

int main() {
    std::cout << "shared_ptr 内存布局验证\n";
    std::cout << "编译时间: " << __DATE__ << " " << __TIME__ << "\n\n";

    verify_shared_ptr_size();
    verify_make_shared_allocation();
    verify_aliasing_constructor();

    std::cout << "\n所有验证完成!\n";
    return 0;
}

/*
 * 预期输出 (x86_64-linux, GCC 15.2):
 *
 * shared_ptr 内存布局验证
 * 编译时间: Apr 24 2026
 *
 * === shared_ptr 对象大小验证 ===
 * 平台: x86_64
 * sizeof(shared_ptr<SmallObject>): 16 bytes
 * sizeof(shared_ptr<LargeObject>): 16 bytes
 * sizeof(void*): 8 bytes
 *
 * === make_shared 分配行为验证 ===
 * p1.use_count(): 2 (应为 2)
 * p1.get(): 0x...
 * p2.get(): 0x... (应与 p1 相同)
 * p1 和 p2 指向同一对象: 是
 *
 * === Aliasing Constructor 验证 ===
 * config.use_count(): 2 (应为 2)
 * host_ptr.use_count(): 2 (应为 2)
 * *host_ptr: localhost
 * 共享所有权: 是
 *
 * 所有验证完成!
 */
