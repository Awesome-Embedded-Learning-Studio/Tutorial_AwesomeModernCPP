/**
 * 验证 01-enum-class.md 中的技术断言
 * 编译: g++ -std=c++17 -Wall -Wextra -Wswitch -Wswitch-enum $this -o /tmp/verify
 */

#include <cstdio>
#include <cstdint>

// 验证点1: enum class 禁止隐式转换
enum class Color { Red, Green, Blue };
enum class Fruit { Apple, Orange, Banana };

void test_implicit_conversion()
{
    // Color c = Color::Red;
    // int x = c;  // 编译错误！不能隐式转换
    // int y = static_cast<int>(c);  // OK，必须显式转换

    printf("✓ enum class 禁止隐式转换（需手动验证编译错误）\n");
}

// 验证点2: 指定底层类型与前向声明
enum class Status : uint8_t;

class Device {
public:
    Status get_status() const;
    void set_status(Status s);
};

enum class Status : uint8_t {
    kOk = 0,
    kError = 1,
    kBusy = 2
};

Status Device::get_status() const { return Status::kOk; }
void Device::set_status(Status s) { (void)s; }

void test_forward_declaration()
{
    Device d;
    d.set_status(Status::kOk);
    printf("✓ enum class 支持前向声明\n");
}

// 验证点3: 大小由底层类型决定
enum class SensorState : uint8_t {
    kOff = 0,
    kInit = 1,
    kReady = 2,
    kError = 3
};

void test_sizeof()
{
    static_assert(sizeof(SensorState) == 1, "SensorState should be 1 byte");
    printf("sizeof(SensorState with uint8_t): %zu byte(s)\n", sizeof(SensorState));

    enum class DefaultEnum { A, B, C };
    printf("sizeof(DefaultEnum without underlying type): %zu byte(s) [default: int]\n",
           sizeof(DefaultEnum));
}

// 验证点4: switch 警告
enum class NetworkState : uint8_t {
    kDisconnected,
    kConnecting,
    kConnected,
    kError
};

const char* to_string_complete(NetworkState state)
{
    switch (state) {
    case NetworkState::kDisconnected: return "disconnected";
    case NetworkState::kConnecting:   return "connecting";
    case NetworkState::kConnected:    return "connected";
    case NetworkState::kError:        return "error";
    }
    return "unknown";
}

// 故意缺少分支来触发警告
const char* to_string_incomplete(NetworkState state)
{
    switch (state) {
    case NetworkState::kDisconnected: return "disconnected";
    case NetworkState::kConnecting:   return "connecting";
    case NetworkState::kConnected:    return "connected";
    // 缺少 kError - 编译器应发出 -Wswitch 警告
    }
    return "unknown";
}

void test_switch_warnings()
{
    printf("✓ switch 完整匹配: %s\n", to_string_complete(NetworkState::kError));
    printf("⚠ switch 缺少分支应触发 -Wswitch 警告（需手动验证）\n");
    (void)to_string_incomplete(NetworkState::kError);
}

int main()
{
    printf("=== 01-enum-class.md 技术断言验证 ===\n\n");

    test_implicit_conversion();
    printf("\n");

    test_forward_declaration();
    printf("\n");

    test_sizeof();
    printf("\n");

    test_switch_warnings();
    printf("\n");

    printf("=== 所有运行时验证通过 ===\n");
    printf("⚠ 编译期警告验证：请用 -Wswitch -Wswitch-enum 编译此文件\n");

    return 0;
}
