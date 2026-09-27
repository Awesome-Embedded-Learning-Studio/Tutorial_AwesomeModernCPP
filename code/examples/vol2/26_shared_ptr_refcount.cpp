#include <iostream>
#include <memory>

struct Connection {
    explicit Connection(const std::string& addr) : addr_(addr) {
        std::cout << "Connected to " << addr_ << "\n";
    }
    ~Connection() { std::cout << "Disconnected from " << addr_ << "\n"; }
    void send(const std::string& msg) { std::cout << "Send to " << addr_ << ": " << msg << "\n"; }

  private:
    std::string addr_;
};

void demo_shared() {
    auto conn = std::make_shared<Connection>("192.168.1.1:8080");
    {
        auto conn2 = conn; // 引用计数: 1 → 2
        conn2->send("hello from conn2");
        std::cout << "use_count: " << conn.use_count() << "\n"; // 2
    } // conn2 离开作用域，引用计数: 2 → 1

    conn->send("hello from conn");
    std::cout << "use_count: " << conn.use_count() << "\n"; // 1
} // conn 离开作用域，引用计数: 1 → 0，Connection 被销毁

int main() {
    demo_shared();
    return 0;
}
