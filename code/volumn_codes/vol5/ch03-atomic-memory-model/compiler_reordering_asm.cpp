/*
 * 验证：编译器在单线程内会合并访存（as-if 规则之内）
 *
 * 背景：文章声称「编译器优化在单线程里不可见（as-if 规则），但机器码
 *       确实变了」。本文件提供两处可在汇编里直接看到的证据：
 *       场景 1：对同一变量连续两次写，中间没有读，-O2 下合并为一次 store；
 *       场景 2：同一个变量的两次读中间夹着一次对别的变量的写，
 *               -O2 下两次读被合并成一条 load（跨线程视角：中间的写入看不见了）。
 *
 * 预期结果：
 *   merge_two_stores —— 对 g_flag 只有一条 movl 写（两条合一）；
 *   double_load      —— 对 g_data 只有一条 movl 读（两条合一）。
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -S -o /tmp/compiler_reordering_asm.s compiler_reordering_asm.cpp
 *
 * 检查：
 *   grep -A6 "merge_two_stores\|double_load" /tmp/compiler_reordering_asm.s
 *
 * 编译器：GCC 16.2.1（WSL2, AMD Ryzen 7 9700X）
 */

int g_flag = 0;
int g_data = 0;

void merge_two_stores(int v) {
    g_flag = v;     // 两次写中间没有任何读，
    g_flag = v + 1; // 第二次覆盖第一次，-O2 下只留第二次的 store
}

int double_load() {
    int a = g_data;
    g_flag = 1;        // 与 g_data 无关的写夹在中间，
    return a + g_data; // 但两次读仍被合并成一条 load
}
