#include <stdio.h>
#include <stddef.h>
#include <limits.h>

void find_min_max(const int* data, size_t len, int* min_out, int* max_out)
{
    if (len == 0) {
        *min_out = 0;
        *max_out = 0;
        return;
    }

    int min_val = data[0];
    int max_val = data[0];
    for (size_t i = 1; i < len; i++) {
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }
    *min_out = min_val;
    *max_out = max_val;
}

int main(void)
{
    int data[] = {42, -5, 17, 99, 0, -128, 63, 7};
    size_t len = sizeof(data) / sizeof(data[0]);

    int min_val, max_val;
    find_min_max(data, len, &min_val, &max_val);
    printf("Array: min=%d, max=%d\n", min_val, max_val);

    return 0;
}
