---
title: "三路比较运算符（C++20 Spaceship Operator）"
description: "C++20三路比较运算符详解：简化自定义类型的比较逻辑"
chapter: 11
order: 5
tags:
  - 三路比较
  - Spaceship Operator
  - C++20
  - 比较类别
difficulty: intermediate
reading_time_minutes: 30
prerequisites:
  - "Chapter 11.1: auto与decltype"
  - "Chapter 11.2: 结构化绑定"
cpp_standard: [20]
---

# 嵌入式现代C++开发——三路比较运算符

## 引言

你在写嵌入式代码的时候，有没有为比较运算符感到头疼？

```cpp
class SensorReading {
public:
    uint16_t sensor_id;
    int32_t value;
    uint32_t timestamp;

    // 需要实现6个比较运算符！
    bool operator==(const SensorReading& other) const {
        return sensor_id == other.sensor_id &&
               value == other.value &&
               timestamp == other.timestamp;
    }

    bool operator!=(const SensorReading& other) const {
        return !(*this == other);
    }

    bool operator<(const SensorReading& other) const {
        if (sensor_id != other.sensor_id)
            return sensor_id < other.sensor_id;
        if (value != other.value)
            return value < other.value;
        return timestamp < other.timestamp;
    }

    bool operator<=(const SensorReading& other) const {
        return *this < other || *this == other;
    }

    bool operator>(const SensorReading& other) const {
        return other < *this;
    }

    bool operator>=(const SensorReading& other) const {
        return !(*this < other);
    }
};
```

这简直是灾难！为了实现一个完整的可排序类型，你需要写6个比较运算符，而且它们之间还有复杂的依赖关系。更糟糕的是，如果修改了成员变量，得同步更新所有这些运算符。

C++20引入的**三路比较运算符**（Three-way Comparison Operator），俗称**宇宙飞船运算符**（Spaceship Operator `<=>`），就是为了解决这个问题。

> 一句话总结：**三路比较运算符用一次定义自动生成所有六个比较运算符，大幅简化自定义类型的比较逻辑。**

在嵌入式开发中，这个特性特别有用：

1. 传感器数据需要按时间、优先级排序
2. 固件版本号比较（带字母后缀的复杂版本）
3. 配置参数的字典序比较
4. 优先级队列的任务排序

------
**警告**：截至2024年，GCC 10+、Clang 10+、MSVC 2019+才完全支持三路比较运算符。如果你的编译器较老，可能需要升级或使用替代方案。

------

## 三路比较运算符基础语法

### 运算符符号

三路比较运算符使用`<=>`符号，因为看起来像宇宙飞船而得名：

```cpp
#include <compare>

struct Point {
    int x, y;

    // 三路比较运算符
    std::strong_ordering operator<=>(const Point& other) const {
        if (auto cmp = x <=> other.x; cmp != 0)
            return cmp;
        return y <=> other.y;
    }
};
```

### 返回值类型

三路比较运算符的返回值不是`bool`，而是表示比较结果的"比较类别"（comparison category）：

```cpp
// <=> 返回值可以理解为：
// a <=> b < 0  表示 a < b
// a <=> b == 0 表示 a == b
// a <=> b > 0  表示 a > b

// 实际上，它返回的是一个类型
auto result = (a <=> b);

if (result < 0) { /* a < b */ }
else if (result == 0) { /* a == b */ }
else { /* a > b */ }
```

### 比较结果的测试

返回的比较类别可以与0进行比较，或者使用命名的方法：

```cpp
#include <compare>

int main() {
    auto cmp = 5 <=> 3;

    // 方式1：与0比较
    if (cmp < 0)    std::cout << "less\n";
    if (cmp == 0)   std::cout << "equal\n";
    if (cmp > 0)    std::cout << "greater\n";

    // 方式2：使用命名方法（推荐，更清晰）
    if (cmp == std::strong_ordering::less)    std::cout << "less\n";
    if (cmp == std::strong_ordering::equal)   std::cout << "equal\n";
    if (cmp == std::strong_ordering::greater) std::cout << "greater\n";

    return 0;
}
```

------
**最佳实践**：直接使用`<`、`==`、`>`与比较结果进行判断，而不是调用命名方法。这样代码更简洁，而且适用于所有比较类别。

