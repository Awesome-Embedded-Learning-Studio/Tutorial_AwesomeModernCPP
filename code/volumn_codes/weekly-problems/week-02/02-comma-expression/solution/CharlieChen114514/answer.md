笔者这里说两句。

因为这个题目算是一个讨论C范畴的题目了（当时记得是C范畴的，是不是C++我忘记了，很久之前跟owollz4聊过的）

答案最后的确是20，这一点可以自己去Godbolt试一试

```c
#include <cstdio>

int get_strange_result() {
    int x;
    int a = 10;
    int b = 20;

    x = (a++, b++);
    return x;
}


int main()
{
    printf("x = %d\n", get_strange_result());
}
```

```asm
# 记得开-O0优化，要不然的话汇编被压缩的太过密集
"get_strange_result()":
        push    rbp
        mov     rbp, rsp
        mov     DWORD PTR [rbp-4], 10
        mov     DWORD PTR [rbp-8], 20
        add     DWORD PTR [rbp-4], 1
        mov     eax, DWORD PTR [rbp-8] # 可以看到，这里我们压栈压入的是20的这个
        lea     edx, [rax+1]
        mov     DWORD PTR [rbp-8], edx
        mov     DWORD PTR [rbp-12], eax
        mov     eax, DWORD PTR [rbp-12]
        pop     rbp
        ret
.LC0:
        .string "x = %d\n"
"main":
        push    rbp
        mov     rbp, rsp
        call    "get_strange_result()"
        mov     esi, eax
        mov     edi, OFFSET FLAT:.LC0
        mov     eax, 0
        call    "printf"
        mov     eax, 0
        pop     rbp
        ret
```

但是笔者解答这个，没有任何兴趣争论这种虽然谈不上是UB，**但是丝毫没有任何可读性而言**的代码。说这个，也只是告诫一个事情。

> 不要写这种需要让人蒙一下，猜测您意图的代码。一行表达式从来只做一个事情。笔者的判定标准就是，如果代码存在明显的最朴实的路径，就干脆别写**不带来性能优化**的**不可读代码**，如果的确是需要，麻烦注释打满，为什么要怎么写，解决什么问题了。不要让后人对着你写的代码思考半天。
> 哦，黑心厂除外 :)
