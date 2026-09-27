#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class EventListener {
  public:
    virtual ~EventListener() = default;
    virtual void on_event(const std::string& msg) = 0;
};

class ConsoleListener : public EventListener {
  public:
    explicit ConsoleListener(const std::string& name) : name_(name) {
        std::cout << "Listener(" << name_ << ") 创建\n";
    }
    ~ConsoleListener() override { std::cout << "~Listener(" << name_ << ") 销毁\n"; }
    void on_event(const std::string& msg) override {
        std::cout << "[" << name_ << "] 收到事件: " << msg << "\n";
    }

  private:
    std::string name_;
};

class EventBus {
  public:
    void subscribe(std::shared_ptr<EventListener> listener) {
        listeners_.push_back(listener); // 存储 weak_ptr
    }

    void publish(const std::string& msg) {
        // 清理已销毁的观察者
        listeners_.erase(
            std::remove_if(listeners_.begin(), listeners_.end(),
                           [](const std::weak_ptr<EventListener>& w) { return w.expired(); }),
            listeners_.end());

        // 通知所有存活的观察者
        for (const auto& weak : listeners_) {
            if (auto listener = weak.lock()) {
                listener->on_event(msg);
            }
        }
    }

  private:
    std::vector<std::weak_ptr<EventListener>> listeners_;
};

void observer_demo() {
    EventBus bus;

    {
        auto l1 = std::make_shared<ConsoleListener>("L1");
        auto l2 = std::make_shared<ConsoleListener>("L2");

        bus.subscribe(l1);
        bus.subscribe(l2);

        bus.publish("第一条消息");
        // L1 和 L2 都能收到

        std::cout << "--- L2 离开作用域 ---\n";
    }
    // L1 和 L2 都离开了作用域
    // 但 EventBus 用的是 weak_ptr，所以不会阻止它们被销毁

    bus.publish("第二条消息");
    // 没有观察者能收到——它们已经被销毁了
}

int main() {
    observer_demo();
    return 0;
}
