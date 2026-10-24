# 共用启动桩：mmap 固定栈页后进入 main（入口由场景 manifest 指定）。
# 栈顶/帧基与 initial.json 的 rsp/rbp 保持一致。
.section .bootstrap,"ax"
.global _start
_start:
  mov $9, %eax
  movabs $0x7fffffffc000, %rdi
  mov $8192, %esi
  mov $3, %edx
  mov $0x32, %r10d
  mov $-1, %r8
  xor %r9d, %r9d
  syscall
  movabs $0x7fffffffd0d8, %rsp
  movabs $0x7fffffffd0f0, %rbp
  jmp main
.section .stop,"ax"
.global __stop
__stop:
  int3
.section .note.GNU-stack,"",@progbits
