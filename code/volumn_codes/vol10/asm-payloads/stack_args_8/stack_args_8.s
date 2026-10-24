# 02-stack-args-and-return-values 的等价场景：第七、第八个参数进栈。
# 六个整数寄存器装满之后，多出来的实参由调用者压栈，被调方从 16(%rbp)、24(%rbp) 读回。
# 调用完调用方自己 addq $8, %rsp 把参数区收掉（cdecl 风格的调用方清栈）。
.data
.globl sink
sink:  .long 0

.text
.globl sum7
sum7:
  pushq %rbp
  movq %rsp, %rbp
  movl %edi, -4(%rbp)
  movl %esi, -8(%rbp)
  movl %edx, -12(%rbp)
  movl %ecx, -16(%rbp)
  movl %r8d, -20(%rbp)
  movl %r9d, -24(%rbp)
  movl -4(%rbp), %eax
  addl -8(%rbp), %eax
  addl -12(%rbp), %eax
  addl -16(%rbp), %eax
  addl -20(%rbp), %eax
  addl -24(%rbp), %eax
  movl 16(%rbp), %edx      # 第七个参数：返回地址与 saved rbp 之上
  addl %edx, %eax
  popq %rbp
  ret

.globl sum8
sum8:
  pushq %rbp
  movq %rsp, %rbp
  movl %edi, -4(%rbp)
  movl %esi, -8(%rbp)
  movl %edx, -12(%rbp)
  movl %ecx, -16(%rbp)
  movl %r8d, -20(%rbp)
  movl %r9d, -24(%rbp)
  movl -4(%rbp), %eax
  addl -8(%rbp), %eax
  addl -12(%rbp), %eax
  addl -16(%rbp), %eax
  addl -20(%rbp), %eax
  addl -24(%rbp), %eax
  movl 16(%rbp), %edx      # 第七个参数
  addl %edx, %eax
  movl 24(%rbp), %edx      # 第八个参数
  addl %edx, %eax
  popq %rbp
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  pushq $7                  # 第七个实参先进栈
  movl $6, %r9d
  movl $5, %r8d
  movl $4, %ecx
  movl $3, %edx
  movl $2, %esi
  movl $1, %edi
  call sum7                 # 1+...+7 = 28
  addq $8, %rsp             # 调用方收掉参数区
  movl %eax, -4(%rbp)
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)
  pushq $8                  # sum8：两个栈参数，第八个先压
  pushq $7
  movl $6, %r9d
  movl $5, %r8d
  movl $4, %ecx
  movl $3, %edx
  movl $2, %esi
  movl $1, %edi
  call sum8                 # 1+...+8 = 36
  addq $16, %rsp            # 两个参数，收 16 字节
  movl %eax, -4(%rbp)
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)
  movl $0, %eax
  leave
  ret
