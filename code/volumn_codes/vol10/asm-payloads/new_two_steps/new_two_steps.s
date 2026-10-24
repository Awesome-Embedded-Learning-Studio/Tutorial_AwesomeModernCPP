# 01-allocation-from-operator-new 的等价场景：new 的两步与 delete 的两步。
# operator new/delete 走 PLT 链接 libc，执行器不仿真，这里用最小桩顶替：
# new 返回 .data 里的一块静态缓冲，delete 只记一次调用次数。
# 教学主线不受影响：分配与构造是两个独立的 call，析构与释放同理。
.data
.globl heap_buf
heap_buf:   .space 16
.globl delete_count
delete_count: .long 0

.text
.globl operator_new
operator_new:
  movabs $heap_buf, %rax   # 桩：固定地址顶替分配器
  ret

.globl operator_delete
operator_delete:
  movl delete_count(%rip), %eax
  addl $1, %eax
  movl %eax, delete_count(%rip)
  ret

.globl Point_ctor
Point_ctor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movl %esi, -12(%rbp)
  movl %edx, -16(%rbp)
  movq -8(%rbp), %rax
  movl -12(%rbp), %edx
  movl %edx, (%rax)        # x → this+0
  movq -8(%rbp), %rax
  movl -16(%rbp), %edx
  movl %edx, 4(%rax)       # y → this+4
  nop
  popq %rbp
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  pushq %rbx
  subq $24, %rsp
  movl $8, %edi            # sizeof(Point) = 8
  call operator_new        # 第一步：向分配器要内存
  movq %rax, %rbx          # 块地址存进被调用者保存寄存器 rbx
  movl $4, %edx            # y = 4
  movl $3, %esi            # x = 3
  movq %rbx, %rdi
  call Point_ctor          # 第二步：对着新内存调构造
  movl $0, %eax
  movq %rbx, -24(%rbp)
  testb %al, %al           # 恒为零，异常清理段照文章保留为死代码
  je .L3
  movl $8, %esi
  movq %rbx, %rdi
  call operator_delete     # （不可达）
.L3:
  movq -24(%rbp), %rax
  movl (%rax), %edx
  movq -24(%rbp), %rax
  movl 4(%rax), %eax
  addl %edx, %eax          # p->x + p->y = 7
  movl %eax, -28(%rbp)
  movl -28(%rbp), %eax
  movl %eax, heap_buf+8(%rip)  # 桩缓冲尾部当 sink 用，不碰对象区
  movq -24(%rbp), %rax
  testq %rax, %rax         # delete 的空指针检查
  je .L4
  movl $8, %esi
  movq %rax, %rdi
  call operator_delete     # delete p → operator delete(p, 8)
.L4:
  movl $0, %eax
  movq -8(%rbp), %rbx
  leave
  ret
