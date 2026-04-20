/**
 * @file ex02_timer.cpp
 * @brief 练习：Timer 类
 *
 * 实现一个基于 std::chrono::steady_clock 的计时器类，
 * 支持 start()、stop()、elapsed_seconds() 操作。
 */

#include <chrono>
#include <iostream>
#include <thread>

class Timer {
private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint start_time_;
    TimePoint end_time_;
    bool running_;

public:
    Timer() : running_(false) {}

    // 开始计时
    void start() {
        start_time_ = Clock::now();
        running_ = true;
    }

    // 停止计时
    void stop() {
        if (running_) {
            end_time_ = Clock::now();
            running_ = false;
        }
    }

    // 获取经过的秒数（double）
    double elapsed_seconds() const {
        if (running_) {
            // 仍在运行，返回当前已过时间
            auto elapsed = Clock::now() - start_time_;
            return std::chrono::duration<double>(elapsed).count();
        }
        auto elapsed = end_time_ - start_time_;
        return std::chrono::duration<double>(elapsed).count();
    }

    // 获取经过的毫秒数
    double elapsed_milliseconds() const {
        return elapsed_seconds() * 1000.0;
    }

    bool is_running() const {
        return running_;
    }
};

int main() {
    std::cout << "===== Timer 类 =====\n\n";

    Timer timer;

    // 测试 1：等待约 100ms
    std::cout << "测试 1: 等待约 100ms\n";
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    timer.stop();
    std::cout << "  耗时: " << timer.elapsed_seconds() << " 秒"
              << " (" << timer.elapsed_milliseconds() << " ms)\n\n";

    // 测试 2：等待约 500ms
    std::cout << "测试 2: 等待约 500ms\n";
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    timer.stop();
    std::cout << "  耗时: " << timer.elapsed_seconds() << " 秒"
              << " (" << timer.elapsed_milliseconds() << " ms)\n\n";

    // 测试 3：实时查询（不 stop）
    std::cout << "测试 3: 实时查询\n";
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "  运行中: " << timer.elapsed_seconds() << " 秒\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "  运行中: " << timer.elapsed_seconds() << " 秒\n";
    timer.stop();
    std::cout << "  停止后: " << timer.elapsed_seconds() << " 秒\n";

    return 0;
}