------

## 自动生成比较函数

### 使用=default自动生成

最简单的用法是使用`= default`让编译器自动生成所有比较运算符：

```cpp
#include <compare>

struct SensorReading {
    uint16_t sensor_id;
    int32_t value;
    uint32_t timestamp;

    // 一行代码搞定所有6个比较运算符！
    auto operator<=>(const SensorReading&) const = default;

    // C++20还会自动生成!=
    // 但==仍需显式default（如果需要）
    bool operator==(const SensorReading&) const = default;
};
```

现在你可以使用所有比较运算符：

```cpp
SensorReading s1{1, 100, 1000};
SensorReading s2{1, 100, 1000};
SensorReading s3{2, 100, 1000};

// 所有这些都可以工作！
bool b1 = (s1 == s2);  // true
bool b2 = (s1 != s2);  // false
bool b3 = (s1 < s3);   // true (按字典序)
bool b4 = (s1 <= s3);  // true
bool b5 = (s1 > s3);   // false
bool b6 = (s1 >= s3);  // false

// 还可以用在标准容器中
std::set<SensorReading> sensor_set;
std::map<SensorReading, std::string> sensor_map;

// 还可以用在算法中
std::vector<SensorReading> sensors;
std::sort(sensors.begin(), sensors.end());
```

### 比较顺序

默认生成的`<=>`按照**成员声明顺序**进行字典序比较：

```cpp
struct Version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;

    auto operator<=>(const Version&) const = default;
    bool operator==(const Version&) const = default;
};

Version v1{1, 2, 3};
Version v2{1, 2, 4};
Version v3{1, 3, 0};

// 比较顺序：major -> minor -> patch
// v1 < v2 (patch: 3 < 4)
// v1 < v3 (minor: 2 < 3)
// v2 < v3 (minor: 2 < 3)
```

------
**注意**：成员变量的顺序很重要！如果你希望按某个特定顺序比较，需要调整成员变量的声明顺序。

------

## 比较类别详解

C++20定义了三种比较类别，用于表示不同强度的比较关系。

### strong_ordering：强序

`strong_ordering`表示最强的比较关系，具有以下性质：

1. **等价即相等**：`a == b`当且仅当`a`和`b`的所有成员都相等
2. **可替换性**：`a == b`时，`f(a) == f(b)`对任何函数`f`都成立

适用场景：整数、字符串、简单的值类型

```cpp
#include <compare>
#include <string>

struct Integer {
    int value;

    std::strong_ordering operator<=>(const Integer& other) const {
        return value <=> other.value;
    }

    bool operator==(const Integer& other) const = default;
};

// 使用
Integer a{5}, b{5}, c{10};
static_assert((a <=> b) == std::strong_ordering::equal);
static_assert((a <=> c) == std::strong_ordering::less);
static_assert((c <=> a) == std::strong_ordering::greater);
```

`std::strong_ordering`有三个可能的值：

| 值 | 含义 |
|-----|------|
| `std::strong_ordering::less` | 小于 |
| `std::strong_ordering::equal` | 等于 |
| `std::strong_ordering::greater` | 大于 |
| `std::strong_ordering::equivalent` | 等价（对于强序，等同于equal） |

### partial_ordering：偏序

`partial_ordering`表示可能存在"不可比较"的情况：

1. 某些值之间可能无法比较（如`NaN`）
2. 等价并不意味着相等

适用场景：浮点数（存在`NaN`）、带允许值的范围

```cpp
#include <compare>
#include <cmath>

struct FloatValue {
    float value;

    std::partial_ordering operator<=>(const FloatValue& other) const {
        if (std::isnan(value) || std::isnan(other.value))
            return std::partial_ordering::unordered;
        return value <=> other.value;
    }

    bool operator==(const FloatValue& other) const {
        return value == other.value;
    }
};

// 使用
FloatValue a{1.0f}, b{2.0f}, c{NAN};

static_assert((a <=> b) == std::partial_ordering::less);
// (a <=> c) == std::partial_ordering::unordered
```

`std::partial_ordering`有四个可能的值：

| 值 | 含义 |
|-----|------|
| `std::partial_ordering::less` | 小于 |
| `std::partial_ordering::equivalent` | 等价 |
| `std::partial_ordering::greater` | 大于 |
| `std::partial_ordering::unordered` | 不可比较 |

