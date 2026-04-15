// 模板元编程示例：编译期循环展开

#include <cstdint>
#include <iostream>
#include <functional>

// ==================== 编译期循环展开 ====================
template<size_t N>
struct UnrollLoop {
    template<typename Func>
    static void execute(Func f) {
        f(N-1);
        UnrollLoop<N-1>::execute(f);
    }
};

// 模板特化：递归终止条件
template<>
struct UnrollLoop<0> {
    template<typename Func>
    static void execute(Func) {}
};

// 模拟数据处理函数
void process_data(size_t i) {
    std::cout << "处理通道 " << i << " 的数据" << std::endl;
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 模板元编程：编译期循环展开 ===" << std::endl;

    std::cout << "\n--- 编译期展开循环 ---" << std::endl;
    UnrollLoop<4>::execute([](size_t i) {
        process_data(i);  // 完全展开，无循环开销
    });

    std::cout << "\n对比：展开后的代码相当于：" << std::endl;
    std::cout << "process_data(0);" << std::endl;
    std::cout << "process_data(1);" << std::endl;
    std::cout << "process_data(2);" << std::endl;
    std::cout << "process_data(3);" << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. 循环在编译期完全展开，无循环计数器和条件判断" << std::endl;
    std::cout << "2. 避免分支预测失败和循环开销" << std::endl;
    std::cout << "3. 适合小次数循环（如处理几个 ADC 通道）" << std::endl;
    std::cout << "4. 循环次数大时会导致代码膨胀，需权衡" << std::endl;

    return 0;
}
