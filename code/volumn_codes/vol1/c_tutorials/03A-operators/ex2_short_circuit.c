#include <stdio.h>
#include <stddef.h>

int find_first_above(const int* arr, size_t len, int threshold)
{
    for (size_t i = 0; i < len && arr[i] <= threshold; i++) {
        // Short-circuit: arr[i] is only evaluated if i < len
    }
    // Simpler implementation with explicit check:
    for (size_t i = 0; i < len; i++) {
        if (arr[i] > threshold) {
            return (int)i;
        }
    }
    return -1;
}

int main(void)
{
    int data[] = {1, 5, 3, 8, 2, 9, 4};
    size_t len = sizeof(data) / sizeof(data[0]);

    int idx = find_first_above(data, len, 5);
    if (idx >= 0) {
        printf("First element > 5 is at index %d (value %d)\n", idx, data[idx]);
    } else {
        printf("No element > 5 found\n");
    }

    return 0;
}
