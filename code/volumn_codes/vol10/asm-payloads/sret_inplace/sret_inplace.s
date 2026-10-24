# 03-nrvo-copy-elision 的等价场景：sret 槽就是返回值的家。
# make_point 不把对象搬回来，调用方先备好 16 字节的槽，槽地址经 rdi
# 当隐藏首参数传进来，被调方直接构造进槽，rax 只回传槽地址。
.data
.globl sink
sink:  .quad 0

.text
.globl PointL_ctor
PointL_ctor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movq %rsi, -16(%rbp)
  movq %rdx, -24(%rbp)
  movq -16(%rbp), %rcx
  movq -8(%rbp), %rax
  movq %rcx, (%rax)        # x → 槽+0
  movq -24(%rbp), %rcx
  movq -8(%rbp), %rax
  movq %rcx, 8(%rax)       # y → 槽+8
  nop
  popq %rbp
  ret

.globl make_point
make_point:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  movq %rdi, -8(%rbp)      # 溢出 sret 指针
  movq %rsi, -16(%rbp)     # 溢出 seed
  movq -16(%rbp), %rax
  leaq (%rax,%rax), %rdx   # seed*2
  movq -16(%rbp), %rcx     # seed
  movq -8(%rbp), %rax
  movq %rcx, %rsi          # 第二参数 seed
  movq %rax, %rdi          # this = sret 槽
  call PointL_ctor         # 直接构造进槽，没有中间对象
  nop
  movq -8(%rbp), %rax      # 返回的只是槽地址
  leave
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $32, %rsp
  leaq -32(%rbp), %rax     # q 的槽在 rbp-32
  movl $7, %esi
  movq %rax, %rdi
  call make_point
  movq -32(%rbp), %rdx     # 直接读 q.x
  movq -24(%rbp), %rax     # 直接读 q.y
  addq %rdx, %rax          # 14 + 7 = 21
  movq %rax, sink(%rip)
  movq sink(%rip), %rax    # 返回 21，终态可观察
  leave
  ret