### weak_ordering：弱序

`weak_ordering`介于强序和偏序之间：

1. 等价不意味着相等（可能有不可区分的替代表示）
2. 但所有值都是可比较的（不存在`unordered`）

适用场景：大小写不敏感的字符串、忽略某些字段的比较

```cpp
#include <compare>
#include <string>
#include <cctype>

struct CaseInsensitiveString {
    std::string value;

    // 辅助函数：大小写不敏感比较
    static int compare_ic(const std::string& a, const std::string& b) {
        size_t i = 0;
        while (i < a.size() && i < b.size()) {
            int ca = std::tolower(static_cast<unsigned char>(a[i]));
            int cb = std::tolower(static_cast<unsigned char>(b[i]));
            if (ca != cb)
                return ca - cb;
            ++i;
        }
        if (a.size() < b.size()) return -1;
        if (a.size() > b.size()) return 1;
        return 0;
    }

    std::weak_ordering operator<=>(const CaseInsensitiveString& other) const {
        int cmp = compare_ic(value, other.value);
        if (cmp < 0) return std::weak_ordering::less;
        if (cmp > 0) return std::weak_ordering::greater;
        return std::weak_ordering::equivalent;
    }

    bool operator==(const CaseInsensitiveString& other) const {
        return compare_ic(value, other.value) == 0;
    }
};

// 使用
CaseInsensitiveString s1{"Hello"}, s2{"HELLO"}, s3{"hello"}, s4{"World"};

// s1, s2, s3 是等价的（weak_ordering::equivalent）
// 但它们不相等（value不同）
static_assert((s1 <=> s2) == std::weak_ordering::equivalent);
static_assert(!(s1 == s2));  // 不相等！
```

`std::weak_ordering`有三个可能的值：

| 值 | 含义 |
|-----|------|
| `std::weak_ordering::less` | 小于 |
| `std::weak_ordering::equivalent` | 等价 |
| `std::weak_ordering::greater` | 大于 |

### 三种比较类别的选择

```cpp
#include <compare>

// 选择指南

// 1. strong_ordering：所有字段都精确比较
struct SensorData {
    uint8_t id;
    int16_t value;

    auto operator<=>(const SensorData&) const = default;
    bool operator==(const SensorData&) const = default;
    // 返回 strong_ordering
};

// 2. partial_ordering：存在NaN或不可比较的值
struct Measurement {
    float value;  // 可能是NaN

    std::partial_ordering operator<=>(const Measurement& other) const {
        if (std::isnan(value) || std::isnan(other.value))
            return std::partial_ordering::unordered;
        return value <=> other.value;
    }
};

// 3. weak_ordering：等价但不相等
struct ConfigKey {
    std::string key;
    bool case_sensitive;

    std::weak_ordering operator<=>(const ConfigKey& other) const {
        if (!case_sensitive) {
            // 大小写不敏感比较
            return case_insensitive_compare(key, other.key);
        }
        return key <=> other.key;
    }
};
```

### 比较类别关系图

```text
strong_ordering (最强)
  ├─ 替换性：a == b 意味着 a 可以完全替代 b
  ├─ 等价即相等
  └─ 例子：整数、枚举

weak_ordering
  ├─ 替换性：a == b 不一定能完全替代 b
  ├─ 等价但不相等
  └─ 例子：大小写不敏感字符串

partial_ordering (最弱)
  ├─ 某些值不可比较
  └─ 例子：浮点数（NaN）
```

------
**重要**：当使用`= default`时，编译器会根据成员类型自动选择最合适的比较类别。如果所有成员都支持`strong_ordering`，生成的就是`strong_ordering`。

------

## 嵌入式场景实战

### 场景1：传感器数据优先级排序

在嵌入式系统中，传感器数据通常需要按优先级和时间戳排序：

