# 03-canary-defense-and-the-o2-story 的等价场景：金丝雀什么时候来。
# plain 没有 canary：纯算术直接 ret，帧里连金丝雀槽都不开。
# guarded 有数组：装载、存槽、取出、比对四步齐全。fs:0x28 的段寻址
# 执行器不仿真，用 .data 里的固定值顶替，比对逻辑照做。
.data
.globl g_canary
g_canary:  .quad 0x1122334455667788

.text
.globl plain
plain:
  pushq %rbp
  movq %rsp, %rbp
  movl %edi, -0x14(%rbp)
  movl -0x14(%rbp), %eax
  addl $5, %eax            # 纯算术
  popq %rbp
  ret                      # 没有 canary 的装载与比对

.globl guarded
guarded:
  pushq %rbp
  movq %rsp, %rbp
  subq $0x40, %rsp
  movl %edi, -0x34(%rbp)
  movq g_canary(%rip), %rax    # 装载金丝雀
  movq %rax, -8(%rbp)          # 存进 rbp-0x8
  xorl %eax, %eax
  movl -0x34(%rbp), %eax
  movl $1, -0x30(%rbp)         # 数组在这里，金丝雀才有了意义
  movl $2, -0x2c(%rbp)
  movl -0x30(%rbp), %edx
  addl -0x2c(%rbp), %edx
  addl %edx, %eax
  movq -8(%rbp), %rdx          # 取出
  subq g_canary(%rip), %rdx    # 与原件比对
  jne .Lbad
  leave
  ret
.Lbad:
  movl $-1, %eax
  leave
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  pushq %rbx
  movl $10, %edi
  call plain                  # 15
  movl %eax, %ebx
  movl $20, %edi
  call guarded                # 23
  addl %ebx, %eax             # 38
  movl $0, %eax
  popq %rbx
  popq %rbp
  ret
