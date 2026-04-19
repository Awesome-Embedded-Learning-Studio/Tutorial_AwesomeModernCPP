// variant_message.cpp
// 使用 variant 实现消息队列示例

#include <variant>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>

// 消息类型定义
struct Heartbeat { int id; };
struct Text { std::string s; };
struct Blob { std::vector<uint8_t> data; };

using Message = std::variant<Heartbeat, Text, Blob>;

// 重载集合
template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// 处理消息
void process(const Message& m) {
    std::cout << "Processing: ";
    std::visit(overloaded{
        [](const Heartbeat& h) { std::cout << "HB " << h.id << "\n"; },
        [](const Text& t)      { std::cout << "Text: " << t.s << "\n"; },
        [](const Blob& b)      { std::cout << "Blob size: " << b.data.size() << " bytes\n"; }
    }, m);
}

int main() {
    std::cout << "=== Message Queue Example ===\n\n";

    // 创建不同类型的消息
    Message msg1 = Heartbeat{42};
    Message msg2 = Text{"Hello, world!"};
    Message msg3 = Blob{std::vector<uint8_t>{0x01, 0x02, 0x03}};

    // 处理消息
    process(msg1);
    process(msg2);
    process(msg3);

    // 演示 in-place 构造
    Message msg4 = Text{std::string(10, 'x')}; // "xxxxxxxxxx"
    process(msg4);

    return 0;
}
