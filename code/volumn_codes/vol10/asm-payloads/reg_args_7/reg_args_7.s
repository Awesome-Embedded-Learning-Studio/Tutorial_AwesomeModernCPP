# 01-integer-args-register-allocation 的等价场景：add(11, 22) 与 sum6(1..6)。
# 教学点是参数装载序列：edi、esi、edx、ecx、r8d、r9d 各就各位。
# 第 7 个参数进栈的场景在下一篇，这里六个寄存器刚好填满。
.data
.globl sink
sink:  .long 0

.text
.globl add
add:
  pushq %rbp
  movq %rsp, %rbp
  movl %edi, -4(%rbp)
  movl %esi, -8(%rbp)
  movl -4(%rbp), %edx
  movl -8(%rbp), %eax
  addl %edx, %eax
  popq %rbp
  ret

.globl sum6
sum6:
  pushq %rbp
  movq %rsp, %rbp
  movl %edi, -4(%rbp)      # 六个参数挨个落栈
  movl %esi, -8(%rbp)
  movl %edx, -12(%rbp)
  movl %ecx, -16(%rbp)
  movl %r8d, -20(%rbp)
  movl %r9d, -24(%rbp)
  movl -4(%rbp), %eax      # 再从栈槽读回累加
  addl -8(%rbp), %eax
  addl -12(%rbp), %eax
  addl -16(%rbp), %eax
  addl -20(%rbp), %eax
  addl -24(%rbp), %eax
  popq %rbp
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  movl $22, %esi
  movl $11, %edi
  call add                 # 11 + 22 = 33
  movl %eax, -4(%rbp)
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)
  movl $6, %r9d            # 第六个参数 → r9d
  movl $5, %r8d            # 第五个 → r8d
  movl $4, %ecx            # 第四个 → ecx
  movl $3, %edx            # 第三个 → edx
  movl $2, %esi            # 第二个 → esi
  movl $1, %edi            # 第一个 → edi
  call sum6                # 1+2+3+4+5+6 = 21
  movl %eax, -4(%rbp)
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)
  movl $0, %eax
  leave
  ret