```cpp
#include <compare>
#include <cstdint>
#include <queue>

class SensorMessage {
public:
    enum class Priority : uint8_t {
        Critical = 0,
        High = 1,
        Normal = 2,
        Low = 3
    };

    uint16_t sensor_id;
    Priority priority;
    int32_t value;
    uint32_t sequence;  // 序列号，用于同优先级排序

    // 按优先级升序（Critical在队列前面），然后按序列号
    auto operator<=>(const SensorMessage& other) const {
        // 优先级越小越重要
        if (auto cmp = priority <=> other.priority; cmp != 0)
            return cmp;
        // 同优先级按序列号（FIFO）
        return sequence <=> other.sequence;
    }

    bool operator==(const SensorMessage& other) const {
        return sensor_id == other.sensor_id &&
               priority == other.priority &&
               value == other.value &&
               sequence == other.sequence;
    }

    // 用于优先级队列（需要>运算符）
    bool operator>(const SensorMessage& other) const {
        return (*this <=> other) > 0;
    }
};

// 使用示例
void message_queue_example() {
    // 小顶堆（Priority值越小优先级越高）
    std::priority_queue<
        SensorMessage,
        std::vector<SensorMessage>,
        std::greater<>
    > message_queue;

    message_queue.push(SensorMessage{1, SensorMessage::Priority::Low, 100, 1});
    message_queue.push(SensorMessage{2, SensorMessage::Priority::Critical, 200, 2});
    message_queue.push(SensorMessage{3, SensorMessage::Priority::High, 150, 3});

    // 按优先级顺序处理：Critical -> High -> Low
    while (!message_queue.empty()) {
        auto msg = message_queue.top();
        process_message(msg);
        message_queue.pop();
    }
}
```

### 场景2：固件版本号比较

固件版本号可能有复杂的格式，如带字母后缀：

```cpp
#include <compare>
#include <string>
#include <variant>

class FirmwareVersion {
public:
    uint8_t major;
    uint8_t minor;
    uint8_t patch;

    // 预发布标识：alpha < beta < rc < 正式版
    enum class PreRelease : uint8_t {
        None = 0,
        Alpha = 1,
        Beta = 2,
        RC = 3
    };

    PreRelease pre_release = PreRelease::None;
    uint8_t pre_release_version = 0;  // alpha1, alpha2等

    // 比较版本号
    std::strong_ordering operator<=>(const FirmwareVersion& other) const {
        // 主版本号
        if (auto cmp = major <=> other.major; cmp != 0)
            return cmp;

        // 次版本号
        if (auto cmp = minor <=> other.minor; cmp != 0)
            return cmp;

        // 补丁版本号
        if (auto cmp = patch <=> other.patch; cmp != 0)
            return cmp;

        // 预发布标识
        if (auto cmp = pre_release <=> other.pre_release; cmp != 0)
            return cmp;

        // 预发布版本号（只在都是预发布时比较）
        if (pre_release != PreRelease::None) {
            return pre_release_version <=> other.pre_release_version;
        }

        return std::strong_ordering::equal;
    }

    bool operator==(const FirmwareVersion& other) const = default;

    // 解析版本字符串 "1.2.3-beta2"
    static FirmwareVersion parse(const std::string& version_str);

    std::string to_string() const;
};

// 使用示例
void version_comparison() {
    FirmwareVersion current{1, 2, 3};
    FirmwareVersion available{1, 2, 4};

    if (available > current) {
        printf("New version available: %s\n",
               available.to_string().c_str());
    }

    // 预发布版本比较
    FirmwareVersion v1{2, 0, 0, FirmwareVersion::PreRelease::Alpha, 1};
    FirmwareVersion v2{2, 0, 0, FirmwareVersion::PreRelease::Beta, 1};
    FirmwareVersion v3{2, 0, 0, FirmwareVersion::PreRelease::None, 0};

    static_assert(v1 < v2);  // alpha < beta
    static_assert(v2 < v3);  // beta < 正式版
}
```

### 场景3：配置参数比较（允许部分相等）

在配置系统中，我们可能只想比较某些关键字段：

