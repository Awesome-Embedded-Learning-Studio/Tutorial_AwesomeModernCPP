// 配套 06-designated-initializers.md「聚合类型要求 + 嵌套/位域/联合体」
// 指定初始化器只能用于聚合类型;嵌套、位域、联合体都支持,未指定成员零初始化
// 编译运行:g++ -std=c++20 -Wall -Wextra designated_aggregate.cpp -o da && ./da
// 复现非聚合报错:g++ -std=c++20 -Wall -Wextra -DNON_AGGREGATE designated_aggregate.cpp -o da_ng
#include <cstdint>
#include <cstdio>

struct Pin {
    std::uint8_t port;
    std::uint8_t pin;
};

// 聚合类型:嵌套结构体
struct UartCfg {
    std::uint32_t baud;
    Pin tx;
    Pin rx;
};

struct Flags {
    unsigned a : 1;
    unsigned b : 1;
    unsigned c : 6;
};

union Value {
    int i;
    float f;
};

#ifdef NON_AGGREGATE
struct WithCtor {
    int a;
    int b;
    WithCtor(int, int) {}
};
#endif

int main() {
    UartCfg u{
        .baud = 115200,
        .tx = {.port = 0, .pin = 9},
        .rx = {.port = 0, .pin = 10},
    };
    std::printf("uart: baud=%u tx=P%c%d rx=P%c%d\n", u.baud, 'A' + u.tx.port, u.tx.pin,
                'A' + u.rx.port, u.rx.pin);

    Flags f{.a = 1, .b = 0, .c = 5}; // 位域也能用指定初始化器
    std::printf("flags: a=%u b=%u c=%u\n", f.a, f.b, f.c);

    Value v{.f = 3.14f}; // 联合体:指定一个成员
    std::printf("union as float: %.2f\n", v.f);

#ifdef NON_AGGREGATE
    WithCtor x{.a = 1,
               .b = 2}; // 编译失败:designated initializers cannot be used with a non-aggregate type
    (void)x;
#endif
}
