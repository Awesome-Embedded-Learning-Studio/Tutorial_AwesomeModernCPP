#include <stdio.h>
const int* linear_search(const int* data, size_t count, int target);

int main(void) {
    const int arr[] = {10, 21, 32, 43, 54, 65, 76, 87, 98, 9};
    int target = 43;

    // 使用 sizeof 自动计算数组大小
    size_t count = sizeof(arr) / sizeof(arr[0]);

    const int* result = linear_search(arr, count, target);

    if (result != NULL) {
        printf("找到了目标 %d，位于地址 %p，是数组的第 %llu 个元素。\n", target, (void*)result,
               (unsigned long long)(result - arr));
    } else {
        printf("未找到目标 %d。\n", target);
    }

    return 0;
}

const int* linear_search(const int* data, size_t count, int target) {
    // 防御性编程：如果指针为空，或者数量为0，直接返回 NULL
    if (data == NULL || count == 0) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        if (*(data + i) == target) {
            return data + i;
        }
    }
    return NULL;
}
