# 04-multivalue-return-and-channel-recap 的等价场景：pair<int,int> 打包进一个 rax。
# O0 版：局部构造 pair 再整块 movq 取出；O2 版：salq/orq 两条指令现场打包。
# 文章里的 fs:40 金丝雀段寻址执行器不仿真，场景里去掉。
.data
.globl sink_lo
sink_lo:  .long 0
.globl sink_hi
sink_hi:  .long 0

.text
.globl pair_ctor
pair_ctor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movq %rsi, -16(%rbp)
  movq -16(%rbp), %rax
  movl (%rax), %edx      # 读 first 的引用
  movq -8(%rbp), %rax
  movl %edx, (%rax)      # 写进 pair.first
  movq -16(%rbp), %rax
  movl 4(%rax), %edx     # second
  movq -8(%rbp), %rax
  movl %edx, 4(%rax)
  nop
  popq %rbp
  ret

.globl make_ipair_o0
make_ipair_o0:
  pushq %rbp
  movq %rsp, %rbp
  subq $32, %rsp
  movl %edi, -20(%rbp)
  movl %esi, -24(%rbp)
  leaq -24(%rbp), %rdx   # &second
  leaq -20(%rbp), %rcx   # &first
  leaq -16(%rbp), %rax
  movq %rcx, %rsi
  movq %rax, %rdi
  call pair_ctor         # 构造进 rbp-16
  movq -16(%rbp), %rax   # 整个 pair 一次装进 rax：低 32 位 first，高 32 位 second
  leave
  ret

.globl make_ipair_o2
make_ipair_o2:
  salq $32, %rsi         # second 移到高 32 位
  movl %edi, %eax        # first 落低 32 位
  orq %rsi, %rax         # 一次拼完
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  pushq %rbx
  movl $3, %esi
  movl $4, %edi
  call make_ipair_o0     # pair(3,4)
  movq %rax, %rbx
  movl %eax, sink_lo(%rip)     # 3
  shrq $32, %rbx
  movl %ebx, sink_hi(%rip)     # 4
  movl $30, %esi
  movl $40, %edi
  call make_ipair_o2     # pair(40,30)
  movq %rax, %rbx
  movl %eax, sink_lo(%rip)     # 40
  shrq $32, %rbx
  movl %ebx, sink_hi(%rip)     # 30
  movl $0, %eax
  popq %rbx
  popq %rbp
  ret
