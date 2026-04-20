// overload.cpp
// Platform: host
// Standard: C++17

#include <cstdint>
#include <cstdio>
#include <cstring>

// ---- 多个 print 重载 ----

void print(int value)
{
    std::printf("int:    %d\n", value);
}

void print(double value)
{
    std::printf("double: %.2f\n", value);
}

void print(const char* str)
{
    std::printf("string: %s\n", str);
}

// ---- 默认参数示例 ----

void draw_rect(int width, int height, bool fill = false,
               char brush = '#')
{
    std::printf("绘制矩形 %dx%d, fill=%s, brush='%c'\n",
                width, height,
                fill ? "true" : "false",
                brush);
}

// ---- 修复歧义：用不同的函数名替代混搭 ----

void scale_value(int value)
{
    std::printf("原始值: %d\n", value);
}

void scale_value(int value, int factor)
{
    std::printf("缩放后: %d (factor=%d)\n", value * factor, factor);
}

int main()
{
    // 演示重载
    std::printf("=== 函数重载 ===\n");
    print(42);
    print(3.14159);
    print("Hello, overloading!");

    // 演示默认参数
    std::printf("\n=== 默认参数 ===\n");
    draw_rect(10, 5);                  // fill=false, brush='#'
    draw_rect(10, 5, true);            // fill=true,  brush='#'
    draw_rect(10, 5, true, '*');       // 全部自定义

    // 演示修复后的"重载 + 不同参数数量"
    std::printf("\n=== 不同参数数量 ===\n");
    scale_value(7);
    scale_value(7, 3);

    return 0;
}
