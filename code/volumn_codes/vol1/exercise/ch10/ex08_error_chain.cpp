/**
 * @file ex08_error_chain.cpp
 * @brief 练习：错误传播链（monadic and_then）
 *
 * 实现 Expected<T, Error> 并提供 and_then 进行链式调用。
 * 演示 read_file -> parse_config -> validate_config 三步流水线，
 * 任何一步失败都会自动向上传播，无需嵌套 if。
 */

#include <iostream>
#include <map>
#include <string>
#include <variant>

// ---- Error 定义 ----

/// @brief 错误信息，附带产生错误的步骤上下文
struct Error {
    std::string message;
    std::string context;

    std::string to_string() const
    {
        return "[" + context + "] " + message;
    }
};

// ---- Expected<T, Error> ----

/// @brief 简易 Expected<T, Error> —— 要么持有值，要么持有 Error
template <typename T>
class Expected {
public:
    using value_type = T;

private:
    std::variant<T, Error> data_;
    bool ok_;

public:
    /// @brief 成功时构造
    Expected(T value) : data_(std::move(value)), ok_(true) {}

    /// @brief 失败时构造
    Expected(Error err) : data_(std::move(err)), ok_(false) {}

    bool ok() const { return ok_; }

    const T& value() const { return std::get<0>(data_); }

    const Error& error() const { return std::get<1>(data_); }

    /// @brief 链式操作：成功时调用 func 继续处理，失败时直接传播错误
    template <typename Func>
    auto and_then(Func func)
        -> Expected<typename std::invoke_result_t<Func, T>::value_type>
    {
        using U = typename std::invoke_result_t<Func, T>::value_type;
        if (ok_) {
            return func(std::get<0>(data_));
        }
        return Expected<U>(std::get<1>(data_));
    }
};

// ---- 三步流水线 ----

using Config = std::map<std::string, std::string>;

/// @brief 第一步：读取文件
Expected<std::string> read_file(const std::string& path)
{
    if (path.empty()) {
        return Error{"file path is empty", "read_file"};
    }
    if (path[0] == '!') {
        return Error{"file not found: " + path, "read_file"};
    }
    // 模拟文件内容
    return std::string("key1=value1\nkey2=42\nkey3=enabled");
}

/// @brief 第二步：解析配置内容为键值对
Expected<Config> parse_config(const std::string& content)
{
    if (content.empty()) {
        return Error{"config content is empty", "parse_config"};
    }

    Config cfg;
    std::size_t start = 0;
    int line_num = 0;

    while (start < content.size()) {
        auto end = content.find('\n', start);
        if (end == std::string::npos) {
            end = content.size();
        }
        std::string line = content.substr(start, end - start);
        start = end + 1;
        ++line_num;

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            return Error{"line " + std::to_string(line_num) + " missing '='",
                         "parse_config"};
        }
        cfg[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return cfg;
}

/// @brief 第三步：验证配置内容
Expected<Config> validate_config(const Config& cfg)
{
    if (cfg.find("key1") == cfg.end()) {
        return Error{"missing required key 'key1'", "validate_config"};
    }
    auto it = cfg.find("key2");
    if (it != cfg.end()) {
        try {
            std::stoi(it->second);
        }
        catch (...) {
            return Error{"key2 must be a number, got '" + it->second + "'",
                         "validate_config"};
        }
    }
    return cfg;
}

// ---- 用 and_then 构建链式调用 ----

void run_pipeline(const std::string& path)
{
    std::cout << "  输入路径: \"" << path << "\"\n";

    // 使用 and_then 链式调用，任何一步失败都会短路传播
    auto result = read_file(path)
                      .and_then(parse_config)
                      .and_then(validate_config);

    if (result.ok()) {
        std::cout << "    配置加载成功:\n";
        for (const auto& [k, v] : result.value()) {
            std::cout << "      " << k << " = " << v << "\n";
        }
    }
    else {
        std::cout << "    错误: " << result.error().to_string() << "\n";
    }
    std::cout << "\n";
}

int main()
{
    std::cout << "===== 错误传播链（and_then） =====\n\n";

    // 正常路径
    run_pipeline("config.txt");

    // 文件不存在（以 '!' 开头触发）
    run_pipeline("!missing.txt");

    // 空路径
    run_pipeline("");

    // 手动测试 parse 失败：构造一个不含 '=' 的内容
    std::cout << "  --- 直接测试 parse 失败 ---\n";
    auto bad_parse = parse_config("no_equals_here");
    if (!bad_parse.ok()) {
        std::cout << "    错误: " << bad_parse.error().to_string() << "\n\n";
    }

    std::cout << "要点:\n";
    std::cout << "  1. and_then 实现了 monadic 链式调用，失败自动短路\n";
    std::cout << "  2. 每一步的错误都带有上下文信息，方便定位问题\n";
    std::cout << "  3. 和异常不同，控制流是线性的，不会跳转\n";

    return 0;
}
