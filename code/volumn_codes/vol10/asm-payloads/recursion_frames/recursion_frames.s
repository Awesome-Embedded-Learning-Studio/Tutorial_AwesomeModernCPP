# 03-kept-frames-and-stack-exhaustion 的等价场景（浅层演示）。
# fib 的 -O0 风格：每层递归开一个完整栈帧，call 压返回地址与 saved rbp，
# 栈窗口一层层向低地址叠上去。n=5 时 fib(5)=5，共 15 次 call。
# 文章里把栈写满的那档不做：max_steps 上限一万，撑不满真实 8 MiB 栈。
.text
.globl fib
fib:
  pushq %rbp
  movq %rsp, %rbp
  subq $32, %rsp
  movl %edi, -20(%rbp)      # 参数 n 落栈
  cmpl $1, -20(%rbp)
  jg .Lrec
  movl -20(%rbp), %eax      # n <= 1：直接返回 n
  jmp .Ldone
.Lrec:
  movl -20(%rbp), %eax
  subl $1, %eax
  movl %eax, -24(%rbp)      # n-1
  movl -24(%rbp), %eax
  movl %eax, %edi
  call fib                  # fib(n-1)，又一层帧
  movl %eax, -28(%rbp)      # 接住结果
  movl -20(%rbp), %eax
  subl $2, %eax
  movl %eax, -24(%rbp)      # n-2
  movl -24(%rbp), %eax
  movl %eax, %edi
  call fib                  # fib(n-2)
  addl -28(%rbp), %eax      # 两个结果相加
.Ldone:
  leave
  ret

.globl main
main:
  pushq %rbp
  movq %rsp, %rbp
  movl $5, %edi
  call fib                  # fib(5) = 5
  movl $0, %eax
  popq %rbp
  ret
