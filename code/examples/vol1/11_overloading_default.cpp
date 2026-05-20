// 11_overloading_default.cpp
// 演示函数重载与默认参数的完整示例

#include <cstdio>
#include <cstring>

void print(int value) {
    std::printf("int:    %d\n", value);
}

void print(double value) {
    std::printf("double: %.2f\n", value);
}

void print(const char* str) {
    std::printf("string: %s\n", str);
}

void draw_rect(int width, int height, bool fill = false, char brush = '#') {
    std::printf("draw rect %dx%d, fill=%s, brush='%c'\n", width, height, fill ? "true" : "false",
                brush);
}

void scale_value(int value) {
    std::printf("raw: %d\n", value);
}

void scale_value(int value, int factor) {
    std::printf("scaled: %d (factor=%d)\n", value * factor, factor);
}

int main() {
    std::printf("=== Function Overloading ===\n");
    print(42);
    print(3.14159);
    print("Hello, overloading!");

    std::printf("\n=== Default Arguments ===\n");
    draw_rect(10, 5);
    draw_rect(10, 5, true);
    draw_rect(10, 5, true, '*');

    std::printf("\n=== Different Parameter Count ===\n");
    scale_value(7);
    scale_value(7, 3);

    return 0;
}
