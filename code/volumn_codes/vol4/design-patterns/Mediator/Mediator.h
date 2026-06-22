#pragma once
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

// 抽象中介者接口:同事对象只依赖这一个抽象头,谁也不认识谁。
// 这里用文章里聊天室的协议——私聊 + 广播,具体怎么路由由中介者决定。
struct IMediator {
    virtual ~IMediator() = default;
    virtual void send_message(const std::string& from, const std::string& to,
                              const std::string& msg) = 0;
    virtual void broadcast(const std::string& from, const std::string& msg) = 0;
};

// 抽象同事:只持有一个 IMediator*,完全不出现其它同事类型的名字。
// 这一行就是「星形耦合」的全部秘密——User 压根不知道世界上还有别的 User。
class User {
  public:
    User(std::string name, IMediator* mediator) : name_(std::move(name)), mediator_(mediator) {}

    const std::string& name() const { return name_; }

    void send_to(const std::string& to, const std::string& msg) {
        mediator_->send_message(name_, to, msg); // 转交中介者
    }
    void broadcast(const std::string& msg) {
        mediator_->broadcast(name_, msg); // 转交中介者
    }
    void receive(const std::string& from, const std::string& msg) {
        std::cout << "[" << name_ << "] recv from " << from << ": " << msg << "\n";
    }

  private:
    std::string name_;
    IMediator* mediator_; // 只依赖抽象,不知道其它 User 的存在
};

// 具体中介者:持有 name -> User 的表,负责编排消息路由。
// 所有「发给谁 / 找不到怎么办 / 要不要广播排除自己」的决策都集中在这里。
// 以后加敏感词过滤、审计日志、离线消息,只改这一个类,User 一行都不用动。
class ChatRoom : public IMediator {
  public:
    void register_user(std::shared_ptr<User> user) { users_[user->name()] = std::move(user); }

    void send_message(const std::string& from, const std::string& to,
                      const std::string& msg) override {
        auto it = users_.find(to);
        if (it != users_.end()) {
            it->second->receive(from, msg);
        } else {
            // 「目标不存在」的处理策略,集中放在中介者里
            std::cout << "[room] " << to << " not online (handled by mediator)\n";
        }
    }

    void broadcast(const std::string& from, const std::string& msg) override {
        for (auto& [name, user] : users_) {
            if (name != from)
                user->receive(from, msg);
        }
    }

  private:
    std::unordered_map<std::string, std::shared_ptr<User>> users_;
};
