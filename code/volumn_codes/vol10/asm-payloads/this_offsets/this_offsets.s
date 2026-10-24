# 01-this-pointer-and-member-offsets 的等价场景：成员函数与自由函数同场对照。
# this 走 rdi，x 在 this+0、y 在 this+4。p 初值 (10, 20)，文章未指定，这里取整方便心算。
# main 末尾把 p.x 装进 eax 返回，让终态可观察（18 = 10 + 3 + 5）。
.text
.globl translate
translate:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)      # rdi 里的 this 落栈
  movl %esi, -12(%rbp)     # dx
  movl %edx, -16(%rbp)     # dy
  movq -8(%rbp), %rax
  movl (%rax), %edx        # x 在 this+0
  movl -12(%rbp), %eax
  addl %eax, %edx
  movq -8(%rbp), %rax
  movl %edx, (%rax)        # x += dx 写回 this+0
  movq -8(%rbp), %rax
  movl 4(%rax), %edx       # y 在 this+4
  movl -16(%rbp), %eax
  addl %eax, %edx
  movq -8(%rbp), %rax
  movl %edx, 4(%rax)       # y += dy 写回 this+4
  nop
  popq %rbp
  ret

.globl point_translate
point_translate:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)      # 自由函数版本：Point* 与 this 同一个位置
  movl %esi, -12(%rbp)
  movl %edx, -16(%rbp)
  movq -8(%rbp), %rax
  movl (%rax), %edx
  movl -12(%rbp), %eax
  addl %eax, %edx
  movq -8(%rbp), %rax
  movl %edx, (%rax)
  movq -8(%rbp), %rax
  movl 4(%rax), %edx
  movl -16(%rbp), %eax
  addl %eax, %edx
  movq -8(%rbp), %rax
  movl %edx, 4(%rax)
  nop
  popq %rbp
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  movl $10, -16(%rbp)      # p.x = 10，落在 rbp-16
  movl $20, -12(%rbp)      # p.y = 20，p+4 = rbp-12
  leaq -16(%rbp), %rax
  movl $4, %edx            # dy = 4
  movl $3, %esi            # dx = 3
  movq %rax, %rdi          # this = &p
  call translate
  leaq -16(%rbp), %rax
  movl $6, %edx
  movl $5, %esi
  movq %rax, %rdi
  call point_translate
  movl -16(%rbp), %eax     # 返回 p.x = 18
  leave
  ret
