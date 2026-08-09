/*
 * 最小 Cortex-M4F 启动:向量表 + 复位处理(搬 .data、清 .bss、调 main)。
 * 其余异常/IRQ 全弱符号兜底到 Default_Handler(闪灯不用中断)。
 */
.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.extern _estack
.extern _sidata
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss
.extern main

.section .isr_vector, "a", %progbits
.global __isr_vector
.type __isr_vector, %object
__isr_vector:
    .word _estack                 /*  0: 初始 SP            */
    .word Reset_Handler           /*  1: Reset              */
    .word NMI_Handler             /*  2: NMI                */
    .word HardFault_Handler       /*  3: HardFault          */
    .word MemManage_Handler       /*  4: MemManage          */
    .word BusFault_Handler        /*  5: BusFault           */
    .word UsageFault_Handler      /*  6: UsageFault         */
    .word 0                       /*  7: Reserved           */
    .word 0                       /*  8: Reserved           */
    .word 0                       /*  9: Reserved           */
    .word 0                       /* 10: Reserved           */
    .word SVC_Handler             /* 11: SVCall             */
    .word DebugMon_Handler        /* 12: Debug monitor      */
    .word 0                       /* 13: Reserved           */
    .word PendSV_Handler          /* 14: PendSV             */
    .word SysTick_Handler         /* 15: SysTick            */
    .rept 82                      /* F407 外部 IRQ 占位      */
    .word Default_Handler
    .endr
.size __isr_vector, . - __isr_vector

.section .text.Reset_Handler, "ax", %progbits
.weak Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    ldr   r0, =_sidata
    ldr   r1, =_sdata
    ldr   r2, =_edata
1:  cmp   r1, r2
    bcs   2f
    ldr   r3, [r0], #4
    str   r3, [r1], #4
    b     1b
2:  ldr   r1, =_sbss
    ldr   r2, =_ebss
    movs  r3, #0
3:  cmp   r1, r2
    bcs   4f
    str   r3, [r1], #4
    b     3b
4:  bl    main
5:  b     5b

.section .text.Default_Handler, "ax", %progbits
.weak Default_Handler
.type Default_Handler, %function
Default_Handler:
    b Default_Handler

.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler
.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler
.weak MemManage_Handler
.thumb_set MemManage_Handler, Default_Handler
.weak BusFault_Handler
.thumb_set BusFault_Handler, Default_Handler
.weak UsageFault_Handler
.thumb_set UsageFault_Handler, Default_Handler
.weak SVC_Handler
.thumb_set SVC_Handler, Default_Handler
.weak DebugMon_Handler
.thumb_set DebugMon_Handler, Default_Handler
.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler
