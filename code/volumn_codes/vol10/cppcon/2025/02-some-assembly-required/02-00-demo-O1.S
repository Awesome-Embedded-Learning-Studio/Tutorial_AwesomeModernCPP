@ Notes: 这个文件是笔者顺手反汇编，在真实机器上汇编得到的机器码
@ 得到这份文件，笔者采用的办法是：
@ g++ -S -O1 -o 02-00-demo-O1.S 02-00-demo.cpp
@ 对于不同版本的编译器，这个结果可能略有不同！
	.file	"02-00-demo.cpp"
	.text
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	.cfi_startproc
	leal	(%rdi,%rsi), %eax
	ret
	.cfi_endproc
.LFE0:
	.size	_Z3addii, .-_Z3addii
	.globl	main
	.type	main, @function
main:
.LFB1:
	.cfi_startproc
	movl	$7, %eax @ 可以看到 -O1 的优化已经做到了constexpr的效果了
	ret
	.cfi_endproc
.LFE1:
	.size	main, .-main
	.ident	"GCC: (GNU) 16.1.1 20260430"
	.section	.note.GNU-stack,"",@progbits