```cpp
#include <compare>
#include <string>
#include <optional>

struct NetworkConfig {
    std::string ssid;
    std::optional<std::string> password;  // 密码不参与比较
    uint8_t channel;
    bool hidden;

    // 比较时忽略密码字段
    auto operator<=>(const NetworkConfig& other) const {
        if (auto cmp = ssid <=> other.ssid; cmp != 0)
            return cmp;
        if (auto cmp = channel <=> other.channel; cmp != 0)
            return cmp;
        return hidden <=> other.hidden;
    }

    bool operator==(const NetworkConfig& other) const {
        return ssid == other.ssid &&
               channel == other.channel &&
               hidden == other.hidden;
        // 注意：password不参与比较
    }

    // 完全比较（包括密码）
    bool fully_equal(const NetworkConfig& other) const {
        if (*this != other) return false;
        if (password.has_value() != other.password.has_value())
            return false;
        if (password.has_value() && *password != *other.password)
            return false;
        return true;
    }
};

// 使用示例
void config_example() {
    NetworkConfig config1{"MyWiFi", "password123", 6, false};
    NetworkConfig config2{"MyWiFi", "different", 6, false};

    // 两个配置"相等"（忽略密码）
    static_assert(config1 == config2);

    // 可以检测配置是否改变
    NetworkConfig saved_config = load_from_flash();
    NetworkConfig current_config = get_current_config();

    if (current_config != saved_config) {
        printf("Configuration changed, need to save\n");
        save_to_flash(current_config);
    }

    // 但检查密码是否改变需要显式调用
    if (!config1.fully_equal(config2)) {
        printf("Password changed\n");
    }
}
```

### 场景4：带NaN的传感器数据

某些传感器可能返回无效数据（类似NaN的概念）：

```cpp
#include <compare>
#include <optional>
#include <cmath>

struct SensorValue {
    std::optional<float> value;

    // 无效值（无值）被视为小于任何有效值
    std::partial_ordering operator<=>(const SensorValue& other) const {
        if (!value.has_value() && !other.value.has_value())
            return std::partial_ordering::equivalent;
        if (!value.has_value())
            return std::partial_ordering::less;
        if (!other.value.has_value())
            return std::partial_ordering::greater;

        // 两个都有值
        float v1 = *value;
        float v2 = *other.value;

        if (std::isnan(v1) || std::isnan(v2))
            return std::partial_ordering::unordered;

        if (v1 < v2) return std::partial_ordering::less;
        if (v1 > v2) return std::partial_ordering::greater;
        return std::partial_ordering::equivalent;
    }

    bool operator==(const SensorValue& other) const {
        if (!value.has_value() && !other.value.has_value())
            return true;
        if (!value.has_value() || !other.value.has_value())
            return false;
        return *value == *other.value;
    }
};

// 使用示例
void sensor_with_invalid_values() {
    std::vector<SensorValue> readings = {
        {10.5f},
        {std::nullopt},  // 无效读数
        {15.2f},
        {NAN},           // NaN读数
        {12.0f}
    };

    // 排序：无效值在前，然后NaN，然后有效值
    std::sort(readings.begin(), readings.end());

    for (const auto& reading : readings) {
        if (reading.value) {
            printf("%.1f ", *reading.value);
        } else {
            printf("(invalid) ");
        }
    }
    // 输出：(invalid) nan 10.5 12.0 15.2
}
```

### 场景5：多级传感器告警

告警系统需要按多个维度排序：

```cpp
#include <compare>
#include <string>
#include <chrono>

class Alarm {
public:
    enum class Severity : uint8_t {
        Info = 0,
        Warning = 1,
        Error = 2,
        Critical = 3
    };

    enum class Status : uint8_t {
        Active = 0,
        Acknowledged = 1,
        Resolved = 2
    };

    uint32_t id;
    Severity severity;
    Status status;
    std::chrono::system_clock::time_point timestamp;
    std::string message;

    // 比较逻辑：
    // 1. Active告警优先
    // 2. 同状态下，Critical优先
    // 3. 同严重程度，最新的优先
    std::strong_ordering operator<=>(const Alarm& other) const {
        // 状态：Active < Acknowledged < Resolved
        if (auto cmp = status <=> other.status; cmp != 0)
            return cmp;

        // 严重程度：Critical > Error > Warning > Info
        // 但我们希望Critical在前面（更"小"）
        if (auto cmp = other.severity <=> severity; cmp != 0)
            return cmp;

        // 时间戳：最新的在前（更"小"）
        return other.timestamp <=> timestamp;
    }

    bool operator==(const Alarm& other) const {
        return id == other.id;
    }
};

// 使用示例
void alarm_system() {
    std::vector<Alarm> alarms = {
        {1, Alarm::Severity::Warning, Alarm::Status::Active,
         std::chrono::system_clock::now(), "Temperature high"},
        {2, Alarm::Severity::Critical, Alarm::Status::Acknowledged,
         std::chrono::system_clock::now(), "Power failure"},
        {3, Alarm::Severity::Error, Alarm::Status::Active,
         std::chrono::system_clock::now(), "Connection lost"}
    };

    // 排序后：
    // 1. Active Error (最新的Active告警)
    // 2. Active Warning
    // 3. Acknowledged Critical
    std::sort(alarms.begin(), alarms.end());

    for (const auto& alarm : alarms) {
        printf("[%d] %s: %s\n",
               static_cast<int>(alarm.severity),
               alarm.status == Alarm::Status::Active ? "Active" : "Acked",
               alarm.message.c_str());
    }
}
```

