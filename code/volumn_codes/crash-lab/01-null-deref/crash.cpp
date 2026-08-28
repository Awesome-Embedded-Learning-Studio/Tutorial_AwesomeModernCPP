// 01 - Null Pointer Dereference (空指针解引用)
//
// 崩溃诱因：对 nullptr / NULL 指针进行解引用操作。
// 这是最经典、最容易理解的崩溃类型。

#include <cstdio>

// 模拟一个可能返回空指针的函数
int* find_value(int* arr, int size, int target) {
    for (int i = 0; i < size; i++) {
        if (arr[i] == target)
            return &arr[i];
    }
    return nullptr; // 没找到，返回空指针
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    int data[] = {10, 20, 30, 40, 50};

    // 查找不存在的值，得到 nullptr
    int* result = find_value(data, 5, 999);
    printf("find_value returned: %p\n", (void*)result);

    // 【崩溃点】未检查空指针，直接解引用
    printf("*result = %d  <-- null deref!\n", *result);

    return 0;
}
