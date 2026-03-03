// RAII 资源管理示例：手动管理 vs 零开销 RAII

#include <cstdint>
#include <iostream>

// 模拟嵌入式外设函数
void enable_clock(uint32_t id) {
    std::cout << "时钟使能: 外设 " << id << std::endl;
}

void disable_clock(uint32_t id) {
    std::cout << "时钟禁用: 外设 " << id << std::endl;
}

void configure_pins() {
    std::cout << "引脚配置完成" << std::endl;
}

void do_something() {
    std::cout << "执行操作..." << std::endl;
}

constexpr uint32_t PERIPH_GPIOA = 1;

// ==================== 手动管理（容易泄漏）====================
void configure_peripheral_manual() {
    enable_clock(PERIPH_GPIOA);
    configure_pins();
    // 如果这里异常或提前 return，时钟不会被禁用！
    do_something();
    disable_clock(PERIPH_GPIOA);
}

// ==================== 零开销 RAII ====================
class ClockGuard {
    uint32_t peripheral_id;
public:
    ClockGuard(uint32_t id) : peripheral_id(id) {
        enable_clock(peripheral_id);
    }
    ~ClockGuard() {
        disable_clock(peripheral_id);  // 自动清理
    }

    // 禁止拷贝
    ClockGuard(const ClockGuard&) = delete;
    ClockGuard& operator=(const ClockGuard&) = delete;
};

void configure_peripheral_raii() {
    ClockGuard clock(PERIPH_GPIOA);
    configure_pins();
    do_something();
    // clock 自动析构，即使发生异常
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== RAII 资源管理示例 ===" << std::endl;

    std::cout << "\n--- 手动管理 ---" << std::endl;
    configure_peripheral_manual();

    std::cout << "\n--- RAII 自动管理 ---" << std::endl;
    configure_peripheral_raii();

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. 无论函数如何退出（return/异常），析构函数都会被调用" << std::endl;
    std::cout << "2. 编译器会内联构造和析构函数，生成的代码与手动管理相同" << std::endl;
    std::cout << "3. RAII 让资源管理自动化，无性能代价" << std::endl;

    return 0;
}
