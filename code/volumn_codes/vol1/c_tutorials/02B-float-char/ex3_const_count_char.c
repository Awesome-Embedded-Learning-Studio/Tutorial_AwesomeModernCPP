#include <stdio.h>
#include <stddef.h>

size_t count_char(const char* str, char ch)
{
    size_t count = 0;
    while (*str != '\0') {
        if (*str == ch) {
            count++;
        }
        str++;
    }
    return count;
}

int main(void)
{
    const char* text = "hello world, welcome to C programming";
    printf("'%s'\n", text);
    printf("count 'l': %zu\n", count_char(text, 'l'));
    printf("count 'o': %zu\n", count_char(text, 'o'));
    printf("count 'z': %zu\n", count_char(text, 'z'));
    return 0;
}
