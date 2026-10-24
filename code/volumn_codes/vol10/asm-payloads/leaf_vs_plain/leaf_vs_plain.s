# 02-leaf-functions-stack-direction-and-o2 的等价场景：-O0 的老实帧
# 对照 leal 一条加法的极简叶子。three 落栈建帧求 1+2+3，
# add_fast 是 -O2 形态：leal (%rdi,%rsi), %eax 后直接 ret。
.data
.globl sink
sink:  .long 0

.text
.globl three
three:
  pushq %rbp
  movq %rsp, %rbp
  movl $1, -12(%rbp)
  movl $2, -8(%rbp)
  movl $3, -4(%rbp)
  movl -12(%rbp), %edx
  movl -8(%rbp), %eax
  addl %eax, %edx
  movl -4(%rbp), %eax
  addl %edx, %eax
  movl %eax, -16(%rbp)     # volatile sink 接住结果
  movl -16(%rbp), %eax
  popq %rbp
  ret

.globl add_fast
add_fast:
  leal (%rdi,%rsi), %eax   # 参数直接在寄存器里相加
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  call three               # 1+2+3 = 6
  movl %eax, -4(%rbp)
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)
  movl $22, %esi
  movl $11, %edi
  call add_fast            # 11+22 = 33，没有帧也不落栈
  addl %eax, -4(%rbp)
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)    # sink = 6 + 33 = 39
  movl $0, %eax
  leave
  ret
