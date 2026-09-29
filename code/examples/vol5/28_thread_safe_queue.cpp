// ThreadSafeQueue 黑盒演示：消费者在 pop 里安静等了 100ms（没有忙转），取完 1..8 后 sum = 36
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

template <typename T> class ThreadSafeQueue {
  public:
    void push(T value) {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(value));
        } // 临界区到此为止：锁在花括号处归还
        cv_.notify_one(); // 通知放在锁外，为什么，收编时讲
    }

    // 黑盒成员：为什么 pop 能「等人」（队列空时不忙等、不空转），
    // 答案在 ch02/04 condition_variable 一篇，此处只管用。
    T pop() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return !q_.empty(); });
        T value = std::move(q_.front());
        q_.pop_front();
        return value;
    }

  private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> q_;
};

int main() {
    ThreadSafeQueue<int> q;
    long sum = 0;
    std::jthread consumer([&] {
        for (int i = 0; i != 8; ++i) {
            sum += q.pop(); // 队列空的时候，这里能等人
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "消费者已在 pop 里等了 100ms（没有忙转）\n";
    for (int i = 1; i <= 8; ++i) {
        q.push(i);
    }
    consumer.join();
    std::cout << "消费者取完 1..8, sum = " << sum << "（期望 36）\n";
    return 0;
}
