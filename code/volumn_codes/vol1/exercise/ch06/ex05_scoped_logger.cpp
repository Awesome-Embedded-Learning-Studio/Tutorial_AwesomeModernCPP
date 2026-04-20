/**
 * @file ex05_scoped_logger.cpp
 * @brief 练习：ScopedLogger 类
 *
 * 在构造时记录当前时间戳（HH:MM:SS 格式），
 * 在析构时打印从构造到析构经过的秒数，演示 RAII 思想。
 */

#include <chrono>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

class ScopedLogger {
private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::string label_;
    TimePoint start_time_;

    // 获取当前时间的 HH:MM:SS 字符串
    static std::string current_time_str() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now_time));
        return std::string(buf);
    }

public:
    explicit ScopedLogger(const std::string& label)
        : label_(label), start_time_(Clock::now()) {
        std::cout << "[" << label_ << "] 构造于 "
                  << current_time_str() << "\n";
    }

    ~ScopedLogger() {
        auto end_time = Clock::now();
        double elapsed =
            std::chrono::duration<double>(end_time - start_time_).count();
        std::cout << "[" << label_ << "] 析构于 "
                  << current_time_str()
                  << "，存活 " << elapsed << " 秒\n";
    }

    // 禁止拷贝
    ScopedLogger(const ScopedLogger&) = delete;
    ScopedLogger& operator=(const ScopedLogger&) = delete;
};

// 模拟一些工作
void simulate_work(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int main() {
    std::cout << "===== ScopedLogger 类 =====\n\n";

    {
        std::cout << "--- 进入外层作用域 ---\n";
        ScopedLogger outer("外层");

        {
            std::cout << "\n进入内层作用域:\n";
            ScopedLogger inner("内层");
            simulate_work(100);
            std::cout << "内层工作完成\n";
        }
        // inner 在此处析构

        std::cout << "\n外层继续工作...\n";
        simulate_work(200);

        std::cout << "\n--- 即将离开外层作用域 ---\n";
    }
    // outer 在此处析构

    std::cout << "\n要点:\n";
    std::cout << "  构造函数记录时间戳和起始时刻\n";
    std::cout << "  析构函数自动计算并打印存活时长\n";
    std::cout << "  RAII 思想：资源获取即初始化，析构即释放\n";

    return 0;
}
