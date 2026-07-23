#include <stddef.h>
#include <stdio.h>

size_t count_char(const char* str, char ch) {
    if (str == NULL) { // 警惕空指针
        return 0;
    }
    size_t count = 0;
    for (; *str; str++) {
        if (*str == ch) {
            count++;
        }
    }
    return count;
}

int main(void) {
    // 演示:统计字符在字符串中出现的次数
    const char* text = "mississippi";
    printf("'%s' 中 's' 出现了 %zu 次\n", text, count_char(text, 's'));
    printf("NULL 指针测试: %zu\n", count_char(NULL, 's'));
    return 0;
}
