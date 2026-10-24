# 02-class-type-cost 的等价场景：类类型的 i++ 要付一次拷贝构造的价。
# 后缀版先把旧值拷贝出来（Iter 的拷贝构造，copies 计数加一）再自增；
# 前缀版直接加在自己身上，一个 call 都没有。int 版的场景见本系列第一篇。
.data
.globl Iter_copies
Iter_copies:  .long 0
.globl sink
sink:        .long 0

.text
.globl Iter_copy_ctor
Iter_copy_ctor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movq %rsi, -16(%rbp)
  movq -16(%rbp), %rax
  movq (%rax), %rdx        # 读取旧对象的 pos
  movq -8(%rbp), %rax
  movq %rdx, (%rax)        # 写进新对象
  movl Iter_copies(%rip), %eax
  addl $1, %eax
  movl %eax, Iter_copies(%rip)   # copies += 1，拷贝的价记在明处
  nop
  popq %rbp
  ret

.globl iter_postinc
iter_postinc:              # Iter operator++(int)
  pushq %rbp
  movq %rsp, %rbp
  subq $32, %rsp
  movq %rdi, -24(%rbp)     # this
  movq %rdi, %rax
  movq %rax, -16(%rbp)
  leaq -8(%rbp), %rax      # 旧值对象放在栈上
  movq %rax, %rdi
  movq -16(%rbp), %rax
  movq %rax, %rsi
  call Iter_copy_ctor      # 拷贝旧值：这就是后缀多付的价
  movq -24(%rbp), %rax
  movq $1, %rdx
  addq %rdx, (%rax)        # pos += 1
  movq -8(%rbp), %rax      # 返回旧值：槽里躺着的就是拷出来的 pos
  leave
  ret

.globl iter_preinc
iter_preinc:               # Iter& operator++()
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movq -8(%rbp), %rax
  movq $1, %rdx
  addq %rdx, (%rax)        # 直接加，没有拷贝
  movq -8(%rbp), %rax
  popq %rbp
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  movq $100, -16(%rbp)     # it.pos = 100
  leaq -16(%rbp), %rax
  movq %rax, %rdi
  call iter_postinc        # 旧值 100 拷出来，it 变 101
  movq %rax, %rdx
  leaq -16(%rbp), %rax
  movq %rax, %rdi
  call iter_preinc         # it 变 102，没有 call 进拷贝构造
  movq (%rax), %rax        # 102
  addl %edx, %eax          # 100 + 102 = 202
  movl %eax, sink(%rip)
  movl Iter_copies(%rip), %eax  # 返回 1：整个流程只付了一次拷贝
  movl $0, %eax
  leave
  ret
