# 02-constructor-and-destructor-calls 的等价场景（栈对象部分）。
# 教学点：构造与析构都是编译器插的 call，析构按构造的逆序。
# p(1, 2) 与 q(3, 4) 先后在栈上构造，作用域结束时 q 先析构、p 后析构。
# 堆对象的 operator new 走 PLT，执行器不仿真 libc，那半段不在场景里。
.data
.globl log_sink
log_sink:  .long 0

.text
.globl Point_ctor
Point_ctor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movl %esi, -12(%rbp)
  movl %edx, -16(%rbp)
  movq -8(%rbp), %rax
  movl -12(%rbp), %edx
  movl %edx, (%rax)         # x = a → this+0
  movq -8(%rbp), %rax
  movl -16(%rbp), %edx
  movl %edx, 4(%rax)        # y = b → this+4
  nop
  popq %rbp
  ret

.globl Point_dtor
Point_dtor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movl log_sink(%rip), %eax # 把析构痕迹记进计数，让空析构可见
  addl $1, %eax
  movl %eax, log_sink(%rip)
  nop
  popq %rbp
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  leaq -8(%rbp), %rax       # p 在 rbp-8
  movl $2, %edx
  movl $1, %esi
  movq %rax, %rdi
  call Point_ctor           # 编译器插入的构造调用
  leaq -16(%rbp), %rax      # q 在 rbp-16
  movl $4, %edx
  movl $3, %esi
  movq %rax, %rdi
  call Point_ctor
  leaq -16(%rbp), %rax      # 作用域结束，先析构 q
  movq %rax, %rdi
  call Point_dtor
  leaq -8(%rbp), %rax       # 再析构 p，与构造逆序
  movq %rax, %rdi
  call Point_dtor
  movl log_sink(%rip), %eax # 返回析构计数 2，终态可观察
  movl $0, %eax
  leave
  ret
