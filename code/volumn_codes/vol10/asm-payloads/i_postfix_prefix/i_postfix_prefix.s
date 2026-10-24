# 01-int-instructions 的等价场景：use_post(41) 与 use_pre(41)。
# 源码是 volatile int sink 配 x = i++ / x = ++i，volatile 压住了每次读写。
# 指令序列照 -O0 产物复刻，只把修饰名还原成可读符号。
.data
.globl sink
sink:  .long 0

.text
.globl use_post
use_post:
  pushq %rbp
  movq %rsp, %rbp
  movl %edi, -20(%rbp)      # 参数 i 落栈
  movl -20(%rbp), %eax      # 取 i 的旧值 → eax
  leal 1(%rax), %edx        # 旧值 + 1 → edx
  movl %edx, -20(%rbp)      # 写回 i
  movl %eax, -4(%rbp)       # x = 旧值
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)     # sink = x
  nop
  popq %rbp
  ret

.globl use_pre
use_pre:
  pushq %rbp
  movq %rsp, %rbp
  movl %edi, -20(%rbp)
  addl $1, -20(%rbp)        # ++i 直接加在内存上，没有旧值搬运
  movl -20(%rbp), %eax      # 取 i 的新值
  movl %eax, -4(%rbp)       # x = 新值
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)
  nop
  popq %rbp
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $41, %edi
  call use_post
  movl $41, %edi
  call use_pre
  movl $0, %eax
  popq %rbp
  ret
