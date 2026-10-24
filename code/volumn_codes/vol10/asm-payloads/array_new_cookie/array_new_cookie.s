# 03-array-new-cookie-and-reverse-destruction 的等价场景。
# operator new[](20)：3*4 + 8 字节 cookie。块头 8 字节写元素个数，
# 首元素地址 = 块首 + 8。构造顺序从前往后，析构时读回 cookie，
# 从尾元素往前收。operator new[]/delete[] 用 .data 桩顶替。
.data
.globl heap_buf
heap_buf:   .space 32
.globl dtor_count
dtor_count: .long 0

.text
.globl operator_new_array
operator_new_array:
  movabs $heap_buf, %rax
  ret

.globl operator_delete
operator_delete:
  ret

.globl Widget_ctor
Widget_ctor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movq -8(%rbp), %rax
  movl $1, (%rax)        # id 占位写入
  nop
  popq %rbp
  ret

.globl Widget_dtor
Widget_dtor:
  pushq %rbp
  movq %rsp, %rbp
  movq %rdi, -8(%rbp)
  movl dtor_count(%rip), %eax
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
  subq $16, %rsp
  movl $20, %edi            # 3*4 + 8 字节 cookie，不是 12
  call operator_new_array
  movq %rax, %rbx
  movq $3, (%rbx)           # cookie：元素个数写进块头
  leaq 8(%rbx), %r12        # 数组首元素 = 块首 + 8
  movl $2, %r13d            # 游标下标从 2（最后一个）开始，正序构造用 jns 循环
  jmp .Lchk
.Lctor:
  movl %r13d, %eax
  cltq
  leaq (%r12,%rax,4), %rdi  # &arr[i]
  call Widget_ctor          # 正序：第 0 个先构造
  subl $1, %r13d
.Lchk:
  testl %r13d, %r13d
  jge .Lctor                # r13 >= 0 → 共 3 次
  movq %rbx, %rax
  addq $8, %rax
  movq %rax, -16(%rbp)      # 记下数组指针
  # delete[]：读回 cookie，逆序析构
  movq (%rbx), %rax         # cookie = 3
  subl $1, %eax             # 从下标 2 开始
  movl %eax, -12(%rbp)      # 下标进栈槽：eax 是调用者保存的，call 会冲掉它
  jmp .Ldchk
.Ldtor:
  movl -12(%rbp), %eax
  cltq
  movq %r12, %rdx
  leaq (%rdx,%rax,4), %rdi  # &arr[i]
  call Widget_dtor          # 逆序：最后一个先析构
  subl $1, -12(%rbp)
.Ldchk:
  cmpl $0, -12(%rbp)
  jge .Ldtor
  movl $20, %esi
  movq %rbx, %rdi
  call operator_delete      # delete[] 释放整块
  movl dtor_count(%rip), %eax   # 3：三次逆序析构都发生了
  movl $0, %eax
  movq -8(%rbp), %rbx
  leave
  ret
