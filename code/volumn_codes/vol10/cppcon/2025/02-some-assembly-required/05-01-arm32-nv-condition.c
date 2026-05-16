/*
 * 验证 ARM32 NV 条件码在不同 ARM 架构版本上的行为差异
 *
 * 背景：
 *   ARM32 的条件码字段占 4 bit (bits 31-28)，理论上可编码 16 种条件。
 *   在 ARMv4 及更早版本中，0b1111 (0xF) 被定义为 NV (Never Execute)。
 *   但从 ARMv5 开始，NV 条件码被废弃，0b1111 编码被重新分配。
 *
 *   在 ARMv7-A 上（本文原始示例使用的架构）：
 *   - 条件码 0b1111 不再是 "Never Execute"
 *   - 使用 0b1111 的行为是 UNPREDICTABLE 或被重新编码为其他指令
 *   - 有效条件码范围仅为 0b0000-0b1110 (EQ 到 AL)
 *
 * 验证方法：
 *   1. 编译为 ARMv7 目标：
 *      arm-none-linux-gnueabihf-gcc -S -O0 05-01-arm32-nv-condition.c -o -
 *      arm-none-linux-gnueabihf-gcc -c 05-01-arm32-nv-condition.c -o test_armv7.o
 *      arm-none-linux-gnueabihf-objdump -d test_armv7.o
 *
 *   2. 编译为 ARMv4 目标（对比用）：
 *      arm-none-linux-gnueabihf-gcc -S -O0 -march=armv4 05-01-arm32-nv-condition.c -o -
 *
 *   3. 在 QEMU 上运行：
 *      arm-none-linux-gnueabihf-gcc -static 05-01-arm32-nv-condition.c -o test_armv7
 *      qemu-arm ./test_armv7
 *
 *   4. ARMv4 对比运行（注意 QEMU 可能不完全模拟 ARMv4 的 NV 行为）：
 *      arm-none-linux-gnueabihf-gcc -static -march=armv4 05-01-arm32-nv-condition.c -o test_armv4
 *      qemu-arm ./test_armv4
 *
 * 参考资料：
 *   - ARM Architecture Reference Manual ARMv7-A/R, Section "The condition code field"
 *     https://developer.arm.com/documentation/ddi0406/c/Application-Level-Architecture/
 *     ARM-Instruction-Set-Encoding/ARM-instruction-set-encoding/The-condition-code-field
 *
 *   - ARM7 Data Sheet: "The never (NV) class of condition codes shall not be used"
 */

#include <stdio.h>

void test_armv7_nv(void) {
    int result = 0;

    /* 正常的 MOV R0, #42，条件码 AL (0xe = 0b1110 = Always) */
    __asm__ volatile("mov r0, #42" : "=r"(result));
    printf("AL (always): result = %d\n", result);

    result = 0;

    /*
     * 手动塞入条件码 NV (0xf) 的 MOV 指令
     * 机器码 0xf3a0002a = MOV R0, #42 with cond=0b1111
     *
     * !! 警告 !!
     * 在 ARMv7 上，这条指令的行为是 UNPREDICTABLE。
     * 它可能：什么都不做 / 执行 MOV / 触发未定义指令异常
     * 不要在 ARMv7+ 代码中依赖此行为
     */
    __asm__ volatile(".word 0xf3a0002a" : "=r"(result));
    printf("NV (0xf on ARMv7): result = %d  [UNPREDICTABLE - do not rely on this]\n", result);
}

int main(void) {
    printf("=== ARM32 NV Condition Code Test ===\n");
    printf("Note: On ARMv7, cond=0b1111 is NOT 'Never Execute'.\n");
    printf("It was deprecated in ARMv5 and the encoding was repurposed.\n");
    printf("Valid condition codes on ARMv7: 0b0000-0b1110 (EQ through AL)\n\n");

    test_armv7_nv();
    return 0;
}
