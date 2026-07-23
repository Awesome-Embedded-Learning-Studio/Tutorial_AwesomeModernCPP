#include <stdint.h>
#include <stdio.h>
// #include <stddef.h>

int main() {
    // 基本类型
    printf("sizeof(char)      = %zu bytes\n", sizeof(char));
    printf("sizeof(short)     = %zu bytes\n", sizeof(short));
    printf("sizeof(int)       = %zu bytes\n", sizeof(int));
    printf("sizeof(long)      = %zu bytes\n", sizeof(long));
    printf("sizeof(long long) = %zu bytes\n", sizeof(long long));

    // 定长整数类型 (需包含 <stdint.h>)
    printf("sizeof(int8_t)    = %zu bytes\n", sizeof(int8_t));
    printf("sizeof(uint8_t)   = %zu bytes\n", sizeof(uint8_t));
    printf("sizeof(int32_t)   = %zu bytes\n", sizeof(int32_t));
    printf("sizeof(uint32_t)  = %zu bytes\n", sizeof(uint32_t));
    printf("sizeof(int64_t)   = %zu bytes\n", sizeof(int64_t));

    // size_t 类型 (需包含<stdio.h>、 <stddef.h> 或 <stdlib.h>)
    printf("sizeof(size_t)    = %zu bytes\n", sizeof(size_t));

    return 0;
}
