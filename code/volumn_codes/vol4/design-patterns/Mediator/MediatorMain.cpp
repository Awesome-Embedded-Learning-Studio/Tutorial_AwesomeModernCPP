#include "Mediator.h"
#include <memory>

// 驱动文章第二步「聊天室」教学代码的 main。
// 复现文章里那段期望输出:Alice 私聊 Bob 只命中 Bob;Bob 的广播同时送到
// Alice 和 Carol、不回送自己;Carol 想私聊不存在的 Dave,中介者统一兜底。
int main() {
    ChatRoom room;

    auto alice = std::make_shared<User>("Alice", &room);
    auto bob = std::make_shared<User>("Bob", &room);
    auto carol = std::make_shared<User>("Carol", &room);

    room.register_user(alice);
    room.register_user(bob);
    room.register_user(carol);

    // 私聊:只命中目标
    alice->send_to("Bob", "hi Bob!");

    // 广播:送到除自己外的所有人
    bob->broadcast("hello everyone, I'm Bob");

    // 私聊不在线的人:由中介者统一兜底
    carol->send_to("Dave", "are you there?");

    return 0;
}
