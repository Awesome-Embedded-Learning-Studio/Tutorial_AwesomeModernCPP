#include <stdio.h>
#include <stddef.h>

void print_int_array(const int* data, size_t count)
{
    const int* end = data + count;
    printf("[");
    int first = 1;
    for (const int* p = data; p < end; p++) {
        if (!first) printf(", ");
        printf("%d", *p);
        first = 0;
    }
    printf("]\n");
}

int main(void)
{
    int data[] = {10, 20, 30, 40, 50};
    size_t count = sizeof(data) / sizeof(data[0]);

    print_int_array(data, count);
    return 0;
}
