/**
 * @file ex04_raw_to_smart.cpp
 * @brief 练习：转换裸指针为智能指针
 *
 * 将使用裸 new/delete 的程序改写为使用 std::unique_ptr / std::make_unique
 * 的版本，展示 before (注释) 和 after 的对比。
 */

#include <iostream>
#include <memory>
#include <string>

// ============================================================
// Logger 类定义
// ============================================================

class Logger {
public:
    explicit Logger(const std::string& name) : name_(name)
    {
        std::cout << "  Logger(" << name_ << ") 构造\n";
    }

    ~Logger() { std::cout << "  Logger(" << name_ << ") 析构\n"; }

    void log(const std::string& msg)
    {
        std::cout << "  [" << name_ << "] " << msg << "\n";
    }

    const std::string& name() const { return name_; }

private:
    std::string name_;
};

// ============================================================
// Sensor 类定义（演示 shared_ptr）
// ============================================================

class Sensor {
public:
    explicit Sensor(int id) : id_(id)
    {
        std::cout << "  Sensor(" << id_ << ") 构造\n";
    }

    ~Sensor() { std::cout << "  Sensor(" << id_ << ") 析构\n"; }

    int read() const
    {
        std::cout << "  Sensor(" << id_ << ") 读取数据\n";
        return id_ * 100;
    }

private:
    int id_;
};

// ============================================================
// 裸指针版本（有问题的原始代码，已注释）
// ============================================================

// void raw_pointer_version()
// {
//     Logger* logger = new Logger("app");
//     logger->log("程序启动");
//
//     Logger* backup = logger;  // 别名，不拥有
//     backup->log("这是备份日志");
//
//     delete logger;
//     // backup 此刻是悬空指针！
//     // 如果在 delete 之后使用 backup，就是 use-after-free
// }

// ============================================================
// 智能指针版本（修复后）
// ============================================================

void smart_pointer_version()
{
    std::cout << "--- unique_ptr 版本 (独占所有权) ---\n";

    // 用 make_unique 创建 Logger，无需手动 delete
    auto logger = std::make_unique<Logger>("app");
    logger->log("程序启动");

    // 如果需要不拥有的观察者，使用裸指针（不增加所有权语义）
    Logger* observer = logger.get();
    observer->log("这是观察者日志（不拥有）");

    // 不能拷贝 unique_ptr，但可以移动
    // auto backup = logger;               // 编译错误！unique_ptr 不可拷贝
    auto moved_logger = std::move(logger);  // OK：所有权转移
    moved_logger->log("所有权已转移");

    // 移动后 logger 变为 nullptr
    std::cout << "  移动后 logger 是否为空: "
              << (logger ? "否" : "是 (nullptr)") << "\n";

    // moved_logger 离开作用域时自动析构，无需手动 delete
    std::cout << "  (离开作用域时自动析构)\n";
}

void shared_pointer_version()
{
    std::cout << "\n--- shared_ptr 版本 (共享所有权) ---\n";

    auto sensor1 = std::make_shared<Sensor>(1);
    std::cout << "  引用计数: " << sensor1.use_count() << "\n";

    {
        // 多个 shared_ptr 共享同一个对象
        auto sensor2 = sensor1;
        std::cout << "  sensor2 加入后引用计数: " << sensor1.use_count() << "\n";

        int value = sensor2->read();
        std::cout << "  读取值: " << value << "\n";
    }
    // sensor2 离开作用域，引用计数减 1
    std::cout << "  sensor2 离开后引用计数: " << sensor1.use_count() << "\n";

    std::cout << "  (最后离开作用域时自动析构)\n";
}

void unique_ptr_array_version()
{
    std::cout << "\n--- unique_ptr 数组版本 ---\n";

    // 用 make_unique<int[]> 替代 new int[N] / delete[]
    constexpr int kSize = 5;
    auto arr = std::make_unique<int[]>(kSize);

    for (int i = 0; i < kSize; ++i) {
        arr[i] = i * i;
    }

    std::cout << "  数组内容: ";
    for (int i = 0; i < kSize; ++i) {
        std::cout << arr[i] << " ";
    }
    std::cout << "\n";

    // 自动调用 delete[]，无需手动释放
    std::cout << "  (离开作用域时自动 delete[])\n";
}

int main()
{
    std::cout << "===== ex04: 转换裸指针为智能指针 =====\n\n";

    std::cout << "有问题的裸指针代码 (已注释):\n";
    std::cout << "  Logger* logger = new Logger(\"app\");\n";
    std::cout << "  Logger* backup = logger;  // 悬空指针风险\n";
    std::cout << "  delete logger;\n";
    std::cout << "  // backup 此刻是悬空指针!\n\n";

    smart_pointer_version();
    shared_pointer_version();
    unique_ptr_array_version();

    std::cout << "\n要点:\n";
    std::cout << "  1. unique_ptr: 独占所有权，不可拷贝但可移动，零额外开销\n";
    std::cout << "  2. shared_ptr: 共享所有权，引用计数管理生命周期\n";
    std::cout << "  3. make_unique/make_shared 比直接 new 更安全\n";
    std::cout << "  4. 不需要所有权的观察者用裸指针 (.get()) 即可\n";
    std::cout << "  5. 智能指针在作用域结束时自动释放，根治内存泄漏\n";

    return 0;
}
