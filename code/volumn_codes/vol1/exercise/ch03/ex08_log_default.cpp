/**
 * @file ex08_log_default.cpp
 * @brief 练习：带默认参数的日志函数
 *
 * 实现 log_message(const char* text, const char* level = "INFO",
 *                  bool show_timestamp = false)，
 * 演示默认参数的使用和各种调用方式。
 */

#include <ctime>
#include <iostream>
#include <string>

// 获取当前时间戳字符串
std::string get_timestamp() {
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&now));
    return buf;
}

// 带默认参数的日志函数
void log_message(const char* text, const char* level = "INFO",
                 bool show_timestamp = false) {
    if (show_timestamp) {
        std::cout << "[" << get_timestamp() << "] ";
    }
    std::cout << "[" << level << "] " << text << '\n';
}

int main() {
    // 只传文本，使用全部默认参数
    std::cout << "===== 1. 只传文本 =====\n";
    log_message("系统启动完成");

    // 传文本和日志级别
    std::cout << "\n===== 2. 传文本和级别 =====\n";
    log_message("配置文件未找到", "WARN");
    log_message("数据库连接失败", "ERROR");
    log_message("用户登录成功", "DEBUG");

    // 传全部参数
    std::cout << "\n===== 3. 传全部参数 =====\n";
    log_message("服务器启动", "INFO", true);
    log_message("内存不足警告", "WARN", true);
    log_message("磁盘空间不足", "ERROR", true);

    // 展示默认参数规则
    std::cout << "\n===== 默认参数规则 =====\n";
    std::cout << "1. 默认参数必须从右向左连续设置\n";
    std::cout << "2. 调用时实参从左向右匹配\n";
    std::cout << "3. 只能省略尾部参数，不能跳过中间参数\n";
    std::cout << "4. 默认参数只能出现在声明中（通常在头文件）\n";

    // 注意：不能跳过 level 只传 show_timestamp
    // log_message("test", true);  // 错误！true 会匹配 const char* level

    return 0;
}
