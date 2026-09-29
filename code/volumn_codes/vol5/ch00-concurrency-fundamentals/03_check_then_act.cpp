// 03_check_then_act.cpp —— 没有 data race、却仍然是 race condition 的例子
// 每次访问 data 都拿着锁，TSan 全程安静；但“检查”和“入队”是两段临界区，
// 中间的窗口照样能让容量上限被突破（最终 size 超过 100）。
// 窗口原本只有几纳秒，不容易碰上；check 与 push 之间睡 1 毫秒把它人为拉宽，
// 超编基本一跑就有。删掉那行睡，代码的错一点没少，只是更难看见了。
// 编译（TSan 版）: g++ -fsanitize=thread -g -O2 -pthread 03_check_then_act.cpp -o cta_tsan
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

std::vector<int> data;
std::mutex data_mtx;
constexpr int kCapacity = 100;

void add_if_not_full(int value)
{
    {
        std::lock_guard<std::mutex> lock(data_mtx);
        if (static_cast<int>(data.size()) >= kCapacity) {
            return;  // 检查：拿着锁
        }
    }  // 锁在这里放下了，窗口敞开
    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 拉宽窗口用，见文件头注释
    {
        std::lock_guard<std::mutex> lock(data_mtx);
        data.push_back(value);  // 操作：重新拿锁
    }
}

int main()
{
    std::thread t1([] {
        for (int i = 0; i < 60; ++i) {
            add_if_not_full(i);
        }
    });
    std::thread t2([] {
        for (int i = 0; i < 60; ++i) {
            add_if_not_full(1000 + i);
        }
    });
    t1.join();
    t2.join();
    std::cout << "final size = " << data.size() << "\n";  // 上限是 100，多跑几次看超不超
    return 0;
}
