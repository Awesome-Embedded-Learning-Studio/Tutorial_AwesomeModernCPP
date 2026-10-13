// 03 - Heap Buffer Overflow (堆缓冲区溢出)
//
// 崩溃诱因：通过 new/malloc 分配的数组越界访问（读/写）。
// 越界写可能破坏相邻堆块的元数据，导致后续堆操作崩溃。

#include <cstdio>
#include <cstdlib>

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    // 分配 5 个 int 的堆数组
    int* arr = new int[5];

    // 初始化
    for (int i = 0; i < 5; i++) {
        arr[i] = i * 10;
    }
    printf("Array allocated at %p, size = 5 ints\n", (void*)arr);

    // 【崩溃点】大量越界写入，踩踏后续多个堆块的元数据
    printf("Writing far out of bounds (corrupting heap metadata)...\n");
    for (int i = 0; i < 1000; i++) {
        arr[i] = 0xDEADBEEF; // 从 index 5 开始全部越界
    }
    printf("Wrote 1000 values to a 5-element array\n");

    // 分配新内存触发堆管理器遍历被破坏的空闲链表 → 崩溃
    printf("Allocating more memory (triggers heap corruption crash)...\n");
    for (int i = 0; i < 100; i++) {
        int* tmp = new int[16];
        printf("  new alloc #%d at %p\n", i, (void*)tmp);
        delete[] tmp;
    }

    // delete[] 时，堆管理器发现元数据被破坏 → 可能崩溃
    printf("Deleting original array...\n");
    delete[] arr;
    printf("Done (if you see this, the corruption was silent)\n");

    return 0;
}
