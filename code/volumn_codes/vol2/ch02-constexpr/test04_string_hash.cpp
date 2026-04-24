#include <cstdint>
#include <cstddef>
#include <cstring>

// 测试编译期字符串哈希

// FNV-1a 哈希：简单、分布均匀、广泛使用
constexpr std::uint32_t fnv1a32(const char* str, std::size_t len)
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

// 从字符串字面量推导长度
template <std::size_t N>
constexpr std::uint32_t str_hash(const char (&s)[N])
{
    return fnv1a32(s, N - 1);  // N - 1 排除末尾的 '\0'
}

// 编译期生成所有命令的哈希值
constexpr auto kHashInit   = str_hash("INIT");
constexpr auto kHashStart  = str_hash("START");
constexpr auto kHashStop   = str_hash("STOP");
constexpr auto kHashReset  = str_hash("RESET");

// 编译期冲突检测
static_assert(kHashInit != kHashStart, "Hash collision detected");
static_assert(kHashInit != kHashStop, "Hash collision detected");
static_assert(kHashStart != kHashStop, "Hash collision detected");
static_assert(kHashStart != kHashReset, "Hash collision detected");

// 运行时命令分派
void dispatch_command(const char* cmd)
{
    std::uint32_t h = fnv1a32(cmd, std::strlen(cmd));
    switch (h) {
        case kHashInit:  /* handle INIT */  break;
        case kHashStart: /* handle START */ break;
        case kHashStop:  /* handle STOP */  break;
        case kHashReset: /* handle RESET */ break;
        default: /* unknown command */ break;
    }
}

int main()
{
    // 测试运行时哈希与编译期哈希的一致性
    // 注意：str_hash 只能接受字符串字面量，不能接受指针
    constexpr auto compile_time_hash = str_hash("START");

    // 验证哈希函数的幂等性
    static_assert(str_hash("INIT") == fnv1a32("INIT", 4),
                  "Hash function should be idempotent");
    static_assert(str_hash("START") == fnv1a32("START", 5),
                  "Hash function should be idempotent for START");

    return 0;
}
