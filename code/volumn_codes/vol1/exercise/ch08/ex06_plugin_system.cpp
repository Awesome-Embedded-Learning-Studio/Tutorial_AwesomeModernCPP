/**
 * @file ex06_plugin_system.cpp
 * @brief 练习：插件系统
 *
 * IPlugin 接口定义 name/version/initialize/shutdown。
 * 实现 LoggerPlugin、TimerPlugin、ConfigPlugin 三个具体插件。
 * PluginManager 用 vector<unique_ptr<IPlugin>> 管理生命周期。
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual std::string name() const = 0;
    virtual std::string version() const = 0;
    virtual void initialize() = 0;
    virtual void shutdown() = 0;
};

class LoggerPlugin : public IPlugin {
public:
    std::string name() const override { return "Logger"; }
    std::string version() const override { return "1.0.0"; }

    void initialize() override
    {
        std::cout << "  [Logger] v" << version()
                  << " 已初始化 —— 日志系统就绪\n";
    }

    void shutdown() override
    {
        std::cout << "  [Logger] 已关闭\n";
    }
};

class TimerPlugin : public IPlugin {
public:
    std::string name() const override { return "Timer"; }
    std::string version() const override { return "2.1.3"; }

    void initialize() override
    {
        std::cout << "  [Timer] v" << version()
                  << " 已初始化 —— 计时器就绪\n";
    }

    void shutdown() override
    {
        std::cout << "  [Timer] 已关闭\n";
    }
};

class ConfigPlugin : public IPlugin {
public:
    std::string name() const override { return "Config"; }
    std::string version() const override { return "0.9.5"; }

    void initialize() override
    {
        std::cout << "  [Config] v" << version()
                  << " 已初始化 —— 配置加载完成\n";
    }

    void shutdown() override
    {
        std::cout << "  [Config] 已关闭 —— 配置已保存\n";
    }
};

class PluginManager {
private:
    std::vector<std::unique_ptr<IPlugin>> plugins_;

public:
    void add(std::unique_ptr<IPlugin> plugin)
    {
        std::cout << "  注册插件: " << plugin->name()
                  << " v" << plugin->version() << "\n";
        plugins_.push_back(std::move(plugin));
    }

    void load_all()
    {
        std::cout << "--- 初始化所有插件 ---\n";
        for (const auto& p : plugins_) {
            p->initialize();
        }
        std::cout << "  共 " << plugins_.size()
                  << " 个插件已初始化\n";
    }

    void unload_all()
    {
        std::cout << "--- 关闭所有插件 ---\n";
        for (const auto& p : plugins_) {
            p->shutdown();
        }
        plugins_.clear();
        std::cout << "  所有插件已卸载\n";
    }

    void list() const
    {
        std::cout << "--- 已加载插件列表 ---\n";
        for (const auto& p : plugins_) {
            std::cout << "  - " << p->name()
                      << " v" << p->version() << "\n";
        }
    }

    std::size_t count() const { return plugins_.size(); }
};

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== 插件系统 =====\n\n";

    PluginManager mgr;

    // 注册插件
    mgr.add(std::make_unique<LoggerPlugin>());
    mgr.add(std::make_unique<TimerPlugin>());
    mgr.add(std::make_unique<ConfigPlugin>());

    std::cout << "\n";
    mgr.list();

    std::cout << "\n";
    mgr.load_all();

    std::cout << "\n";
    mgr.list();

    std::cout << "\n";
    mgr.unload_all();

    std::cout << "\n卸载后插件数: " << mgr.count() << "\n";

    return 0;
}
