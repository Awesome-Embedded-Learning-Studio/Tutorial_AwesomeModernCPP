/* 统一布局：代码页固定起点，数据页独立可写（sink 等全局写目标），stop 守卫固定地址。
   栈由 boot.s 的 mmap 提供，不经链接器。 */
ENTRY(_start)
PHDRS { text PT_LOAD FILEHDR PHDRS FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  .text   0x555555555119 : { *(.text) } :text
  .bootstrap 0x555555555200 : { *(.bootstrap) } :text
  .stop   0x555555555300 : { *(.stop) } :text
  .data   0x555555556000 : { *(.data .bss .rodata) } :data
  /DISCARD/ : { *(.eh_frame) *(.note*) *(.comment) }
}
