# 04-devirtualization-under-o2 的等价场景：同一份源码的两副输出。
# o0 版三步齐全：装 vptr、取槽位、call *%rdx；
# o2 版编译器看穿了对象的静态类型，间接 call 消失，换成一个直接 call。
# 场景把两版放进同一个 main 对照，符号名去掉修饰。
.data
.globl vtable_d
vtable_d:  .quad d_poke
.globl sink
sink:      .long 0

.text
.globl d_poke
d_poke:
  pushq %rbp
  movq %rsp, %rbp
  mov $0x2a, %eax          # 42：D::poke 的返回值
  popq %rbp
  ret

.globl seen_o0
seen_o0:
  pushq %rbp
  movq %rsp, %rbp
  subq $32, %rsp
  leaq vtable_d(%rip), %rax
  movq %rax, -24(%rbp)     # 构造 d：写 vptr
  leaq -24(%rbp), %rax
  movq %rax, -16(%rbp)     # p = &d
  movq -16(%rbp), %rax
  movq (%rax), %rax        # 装 vptr
  movq (%rax), %rdx        # 取槽位
  movq -16(%rbp), %rax
  movq %rax, %rdi          # this → rdi
  call *%rdx               # -O0：间接 call，三步齐全
  movl %eax, -28(%rbp)
  movl -28(%rbp), %eax
  movl %eax, sink(%rip)
  movl -28(%rbp), %eax
  leave
  ret

.globl seen_o2
seen_o2:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  call d_poke              # -O2：目标已知，直接 call，三步全没了
  movl %eax, -4(%rbp)
  movl -4(%rbp), %eax
  movl %eax, sink(%rip)
  movl -4(%rbp), %eax
  leave
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  call seen_o0
  movl %eax, -4(%rbp)
  call seen_o2
  addl -4(%rbp), %eax      # 42 + 42 = 84
  movl $0, %eax
  leave
  ret
