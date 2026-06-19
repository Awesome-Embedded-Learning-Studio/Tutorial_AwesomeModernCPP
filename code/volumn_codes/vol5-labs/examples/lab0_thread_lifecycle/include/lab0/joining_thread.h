#pragma once

#include <thread>
#include <utility>

namespace lab0 {

/**
 * @brief JoiningThread is a pre-implemented std::jthread in C++
 *        RAII wrapper of thread
 *
 */
class JoiningThread {
  public:
    /**
     * @brief Template Meta Programming, simply means to redirect the args and
     *        functions by once
     *
     * @tparam Callable: function-like, or anything can be thought as a "function"
     * @tparam Args
     * @param f
     * @param args
     */
    template <class Callable, class... Args> explicit JoiningThread(Callable&& f, Args&&... args)
        : thread_(std::forward<Callable>(f), std::forward<Args>(args)...) {}

    /**
     * @brief moves a thread which out of control in control
     *
     * @param t
     */
    explicit JoiningThread(std::thread t) : thread_(std::move(t)) {}

    JoiningThread(JoiningThread&& other) : thread_(std::move(other.thread_)) {}

    JoiningThread& operator=(JoiningThread&& other [[maybe_unused]]) noexcept {
        // here we got one thing, self has been running possibly.
        // we need to end up ourselves
        if (thread_.joinable()) {
            thread_.join(); // We have to join this
        }

        thread_ = std::move(other.thread_);
        return *this;
    }

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

    ~JoiningThread() {
        try {
            this->join();
        } catch (...) {
            // 析构里 join 可能抛异常, 吞掉避免 std::terminate(MS2 设计点)
        }
    };

    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool joinable() const noexcept { return thread_.joinable(); }

  private:
    std::thread thread_{};
};

} // namespace lab0
