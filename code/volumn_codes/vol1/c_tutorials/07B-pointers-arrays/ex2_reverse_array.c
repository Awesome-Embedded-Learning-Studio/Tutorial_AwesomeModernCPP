#include <stdio.h>
#include <stddef.h>

void reverse_array(int* data, size_t count)
{
    int* left = data;
    int* right = data + count - 1;
    while (left < right) {
        int tmp = *left;
        *left = *right;
        *right = tmp;
        left++;
        right--;
    }
}

static void print_array(const int* data, size_t count)
{
    printf("[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) printf(", ");
        printf("%d", data[i]);
    }
    printf("]\n");
}

int main(void)
{
    int data[] = {1, 2, 3, 4, 5, 6, 7};
    size_t count = sizeof(data) / sizeof(data[0]);

    printf("Before: ");
    print_array(data, count);

    reverse_array(data, count);

    printf("After:  ");
    print_array(data, count);

    return 0;
}
