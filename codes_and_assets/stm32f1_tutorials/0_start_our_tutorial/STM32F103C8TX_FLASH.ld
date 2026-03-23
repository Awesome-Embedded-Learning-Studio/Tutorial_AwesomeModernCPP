/* STM32F103C8T6 链接脚本
 *   Flash : 64KB @ 0x08000000
 *   SRAM  : 20KB @ 0x20000000
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);   /* 栈顶 = SRAM 末尾 */

MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
}

SECTIONS
{
  /* ── 中断向量表，必须放最前面 ── */
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } >FLASH

  /* ── 代码 + 只读数据 ── */
  .text :
  {
    *(.text*)
    *(.rodata*)
    _etext = .;
  } >FLASH

  /* ── 初始化数组（C++ 全局对象构造，C 也保留以防万一）── */
  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* ── 初始化数据：Flash 存储，运行时复制到 SRAM ── */
  _sidata = LOADADDR(.data);
  .data :
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } >RAM AT >FLASH

  /* ── 未初始化数据：SRAM，启动文件负责清零 ── */
  .bss :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } >RAM
}
