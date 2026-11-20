// 停止时的剩余任务:stop_requested 后清点队列,丢掉没跑的
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <print>
#include <queue>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

class TaskQueue {
  public:
    void submit(int task) {
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.push(task);
        }
        cv_.notify_one();
    }

    void run(std::stop_token token) {
        while (!token.stop_requested()) {
            int task = 0;
            {
                std::unique_lock<std::mutex> lock(m_);
                if (!cv_.wait(lock, token, [this] { return !q_.empty(); })) {
                    std::println("[worker] wait returned false");
                    break;
                }
                task = q_.front();
                q_.pop();
            }
            std::println("[worker] ran task {}", task);
            std::this_thread::sleep_for(50ms);
        }
        std::lock_guard<std::mutex> lock(m_);
        std::println("[worker] stop requested, {} tasks left in the queue, dropping them",
                     q_.size());
        q_ = {};
    }

  private:
    std::mutex m_;
    std::condition_variable_any cv_;
    std::queue<int> q_;
};

int main() {
    TaskQueue q;
    std::jthread worker([&q](std::stop_token token) { q.run(token); });
    for (int task = 0; task < 10; ++task)
        q.submit(task);
    std::this_thread::sleep_for(60ms);
    std::println("[main] request_stop");
    worker.request_stop();
}
