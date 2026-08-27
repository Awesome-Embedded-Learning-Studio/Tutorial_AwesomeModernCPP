// 02 - Use-After-Free (悬垂指针 / 释放后使用)
//
// 崩溃诱因：delete 释放堆内存后，仍然通过原指针访问该内存。
// 这是最常见、最危险的内存安全 bug 之一。

#include <cstdio>
#include <cstdlib>

int main() {
    // 禁用 stdout 缓冲，确保崩溃前输出可见
    setvbuf(stdout, nullptr, _IONBF, 0);
    // 1. 分配一块堆内存
    int* p = new int(42);
    printf("Before free: *p = %d, p = %p\n", *p, (void*)p);

    // 2. 释放内存
    delete p;
    printf("After free:  memory released\n");

    // 3. 【崩溃点】释放后继续使用 —— Use-After-Free
    //    此时 *p 读取的是已释放的堆内存，属于未定义行为（UB）。
    //    - 可能读到原值（堆管理器尚未覆盖）
    //    - 可能读到垃圾数据
    //    - 可能 SIGSEGV（页面已被归还给 OS）
    //    - ASAN 下必定被检测到
    printf("After free:  *p = %d  <-- UAF! reading freed memory\n", *p);

    // 4. 更危险的场景：往已释放的内存写入数据
    //    可能破坏堆管理器的元数据，导致后续操作崩溃
    *p = 999;
    printf("After free:  wrote 999 to freed memory <-- heap corruption!\n");

    // 5. 再次分配可能复用刚才释放的内存块
    int* q = new int(0);
    printf("New alloc:   *q = %d, q = %p (may overlap with freed p)\n", *q, (void*)q);

    delete q;
    return 0;
}