------

## 自定义三路比较实现

### 手动实现多字段比较

当默认的字典序不满足需求时，需要手动实现：

```cpp
#include <compare>

struct Task {
    uint8_t priority;      // 0-255，越小越重要
    uint32_t deadline;     // 截止时间戳
    uint32_t created_at;   // 创建时间戳
    uint16_t task_id;

    // 比较逻辑：
    // 1. 优先级最高的先执行
    // 2. 同优先级，deadline最近的先执行
    // 3. 同deadline，创建最早的先执行
    // 4. 都相同，task_id小的先执行
    std::strong_ordering operator<=>(const Task& other) const {
        // 优先级升序
        if (auto cmp = priority <=> other.priority; cmp != 0)
            return cmp;

        // deadline升序
        if (auto cmp = deadline <=> other.deadline; cmp != 0)
            return cmp;

        // 创建时间升序（早创建的优先）
        if (auto cmp = created_at <=> other.created_at; cmp != 0)
            return cmp;

        // task_id升序
        return task_id <=> other.task_id;
    }

    bool operator==(const Task& other) const = default;
};
```

### 使用比较合成助手

C++23提供了`std::compare_*`系列函数简化比较逻辑：

```cpp
#include <compare>

// C++23风格的比较合成
struct Task {
    uint8_t priority;
    uint32_t deadline;
    uint32_t created_at;
    uint16_t task_id;

    std::strong_ordering operator<=>(const Task& other) const {
        // 使用C++23的合成函数（如果可用）
        return std::compare_three_way()(
            priority, other.priority,
            deadline, other.deadline,
            created_at, other.created_at,
            task_id, other.task_id
        );
    }

    bool operator==(const Task& other) const = default;
};
```

对于C++20，可以自己实现简单的助手：

```cpp
// C++20比较合成助手
namespace detail {
    template<typename... Ts>
    constexpr auto synthesized_three_way(const Ts&... args) {
        using R = std::common_comparison_category_t<
            typename std::decay_t<decltype(args <=> std::declval<Ts>())>::comparison_category...>;
        return R{};
    }

    // 简单实现
    template<typename T>
    constexpr auto compare_fields(const T& a, const T& b) {
        return a <=> b;
    }

    template<typename T, typename U, typename... Rest>
    constexpr auto compare_fields(const T& a, const T& b,
                                  const U& ua, const U& ub,
                                  const Rest&... rest) {
        if (auto cmp = a <=> b; cmp != 0)
            return cmp;
        return compare_fields(ua, ub, rest...);
    }
}

struct Task {
    uint8_t priority;
    uint32_t deadline;
    uint32_t created_at;
    uint16_t task_id;

    std::strong_ordering operator<=>(const Task& other) const {
        return detail::compare_fields(
            priority, other.priority,
            deadline, other.deadline,
            created_at, other.created_at,
            task_id, other.task_id
        );
    }

    bool operator==(const Task& other) const = default;
};
```

------
**注意**：C++23提供了更强大的比较合成工具，如`std::compare_three_way`和`std::compare_*_result`，使用时请查阅最新标准库文档。

------

## 常见的坑

### 坑1：忘记显式定义==

在C++20中，`<=>`不会自动生成`==`运算符，必须显式定义：

```cpp
// ❌ 错误：只有<=>，没有==
struct Bad {
    int value;
    auto operator<=>(const Bad&) const = default;
    // 缺少 bool operator==(const Bad&) const = default;
};

Bad b1{1}, b2{1};
// bool eq = (b1 == b2);  // 编译错误！

// ✅ 正确：同时定义<=>和==
struct Good {
    int value;
    auto operator<=>(const Good&) const = default;
    bool operator==(const Good&) const = default;
};

Good g1{1}, g2{1};
bool eq = (g1 == g2);  // OK
```

