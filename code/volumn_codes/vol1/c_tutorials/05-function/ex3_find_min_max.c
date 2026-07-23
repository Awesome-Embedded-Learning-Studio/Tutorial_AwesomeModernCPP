#include <stddef.h>
#include <stdio.h>

void find_min_max(const int* data, size_t len, int* min_out, int* max_out) {
    if (data == NULL || min_out == NULL || max_out == NULL || len < 1) {
        return;
    }
    *min_out = *max_out = data[0];
    for (size_t i = 1; i < len; i++) {
        if (data[i] < *min_out) {
            *min_out = data[i];
        }
        if (data[i] > *max_out) {
            *max_out = data[i];
        }
    }
}

int main(void) {
    int data[] = {3, 1, 7, -2, 9, 4, 8, 0, 5};
    size_t len = sizeof(data) / sizeof(data[0]);
    int lo, hi;

    find_min_max(data, len, &lo, &hi);
    printf("min=%d max=%d\n", lo, hi);

    // 单元素边界
    int single[] = {42};
    int s_lo, s_hi;
    find_min_max(single, 1, &s_lo, &s_hi);
    printf("single: min=%d max=%d\n", s_lo, s_hi);

    // 空数组边界(函数应直接返回不改 out)
    int empty_lo = 99, empty_hi = 99;
    find_min_max(data, 0, &empty_lo, &empty_hi);
    printf("empty: min=%d max=%d (unchanged expect 99 99)\n", empty_lo, empty_hi);
    return 0;
}
