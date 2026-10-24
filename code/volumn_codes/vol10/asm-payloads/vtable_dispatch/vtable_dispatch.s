# 03-virtual-functions-and-vtables 的等价场景：同一个调用点 call_who 走两次，
# 分别分派到 Base::who 与 Derived::who。①装载 vptr ②取槽位 ③call *%rdx 与文章一一对应。
# 简化处：真实 vtable 前两个槽是 offset-to-top 与 RTTI，vptr 指向表 +16；
# 这里表首即槽 0，教学主线不受影响。
.data
.globl vtable_base
vtable_base:    .quad base_who
.globl vtable_derived
vtable_derived: .quad derived_who
.globl observed
observed:       .quad 0

.text
base_who:
  pushq %rbp
  movq %rsp, %rbp
  mov $1, %rax
  popq %rbp
  ret

derived_who:
  pushq %rbp
  movq %rsp, %rbp
  mov $2, %rax
  popq %rbp
  ret

.globl call_who
call_who:
  pushq %rbp
  movq %rsp, %rbp
  subq $32, %rsp
  movq %rdi, -24(%rbp)      # p 落栈
  movq -24(%rbp), %rax
  movq (%rax), %rax         # ① 装载 vptr：p+0 处的 8 字节
  movq (%rax), %rdx         # ② 取槽位：vptr+0 → rdx
  movq -24(%rbp), %rax
  movq %rax, %rdi           # this 作为第一个实参 → rdi
  call *%rdx                # ③ 间接 call：目标来自寄存器
  movq %rax, -8(%rbp)
  movq -8(%rbp), %rax
  movq %rax, observed(%rip) # volatile 落点
  movq -8(%rbp), %rax
  leave
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  leaq vtable_base(%rip), %rax
  movq %rax, -8(%rbp)       # b 的 vptr
  leaq vtable_derived(%rip), %rax
  movq %rax, -16(%rbp)      # d 的 vptr
  leaq -8(%rbp), %rax
  movq %rax, %rdi
  call call_who            # 同一个调用点，第一次 → base_who
  leaq -16(%rbp), %rax
  movq %rax, %rdi
  call call_who            # 同一个调用点，第二次 → derived_who
  movl $0, %eax
  leave
  ret
