#pragma once

#include <thread>
#include <utility>

namespace lab0 {

/**
 * @brief JoiningThread is a pre-implemented std::jthread in C++
 *
 */
class JoiningThread {
  public:
    template <class Callable, class... Args> explicit JoiningThread(Callable&& f, Args&&... args)
        : thread_(std::forward<Callable>(f), std::forward<Args>(args)...) {}

    explicit JoiningThread(std::thread t) noexcept;

    JoiningThread(JoiningThread&& other) noexcept;

    JoiningThread& operator=(JoiningThread&& other) noexcept;

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

    ~JoiningThread() {};

    void join();

    bool joinable() const noexcept;

  private:
    std::thread thread_;
};

} // namespace lab0
