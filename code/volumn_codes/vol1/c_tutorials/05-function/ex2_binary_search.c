#include <stddef.h>
#include <stdio.h>

int binary_search_recursive(const int* arr, size_t len, int target) {
    if (len < 1) {
        printf("%d is not found in index\n", target);
        return -1;
    }
    size_t mid = (len - 1) / 2;

    if (arr[mid] == target) {
        return mid;
    }
    if (arr[mid] < target) {
        int res = binary_search_recursive(arr + mid + 1, len - mid - 1, target);
        return (res == -1) ? -1 : (int)(res + mid + 1);
    }
    if (arr[mid] > target) {
        return binary_search_recursive(arr, mid, target);
    }
    return -1;
}

int binary_search_iterative(const int* arr, size_t len, int target) {
    size_t lo = 0, hi = len; // 搜索区间 [lo, hi)，左闭右开
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] == target) {
            return mid;
        }
        if (arr[mid] < target) {
            lo = mid + 1; // 搜右半边，lo 只增不下溢
        } else {
            hi = mid; // 搜左半边，hi 收敛到 mid，不下溢
        }
    }
    printf("%d is not found in index\n", target);
    return -1;
}

int main(void) {
    int arr[] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19};
    size_t len = sizeof(arr) / sizeof(arr[0]);

    printf("--- recursive ---\n");
    for (int t = 0; t <= 20; t++) {
        int idx = binary_search_recursive(arr, len, t);
        if (idx >= 0) {
            printf("arr[%d] = %d\n", idx, t);
        }
    }

    printf("--- iterative ---\n");
    for (int t = 0; t <= 20; t++) {
        int idx = binary_search_iterative(arr, len, t);
        if (idx >= 0) {
            printf("arr[%d] = %d\n", idx, t);
        }
    }
    return 0;
}
