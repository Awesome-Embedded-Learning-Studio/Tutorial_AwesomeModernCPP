# add(3, 4)：一次完整的函数调用。-O0 编译器产物的等价汇编，标签去掉名字修饰。
# _start（boot.s）建好固定栈后 jmp 进入 main；教学止于 __stop 的 int3 守卫之前。
.text
.globl add
.type add, @function
add:
  pushq %rbp                # 保存调用者的栈帧
  movq %rsp, %rbp           # 建立 add 的栈帧
  movl %edi, -4(%rbp)       # 第一个参数 a=3 落栈
  movl %esi, -8(%rbp)       # 第二个参数 b=4 落栈
  movl -4(%rbp), %edx
  movl -8(%rbp), %eax
  addl %edx, %eax           # a + b
  popq %rbp                 # 恢复调用者栈帧
  ret                       # 返回值 7 在 eax
.size add, .-add

.globl main
.type main, @function
main:
  pushq %rbp
  movq %rsp, %rbp           # 建立 main 的栈帧
  subq $16, %rsp            # 给局部变量留 16 字节
  movl $4, %esi             # 第二个实参 4
  movl $3, %edi             # 第一个实参 3
  call add                  # 返回地址入栈，跳进 add
  movl %eax, -4(%rbp)       # 结果 s = 7 落栈
  movl -4(%rbp), %eax       # main 的返回值
  leave                     # 撤栈帧：rsp=rbp，再 pop rbp
  ret
.size main, .-main
.section .note.GNU-stack,"",@progbits
