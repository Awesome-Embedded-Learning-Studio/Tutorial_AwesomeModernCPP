# 02-delete-two-steps-and-placement-new 的等价场景。
# new 与 delete 各两步对称展开：分配、构造，析构、释放。
# 段末尾加一小段 placement new：跳过分配，直接对着栈上缓冲调构造。
# operator new/delete 走 PLT 链接 libc，用 .data 桩顶替，与上一篇场景同一套。
.data
.globl heap_buf
heap_buf:      .space 16
.globl dtor_count
dtor_count:    .long 0

.text
.globl operator_new
operator_new:
  movabs $heap_buf, %rax
  ret

.globl operator_delete
operator_delete:
  ret                     # 桩：真实释放没有可观察的动作

.globl Resource_ctor
Resource_ctor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movl %esi, -12(%rbp)
  movq -8(%rbp), %rax
  movl -12(%rbp), %edx
  movl %edx, (%rax)      # id → this+0
  nop
  popq %rbp
  ret

.globl Resource_dtor
Resource_dtor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movl dtor_count(%rip), %eax   # 析构计数，让删除侧第一步可见
  addl $1, %eax
  movl %eax, dtor_count(%rip)
  nop
  popq %rbp
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  pushq %rbx
  subq $32, %rsp
  movl $4, %edi            # sizeof(Resource) = 4
  call operator_new        # new 第一步：分配
  movq %rax, %rbx          # 指针交给被调用者保存寄存器保管
  movl $7, %esi
  movq %rbx, %rdi
  call Resource_ctor       # new 第二步：构造
  movl $0, %eax
  movq %rbx, -24(%rbp)
  testb %al, %al           # 恒跳转，异常清理死代码照文章保留
  je .L4
  movl $4, %esi
  movq %rbx, %rdi
  call operator_delete     # （不可达）
.L4:
  movq -24(%rbp), %rax
  movl (%rax), %eax        # 读 r->id = 7
  movl %eax, -28(%rbp)
  movl -28(%rbp), %eax
  movq -24(%rbp), %rbx
  testq %rbx, %rbx         # delete 的空指针检查
  je .L5
  movq %rbx, %rdi
  call Resource_dtor       # delete 第一步：析构
  movl $4, %esi
  movq %rbx, %rdi
  call operator_delete     # delete 第二步：释放
.L5:
  leaq -32(%rbp), %rax     # placement new：缓冲就在栈上
  movl $9, %esi
  movq %rax, %rdi
  call Resource_ctor       # 不经分配，直接构造进缓冲
  movl -32(%rbp), %eax     # 读出 9
  movl $0, %eax
  movq -8(%rbp), %rbx
  leave
  ret
