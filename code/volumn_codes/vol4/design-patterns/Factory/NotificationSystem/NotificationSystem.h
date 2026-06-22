#pragma once
#include <functional>
#include <memory>
#include <print>
#include <string>
#include <unordered_map>
class AbstractNocification {
  public:
    virtual void send_message(const std::string& message) = 0;
    virtual ~AbstractNocification() = default;
};

class EmailNocification : public AbstractNocification {
  public:
    void send_message(const std::string& message) override { std::print("[Email]: {}", message); }
};

class SMSNocification : public AbstractNocification {
  public:
    void send_message(const std::string& message) override { std::print("[SMS]: {}", message); }
};

class PushNocification : public AbstractNocification {
  public:
    void send_message(const std::string& message) override { std::print("[Push]: {}", message); }
};

class NocificationCreator {
  public:
    std::unique_ptr<AbstractNocification> notification_creator(const std::string& s) {
        auto functor = creator_type_mape.find(s);
        return (functor != creator_type_mape.end() ? (functor->second)() : nullptr);
    }

  private:
    std::unordered_map<std::string, std::function<std::unique_ptr<AbstractNocification>(void)>>
        creator_type_mape = {
            {"Email",
             []() -> std::unique_ptr<AbstractNocification> {
                 return std::make_unique<EmailNocification>();
             }},
            {"SMS",
             []() -> std::unique_ptr<AbstractNocification> {
                 return std::make_unique<SMSNocification>();
             }},
            {"Push",
             []() -> std::unique_ptr<AbstractNocification> {
                 return std::make_unique<PushNocification>();
             }},
    };
};
