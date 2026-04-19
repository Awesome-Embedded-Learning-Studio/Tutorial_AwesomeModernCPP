#include <stdio.h>
#include <stdint.h>

#define PRINT_SIZEOF(type) printf("sizeof(%-15s) = %zu bytes\n", #type, sizeof(type))

int main(void)
{
    PRINT_SIZEOF(char);
    PRINT_SIZEOF(short);
    PRINT_SIZEOF(int);
    PRINT_SIZEOF(long);
    PRINT_SIZEOF(long long);
    PRINT_SIZEOF(int8_t);
    PRINT_SIZEOF(uint8_t);
    PRINT_SIZEOF(int32_t);
    PRINT_SIZEOF(uint32_t);
    PRINT_SIZEOF(int64_t);
    PRINT_SIZEOF(size_t);

    return 0;
}
