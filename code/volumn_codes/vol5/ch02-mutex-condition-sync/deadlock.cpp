#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> a(mtx_a);               // 拿 A
    std::cout << "t1: 拿到 A，伸手等 B\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> b(mtx_b);               // 等 B：B 在 t2 手里
    std::cout << "t1: 两把都到手\n";
}

void thread2()
{
    std::lock_guard<std::mutex> b(mtx_b);               // 拿 B
    std::cout << "t2: 拿到 B，伸手等 A\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::lock_guard<std::mutex> a(mtx_a);               // 等 A：A 在 t1 手里
    std::cout << "t2: 两把都到手\n";
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    std::cout << "程序正常收尾（死锁时到不了这里）\n";
    return 0;
}

// ---------------------------------------------------------------------------
// 注：本文件是 documents/vol5-concurrency/ch02-mutex-condition-sync/03-deadlock-and-gdb.md
// 的独立现场程序——正文「35 行的完整代码」原样落盘，行号与文中 gdb 会话锚定：
//   :11 = thread1 拿 mtx_a（持有边）      :14 = thread1 等 mtx_b（等待边）
//   :20 = thread2 拿 mtx_b（持有边）      :23 = thread2 等 mtx_a（等待边）
//   :31 = t1.join()（main 是受害者）
// 这段注释放在文件尾部而不是头部，就是为了不动上面 35 行的行号——别把它挪到
// 开头，也别在前头加任何行（连空行都是行号的一部分）。
//
// 编译与复跑（与正文 L77-80 的命令一致）：
//   g++ -std=c++20 -Wall -Wextra -pedantic -pthread -g -O0 deadlock.cpp -o deadlock
//   timeout 3 ./deadlock
//   echo $?                     # 预期：两行打印后挂住，退出码 124
// gdb 三命令判读：从 gdb 里 run，挂住后 Ctrl-C，然后依次
//   info threads / thread apply all bt / thread N + bt，
// 再用 print mtx_a._M_mutex.__data.__owner 的值对 info threads 里的 LWP，
// 持有边有了直接证据，四条边闭成环，死锁定案。
// ---------------------------------------------------------------------------
