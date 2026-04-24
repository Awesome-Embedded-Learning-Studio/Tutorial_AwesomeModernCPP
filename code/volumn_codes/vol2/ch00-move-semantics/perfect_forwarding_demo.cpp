// perfect_forwarding.cpp -- 完美转发演示
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <memory>
#include <functional>

/// @brief 一个简单的缓存包装器
/// 完美转发函数参数，同时保持值类别信息
template<typename Key, typename Value>
class Cache
{
    std::map<Key, Value> storage_;

public:
    /// @brief 查找或插入：如果 key 不存在则用 args 构造 Value
    template<typename... Args>
    Value& emplace_get(const Key& key, Args&&... args)
    {
        auto it = storage_.find(key);
        if (it != storage_.end()) {
            std::cout << "  [缓存命中] key = " << key << "\n";
            return it->second;
        }

        std::cout << "  [缓存未命中] key = " << key << "，构造新值\n";
        auto [new_it, inserted] = storage_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(std::forward<Args>(args)...)
        );
        return new_it->second;
    }

    std::size_t size() const { return storage_.size(); }
};

/// @brief 被包装的"昂贵"操作
class ExpensiveData
{
    std::string label_;
    int value_;

public:
    /// @brief 从字符串和整数构造
    ExpensiveData(std::string label, int value)
        : label_(std::move(label))
        , value_(value)
    {
        std::cout << "  [ExpensiveData] 构造: " << label_
                  << " = " << value_ << "\n";
    }

    /// @brief 从字符串构造（重载）
    explicit ExpensiveData(std::string label)
        : label_(std::move(label))
        , value_(0)
    {
        std::cout << "  [ExpensiveData] 构造(仅标签): " << label_ << "\n";
    }

    const std::string& label() const { return label_; }
    int value() const { return value_; }
};

/// @brief 通用的转发包装器——演示完美转发的核心用法
template<typename Func, typename... Args>
auto invoke_and_log(Func&& func, Args&&... args)
    -> std::invoke_result_t<Func, Args...>
{
    std::cout << "  [invoke_and_log] 调用前\n";
    auto result = std::invoke(
        std::forward<Func>(func),
        std::forward<Args>(args)...
    );
    std::cout << "  [invoke_and_log] 调用后\n";
    return result;
}

int main()
{
    std::cout << "=== 1. 缓存包装器 ===\n";
    Cache<std::string, ExpensiveData> cache;

    // 第一次调用：缓存未命中，构造新值
    // 传入右值字符串和整数
    cache.emplace_get("alpha", "first", 100);

    // 第二次调用：同样的 key，缓存命中
    cache.emplace_get("alpha", "first", 200);

    // 新 key，传入右值字符串（单参数构造）
    std::string label = "beta";
    cache.emplace_get("beta", std::move(label));
    // label 已被移动，不要再使用

    std::cout << "  缓存大小: " << cache.size() << "\n\n";

    std::cout << "=== 2. 转发包装器 ===\n";
    auto add = [](int a, int b) -> int {
        return a + b;
    };

    int x = 10;
    int result = invoke_and_log(add, x, 20);
    std::cout << "  结果: " << result << "\n\n";

    std::cout << "=== 3. make_unique 风格的工厂 ===\n";
    // 演示完美转发在构造函数参数传递中的效果
    auto data = std::make_unique<ExpensiveData>("gamma", 42);
    std::cout << "  data: " << data->label() << " = " << data->value() << "\n\n";

    std::cout << "=== 程序结束 ===\n";
    return 0;
}
