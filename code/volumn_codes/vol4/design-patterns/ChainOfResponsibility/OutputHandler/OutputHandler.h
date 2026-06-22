#pragma once
#include <list>
#include <memory>
#include <print>
#include <string>
struct Message {
    enum Type { DISK, CONSOLE, GUI_SCREEN };

    Message(Type t, std::string message) : t(t), message(std::move(message)) {}

    const Type t;
    const std::string message;
};

struct Handler {
    virtual ~Handler() = default;

    virtual bool canAccept(const Message& message) = 0;
    virtual void processSessions(const Message& message) = 0;
};

struct DiskHandler : Handler {
    bool canAccept(const Message& message) override { return message.t == message.DISK; }
    void processSessions(const Message& message) override {
        std::println("From Disk: {}", message.message);
    }
};

struct ConsoleHandler : Handler {
    bool canAccept(const Message& message) override { return message.t == message.CONSOLE; }
    void processSessions(const Message& message) override {
        std::println("From Console: {}", message.message);
    }
};

struct GuiHandler : Handler {
    bool canAccept(const Message& message) override { return message.t == message.GUI_SCREEN; }
    void processSessions(const Message& message) override {
        std::println("From GUI: {}", message.message);
    }
};

struct HandlerChain {
    HandlerChain() {
        handlers.emplace_back(std::make_shared<DiskHandler>());
        handlers.emplace_back(std::make_shared<ConsoleHandler>());
        handlers.emplace_back(std::make_shared<GuiHandler>());
    }

    void processMessageType(const Message& t) {
        for (auto it = handlers.begin(); it != handlers.end(); it++) {
            if (it->get()->canAccept(t)) {
                it->get()->processSessions(t);
            }
        }
    }

  private:
    std::list<std::shared_ptr<Handler>> handlers;
};
