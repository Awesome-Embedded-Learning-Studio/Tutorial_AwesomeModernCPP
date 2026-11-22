// TaskQueue 与停止令牌:condition_variable_any 的带 token 重载,停的时候不丢锁也不睡死
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
                    std::println("[worker] wait returned false, stopping");
                    break;
                }
                task = q_.front();
                q_.pop();
            }
            std::println("[worker] processed {}", task);
            std::this_thread::sleep_for(30ms);
        }
    }

  private:
    std::mutex m_;
    std::condition_variable_any cv_;
    std::queue<int> q_;
};

int main() {
    TaskQueue q;
    std::jthread worker([&q](std::stop_token token) { q.run(token); });
    for (int task = 0; task < 3; ++task) {
        q.submit(task);
        std::this_thread::sleep_for(40ms);
    }
    std::println("[main] requesting stop");
    worker.request_stop();
    std::println("[main] request_stop returned");
}
