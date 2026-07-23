#include <stddef.h>
#include <stdio.h>

int find_first_above(const int* arr, size_t len, int threshold) {
    // 数组为空或长度为 0，直接返回 -1
    if (arr == NULL || len == 0) {
        return -1;
    }

    size_t i = 0;

    // 利用短路求值防越界：先判 i < len，成立才访问 arr[i]
    while (i < len && arr[i] <= threshold) {
        i++;
    }

    // 退出循环后，i < len 说明找到了，否则就是遍历完没找到
    return (i < len) ? (int)i : -1;
}

int main(void) {
    // 演示:在数组中找第一个大于阈值的元素
    int arr[] = {1, 3, 5, 7, 9};
    size_t len = sizeof(arr) / sizeof(arr[0]);

    int idx = find_first_above(arr, len, 4);
    printf("第一个大于 4 的元素下标: %d\n", idx); // 期望 2 (arr[2]=5)

    idx = find_first_above(arr, len, 100);
    printf("找不到大于 100 的元素: %d\n", idx); // 期望 -1

    idx = find_first_above(NULL, 0, 4);
    printf("空数组 / 空指针: %d\n", idx); // 期望 -1
    return 0;
}
