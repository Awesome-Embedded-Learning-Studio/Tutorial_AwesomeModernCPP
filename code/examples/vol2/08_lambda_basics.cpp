// 08_lambda_basics.cpp
// Lambda 基础：事件处理系统中的引用捕获与值捕获

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>

class EventDispatcher {
  public:
    using Handler = std::function<void(uint32_t)>;
    void on_event(int id, Handler handler) {
        if (id >= 0 && id < static_cast<int>(handlers_.size())) {
            handlers_[id] = std::move(handler);
        }
    }
    void trigger(int id, uint32_t timestamp) {
        if (id >= 0 && id < static_cast<int>(handlers_.size()) && handlers_[id]) {
            handlers_[id](timestamp);
        }
    }

  private:
    std::array<Handler, 8> handlers_;
};

int main() {
    EventDispatcher dispatcher;
    int press_count = 0;
    uint32_t last_press_time = 0;

    dispatcher.on_event(0, [&](uint32_t timestamp) {
        if (timestamp - last_press_time > 50) {
            press_count++;
            last_press_time = timestamp;
            std::cout << "Press #" << press_count << " at " << timestamp << "ms\n";
        }
    });

    uint32_t threshold = 1000;
    dispatcher.on_event(1, [threshold](uint32_t timestamp) {
        if (timestamp > threshold) {
            std::cout << "Timeout at " << timestamp << "ms\n";
        }
    });

    dispatcher.trigger(0, 100);
    dispatcher.trigger(0, 160);
    dispatcher.trigger(0, 180);
    dispatcher.trigger(1, 1200);

    return 0;
}
