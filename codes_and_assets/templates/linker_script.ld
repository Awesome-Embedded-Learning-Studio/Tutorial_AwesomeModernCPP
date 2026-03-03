/*
 * 嵌入式系统链接器脚本模板
 *
 * 使用说明：
 * 1. 根据目标 MCU 修改 MEMORY 配置
 * 2. 调整 SECTION 和 布置地址
 * 3. 在 CMakeLists.txt 中添加： ld_script.ld
 *
 * MEMORY 配置示例：
 * MEMORY {
 *   FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K
 *   RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 64K
 * }
 */

/* 入口点 */
ENTRY(Reset_Handler)

/* 最高优先级 */
/* 设置栈顶指针 */
_estack = (ORIGIN(RAM) + LENGTH(RAM)) - 8;

/* 程序大小计算 */
_min_heap_size = 0x200;  /* 最小堆大小 */
_heap_size = 0;              /* 堧大小（由链接器计算） */

/* ==================== 内存定义 ==================== */
MEMORY
{
  /* 修改为您的 MCU 实际内存配置 */
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 256K
  RAM (rwx)        : ORIGIN = 0x20000000, LENGTH = 64K
}

/* ==================== 段分配 ==================== */
SECTIONS
{
  /* 中断向量表 */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = 0x0;
  } > FLASH

  /* 程序代码 */
  .text :
  {
    *(.text)
    *(.text.*)
    *(.glue_7t)
    *(.glue_7)
    *(.rodata)
    *(.rodata.*)
    . = ALIGN(4);
  } > FLASH

  /* ARM 特定段 */
  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  .ARM.attributes :
  {
    *(.ARM.attributes)
  } > FLASH

  /* 只读数据 */
  .rodata :
  {
    *(.rodata)
    *(.rodata.*)
  } > FLASH

  /* ARM 特定 */
  .ARM.exidx : {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > FLASH

  /* 已初始化数据 */
  .data :
  {
    _sdata = .;
    *(.data .data.*)
    *(.gnu.linkonce.d.*)
    _edata = .;
  } > RAM AT > FLASH

  /* 加载地址 */
  _ldata = LOADADDR(.data);

  /* 未初始化数据 */
  .bss :
  {
    *(.bss)
    *(.bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  /* 用户堆栈 */
  ._user_stack :
  {
    . = ALIGN(8);
    PROVIDE ( _estack = . );
  } > RAM

  /* 堆定义 */
  .heap (COPY) :
  {
    __heap_start = .;
    . = . + _min_heap_size;
    . = . + (_heap_size);
    __heap_end = .;
  } > RAM

  /* 删除未使用的段 */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  /* 调试信息 */
  .debug_info      0 : { *(.debug_info .gnu.debugli .debug_info *) }
  .debug_abbrev    0 : { *(.debug_abbrev)
  .debug_line      0 : { *(.debug_line .debug_line.* .debug_line_end) }
  .debug_frame     0 : { *(.debug_frame*) }
  .debug_str       0 : { *(.debug_str*) }
  .debug_loc       0 : { *(.debug_loc*) }
  .debug_macinfo    0 : { *(.debug_macinfo) }
  .debug_ranges    0 : { *(.debug_ranges) }
  .debug_pubtypes  0 : { *(.debug_pubtypes) }
  .debug_pubnames  0 : { *(.debug_pubnames) }
  .ARM.attributes 0 : { *(.ARM.attributes) }
  .debug_frame     0 : { *(.debug_frame*) }
  .debug_str       0 : { *(.debug_str*) }
  .debug_loc       0 : { . = 0; } /* Keep debug_loc if needed */
}
