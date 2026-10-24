# 02-hitting-canary-saved-rbp-and-return-address 的等价场景（去保护构建版）。
# arr 是 int 数组，arr[6] 按 4 字节步进正好落在 rbp+8 的返回地址槽上，
# 一次 8 字节写把 boom 的地址盖进去。f 返回时不回 main 的下一条指令，
# 而是跳进 boom。main 在 call f 之前预先压了 __stop_guard 的地址，
# 给 boom 的 ret 摆好出口，让这场越界可控地收场。
# 带金丝雀版本依赖 fs:0x28 段基址与 __stack_chk_fail，执行器不仿真，不在场景里。
.data
.globl g_payload
g_payload:  .quad boom
.globl g_index
g_index:    .long 6

.text
.globl boom
boom:
  mov $0x63, %eax          # 99：走这条路的人留下的记号
  ret

.globl f
f:
  pushq %rbp
  movq %rsp, %rbp
  subq $0x10, %rsp
  movl $1, -0x10(%rbp)     # arr[0..3]
  movl $2, -0xc(%rbp)
  movl $3, -8(%rbp)
  movl $4, -4(%rbp)
  movq g_payload(%rip), %rdx
  movl g_index(%rip), %eax
  cltq
  movq %rdx, -0x10(%rbp,%rax,4)   # arr[6] = g_payload，越界写盖住返回地址
  nop
  leave
  ret                      # 弹出的已不是 main 的返回地址

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  movabs $__stop_guard, %rax
  pushq %rax               # 给 boom 的 ret 预摆出口
  call f
  movl $0, %eax            # 正常路径永远到不了这里
  leave
  ret

.globl __stop_guard
__stop_guard:
  int3
