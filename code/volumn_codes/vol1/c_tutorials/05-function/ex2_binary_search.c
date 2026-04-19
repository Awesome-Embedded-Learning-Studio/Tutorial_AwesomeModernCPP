#include <stdio.h>
#include <stddef.h>

int binary_search_recursive(const int* arr, size_t len, int target)
{
    if (len == 0) return -1;

    size_t mid = len / 2;
    if (arr[mid] == target) {
        return (int)mid;
    } else if (arr[mid] > target) {
        return binary_search_recursive(arr, mid, target);
    } else {
        int result = binary_search_recursive(arr + mid + 1, len - mid - 1, target);
        return result == -1 ? -1 : (int)(mid + 1) + result;
    }
}

int binary_search_iterative(const int* arr, size_t len, int target)
{
    size_t lo = 0, hi = len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] == target) {
            return (int)mid;
        } else if (arr[mid] < target) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return -1;
}

int main(void)
{
    int data[] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19};
    size_t len = sizeof(data) / sizeof(data[0]);

    int targets[] = {7, 1, 19, 8};
    for (int i = 0; i < 4; i++) {
        int t = targets[i];
        int r = binary_search_recursive(data, len, t);
        int it = binary_search_iterative(data, len, t);
        printf("search(%2d): recursive=%2d, iterative=%2d\n", t, r, it);
    }

    return 0;
}
