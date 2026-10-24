# 01-stack-frame-neighbors 的等价场景：一次调用在栈上留下一整个排布。
# f 的帧从低到高：arr 四个 int、留白、金丝雀槽、saved rbp、返回地址。
# 真金丝雀读 fs:0x28，执行器不仿真段寻址，这里用 .data 里的固定值顶替，
# 写入与返回前的对比照做，布局的教学不受影响。printf 走 PLT，不在场景里。
.data
.globl g_canary
g_canary:  .quad 0x1122334455667788

.text
.globl f
f:
  pushq %rbp
  movq %rsp, %rbp
  subq $0x30, %rsp
  movq g_canary(%rip), %rax
  movq %rax, -8(%rbp)      # 金丝雀落进槽位
  movl $1, -0x20(%rbp)     # arr[0..3]，帧的低处
  movl $2, -0x1c(%rbp)
  movl $3, -0x18(%rbp)
  movl $4, -0x14(%rbp)
  movl -0x20(%rbp), %eax   # 求和 10，让 arr 的读回留下踪迹
  addl -0x1c(%rbp), %eax
  addl -0x18(%rbp), %eax
  addl -0x14(%rbp), %eax
  movl %eax, -0x24(%rbp)   # 局部 sum
  movq -8(%rbp), %rax      # 返回前对比金丝雀
  subq g_canary(%rip), %rax
  jne .Lbad
  movl -0x24(%rbp), %eax
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
  call f                   # 10：金丝雀没被动过，正常返回
  movl $0, %eax
  popq %rbp
  ret