### 坑2：比较类别不一致

手动实现时，返回的比较类别要一致：

```cpp
// ❌ 错误：混合不同的比较类别
struct BadCompare {
    float f;
    int i;

    std::partial_ordering operator<=>(const BadCompare& other) const {
        // float <=> float 返回 partial_ordering
        // int <=> int 返回 strong_ordering
        // 不能直接组合！
        if (f <=> other.f != std::partial_ordering::equivalent)
            return f <=> other.f;
        return i <=> other.i;  // 类型不匹配
    }
};

// ✅ 正确：统一返回类型
struct GoodCompare {
    float f;
    int i;

    std::partial_ordering operator<=>(const GoodCompare& other) const {
        if (auto cmp = f <=> other.f;
            cmp != std::partial_ordering::equivalent)
            return cmp;
        // strong_ordering可以隐式转换为partial_ordering
        return i <=> other.i;
    }
};

// ✅ 或者使用通用比较类别
struct BetterCompare {
    float f;
    int i;

    auto operator<=>(const BetterCompare& other) const {
        // 使用auto推导合适的比较类别
        if (auto cmp = f <=> other.f; cmp != 0)
            return cmp;
        return i <=> other.i;
    }
};
```

### 坑3：继承体系中的比较

在继承体系中使用`= default`需要小心：

```cpp
struct Base {
    int x;
    auto operator<=>(const Base&) const = default;
    bool operator==(const Base&) const = default;
};

// ✅ 如果派生类没有新增数据成员
struct Derived : Base {
    // 继承的比较运算符仍然有效
};

// ❌ 如果派生类新增了数据成员
struct DerivedWithNew : Base {
    int y;
    // 需要重新定义比较运算符
    auto operator<=>(const DerivedWithNew&) const = default;
    bool operator==(const DerivedWithNew&) const = default;
};

// ⚠️ 比较不同类型
Derived d1{1};
DerivedWithNew d2{1, 2};
// bool cmp = (d1 == d2);  // 编译错误！类型不同
```

### 坑4：浮点数的NaN问题

浮点数的`NaN`（Not a Number）会导致比较结果为`unordered`：

```cpp
#include <cmath>

float nan_value = std::nan("1");

// ❌ 传统比较运算符的问题
if (nan_value > 0.0f) { /* 不会执行 */ }
if (nan_value < 0.0f) { /* 不会执行 */ }
if (nan_value == 0.0f) { /* 不会执行 */ }
// NaN与任何浮点数比较都是false！

// ✅ 使用partial_ordering处理NaN
struct SafeFloat {
    float value;

    std::partial_ordering operator<=>(const SafeFloat& other) const {
        if (std::isnan(value) || std::isnan(other.value))
            return std::partial_ordering::unordered;
        return value <=> other.value;
    }

    bool operator==(const SafeFloat& other) const {
        if (std::isnan(value) || std::isnan(other.value))
            return false;
        return value == other.value;
    }
};
```

### 坑5：编译器支持问题

三路比较运算符需要较新的编译器：

```cpp
// 检查编译器支持
#if __cplusplus < 202002L
    #error "Three-way comparison requires C++20"
#endif

#if defined(__GNUC__) && __GNUC__ < 10
    #error "GCC 10 or later required for three-way comparison"
#endif

#if defined(__clang__) && __clang_major__ < 10
    #error "Clang 10 or later required for three-way comparison"
#endif

#if defined(_MSC_VER) && _MSC_VER < 1920
    #error "MSVC 2019 or later required for three-way comparison"
#endif
```

对于需要支持老编译器的项目，可以使用宏进行条件编译：

