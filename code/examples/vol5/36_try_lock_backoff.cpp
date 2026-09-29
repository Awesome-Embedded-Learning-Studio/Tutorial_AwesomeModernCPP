// 防线三：try_lock 回退——第二把摸不到就全放，退避重试
#include <iostream>
#include <mutex>
#include <thread>

std::mutex mtx_a;
std::mutex mtx_b;

void worker(int id, int rounds) {
    for (int round = 0; round != rounds; ++round) {
        while (true) {
            std::unique_lock<std::mutex> a(mtx_a, std::defer_lock);
            if (!a.try_lock()) {
                std::this_thread::yield(); // 头一把就没摸到，让一让再来
                continue;
            }
            std::unique_lock<std::mutex> b(mtx_b, std::defer_lock);
            if (b.try_lock()) {
                break; // 两把都在手：进临界区
            }
            std::this_thread::yield(); // 第二把没摸到：a 随析构放掉，空手回去
        }
        // ……临界区干活：a、b 一直看管到本轮结束……
    }
    std::cout << "worker " << id << " done\n";
}

int main() {
    std::thread t1(worker, 1, 100000);
    std::thread t2(worker, 2, 100000);
    t1.join();
    t2.join();
    std::cout << "程序正常收尾\n";
    return 0;
}
