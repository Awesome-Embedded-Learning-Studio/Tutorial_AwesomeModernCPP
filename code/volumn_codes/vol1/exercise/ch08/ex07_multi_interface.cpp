/**
 * @file ex07_multi_interface.cpp
 * @brief 练习：多接口实现
 *
 * LogEntry 同时实现三个接口：
 *   IPrintable   —— 格式化输出
 *   ISerializable —— 序列化为 JSON 字符串
 *   IFilterable   —— 按关键字匹配过滤
 * 字段: timestamp(int), level(string), message(string)
 */

#include <iostream>
#include <string>

class IPrintable {
public:
    virtual ~IPrintable() = default;
    virtual void print(std::ostream& os) const = 0;
};

class ISerializable {
public:
    virtual ~ISerializable() = default;
    virtual std::string to_json() const = 0;
};

class IFilterable {
public:
    virtual ~IFilterable() = default;
    virtual bool matches(const std::string& keyword) const = 0;
};

class LogEntry : public IPrintable,
                 public ISerializable,
                 public IFilterable {
private:
    int timestamp_;
    std::string level_;
    std::string message_;

public:
    LogEntry(int ts, const std::string& level,
             const std::string& msg)
        : timestamp_(ts), level_(level), message_(msg) {}

    // IPrintable
    void print(std::ostream& os) const override
    {
        os << "[" << timestamp_ << "]["
           << level_ << "] "
           << message_;
    }

    // ISerializable
    std::string to_json() const override
    {
        return std::string("{")
            + "\"timestamp\":" + std::to_string(timestamp_) + ","
            + "\"level\":\"" + level_ + "\","
            + "\"message\":\"" + message_ + "\""
            + "}";
    }

    // IFilterable —— 在 level 和 message 中搜索关键字
    bool matches(const std::string& keyword) const override
    {
        return level_.find(keyword) != std::string::npos
            || message_.find(keyword) != std::string::npos;
    }

    // Getters
    int timestamp() const { return timestamp_; }
    const std::string& level() const { return level_; }
    const std::string& message() const { return message_; }
};

// 按接口过滤的辅助函数
void print_if_matches(const IFilterable& filterable,
                      const std::string& keyword)
{
    if (filterable.matches(keyword)) {
        // 安全向下转型：我们传的是 LogEntry
        const auto& entry =
            static_cast<const LogEntry&>(filterable);
        entry.print(std::cout);
        std::cout << "\n";
    }
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== 多接口实现 =====\n\n";

    LogEntry e1(1000, "INFO", "Server started");
    LogEntry e2(1005, "ERROR", "Connection refused");
    LogEntry e3(1010, "WARN", "Low memory warning");
    LogEntry e4(1015, "ERROR", "Disk write failed");

    // 通过 IPrintable 接口使用
    std::cout << "--- IPrintable 接口 ---\n";
    e1.print(std::cout); std::cout << "\n";
    e2.print(std::cout); std::cout << "\n";

    // 通过 ISerializable 接口使用
    std::cout << "\n--- ISerializable 接口 ---\n";
    std::cout << e1.to_json() << "\n";
    std::cout << e2.to_json() << "\n";

    // 通过 IFilterable 接口过滤
    std::cout << "\n--- IFilterable 接口（过滤 ERROR）---\n";
    print_if_matches(e1, "ERROR");
    print_if_matches(e2, "ERROR");
    print_if_matches(e3, "ERROR");
    print_if_matches(e4, "ERROR");

    std::cout << "\n--- IFilterable 接口（过滤 memory）---\n";
    print_if_matches(e1, "memory");
    print_if_matches(e2, "memory");
    print_if_matches(e3, "memory");
    print_if_matches(e4, "memory");

    return 0;
}
