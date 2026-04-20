/**
 * @file ex11_unique_ptr_leak.cpp
 * @brief 练习：识别内存泄漏模式
 *
 * 展示两种常见的内存泄漏场景：
 *   1. early return 跳过了 delete
 *   2. throw 导致 delete 被跳过
 * 然后用 unique_ptr 修复。
 */

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

class Resource {
public:
    explicit Resource(const std::string& name) : name_(name) {
        std::cout << "  Resource(\"" << name_ << "\") 分配\n";
    }
    ~Resource() {
        std::cout << "  Resource(\"" << name_ << "\") 释放\n";
    }
    void use() const {
        std::cout << "  使用 Resource(\"" << name_ << "\")\n";
    }
private:
    std::string name_;
};

// ---- 泄漏场景 1: early return ----
// 裸指针版本（有泄漏风险）
void leaky_early_return(bool error) {
    std::cout << "泄漏场景 1 (裸指针):\n";
    // Resource* r = new Resource("Leak1");
    // if (error) {
    //     std::cout << "  发生错误，提前返回！\n";
    //     return;  // 泄漏！没有执行 delete
    // }
    // r->use();
    // delete r;
    std::cout << "  (已注释，避免实际泄漏)\n\n";
}

// unique_ptr 修复版
void fixed_early_return(bool error) {
    std::cout << "修复场景 1 (unique_ptr):\n";
    auto r = std::make_unique<Resource>("Fixed1");

    if (error) {
        std::cout << "  发生错误，提前返回！\n";
        return;  // 安全！unique_ptr 自动释放
    }
    r->use();
    // 离开作用域自动释放
}

// ---- 泄漏场景 2: 异常 ----
// 裸指针版本（有泄漏风险）
void leaky_exception(bool throw_error) {
    std::cout << "\n泄漏场景 2 (裸指针):\n";
    // Resource* r = new Resource("Leak2");
    // if (throw_error) {
    //     throw std::runtime_error("出错了！");  // 泄漏！
    // }
    // r->use();
    // delete r;
    std::cout << "  (已注释，避免实际泄漏)\n";
}

// unique_ptr 修复版
void fixed_exception(bool throw_error) {
    std::cout << "\n修复场景 2 (unique_ptr):\n";
    auto r = std::make_unique<Resource>("Fixed2");

    if (throw_error) {
        throw std::runtime_error("出错了！");  // 安全！自动释放
    }
    r->use();
}

int main() {
    std::cout << "===== 识别内存泄漏模式 =====\n\n";

    // 场景 1：正常流程
    leaky_early_return(false);
    fixed_early_return(false);

    std::cout << "\n";

    // 场景 1：异常流程（提前返回）
    fixed_early_return(true);

    // 场景 2：异常流程（抛出异常）
    try {
        fixed_exception(true);
    } catch (const std::exception& e) {
        std::cout << "  捕获异常: " << e.what() << "\n";
        std::cout << "  (Resource 已在栈展开时自动释放)\n";
    }

    std::cout << "\n要点:\n";
    std::cout << "  1. 裸指针 + early return = 泄漏\n";
    std::cout << "  2. 裸指针 + 异常 = 泄漏\n";
    std::cout << "  3. unique_ptr 在任何退出路径上都会自动释放资源\n";
    std::cout << "  4. 优先使用 make_unique 而非 new/delete\n";

    return 0;
}