```cpp
#if __cpp_spaceship  // 或者 __cplusplus >= 202002L
    // 使用三路比较运算符
    #define ENABLE_SPACESHIP 1
#else
    // 回退到传统方法
    #define ENABLE_SPACESHIP 0
#endif

#if ENABLE_SPACESHIP
    struct ModernCompare {
        int value;
        auto operator<=>(const ModernCompare&) const = default;
        bool operator==(const ModernCompare&) const = default;
    };
#else
    struct LegacyCompare {
        int value;
        bool operator==(const LegacyCompare& other) const {
            return value == other.value;
        }
        bool operator!=(const LegacyCompare& other) const {
            return !(*this == other);
        }
        bool operator<(const LegacyCompare& other) const {
            return value < other.value;
        }
        bool operator<=(const LegacyCompare& other) const {
            return value <= other.value;
        }
        bool operator>(const LegacyCompare& other) const {
            return value > other.value;
        }
        bool operator>=(const LegacyCompare& other) const {
            return value >= other.value;
        }
    };
#endif
```

------

## C++20相关更新

### 常用比较运算符的重写

C++20允许编译器基于`<=>`自动重写某些比较运算符：

```cpp
struct X {
    // 只要定义了<=>和==
    auto operator<=>(const X&) const = default;
    bool operator==(const X&) const = default;
};

X x1, x2;

// 以下表达式自动重写为：
x1 != x2;  // !(x1 == x2)
x1 < x2;   // (x1 <=> x2) < 0
x1 <= x2;  // (x1 <=> x2) <= 0
x1 > x2;   // (x1 <=> x2) > 0
x1 >= x2;  // (x1 <=> x2) >= 0
```

### 与std::算法的集成

三路比较运算符可以无缝配合标准算法使用：

```cpp
#include <algorithm>
#include <vector>

struct Data {
    int key;
    std::string value;

    auto operator<=>(const Data&) const = default;
    bool operator==(const Data&) const = default;
};

void algorithm_example() {
    std::vector<Data> data = {
        {3, "three"}, {1, "one"}, {2, "two"}
    };

    // 排序
    std::sort(data.begin(), data.end());

    // 二分查找
    auto it = std::lower_bound(data.begin(), data.end(), Data{2, ""});
    if (it != data.end() && it->key == 2) {
        printf("Found: %s\n", it->value.c_str());
    }

    // 去重
    std::sort(data.begin(), data.end());
    auto last = std::unique(data.begin(), data.end());

    // 最小/最大
    auto [min_it, max_it] = std::minmax_element(data.begin(), data.end());
}
```

### 关联容器的键类型

默认生成的`<=>`使类型可以作为关联容器的键：

```cpp
#include <map>
#include <set>

struct ConfigKey {
    std::string section;
    std::string key;

    auto operator<=>(const ConfigKey&) const = default;
    bool operator==(const ConfigKey&) const = default;
};

// 可以直接用作map的键
std::map<ConfigKey, std::string> config = {
    {{"Network", "IP"}, "192.168.1.1"},
    {{"Network", "Port"}, "8080"},
    {{"Sensor", "Rate"}, "1000"}
};

// 可以直接用作set的元素
std::set<ConfigKey> keys;
keys.insert({"Network", "IP"});
```

------
**注意**：C++20之前，关联容器使用`std::less`（要求`operator<`）。C++20引入了`std::compare_three_way`，可以使用`<=>`进行比较。但为了兼容性，大多数实现仍然使用`operator<`。

------

我们回过头来再看看：三路比较运算符是C++20引入的重要特性，大幅简化了自定义类型的比较逻辑：

**核心概念**：

| 概念 | 说明 |
|-----|------|
| `<=>` 运算符 | 三路比较运算符，一次定义自动生成所有六个比较运算符 |
| 比较类别 | `strong_ordering`、`weak_ordering`、`partial_ordering` |
| `= default` | 让编译器自动生成比较逻辑 |
| 比较顺序 | 默认按成员声明顺序的字典序比较 |

**比较类别选择**：

| 类别 | 特点 | 使用场景 |
|-----|------|---------|
| `strong_ordering` | 等价即相等 | 整数、枚举、简单值类型 |
| `weak_ordering` | 等价但不相等 | 大小写不敏感字符串、忽略部分字段比较 |
| `partial_ordering` | 可能不可比较 | 浮点数（NaN） |

三路比较运算符让C++的比较逻辑更加简洁和安全，配合前面学过的auto、结构化绑定、属性等特性，现代C++已经发展成一门既强大又表达力丰富的系统编程语言。在嵌入式开发中，合理使用这些特性可以让代码更清晰、更易维护。
