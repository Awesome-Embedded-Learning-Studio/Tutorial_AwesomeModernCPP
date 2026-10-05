	call	fflush@PLT
	movl	g_flag(%rip), %edx
	xorl	%esi, %esi
	testl	%edx, %edx
	jne	.L4
	.p2align 4
	.p2align 4
	.p2align 3
.L5:
	movl	g_flag(%rip), %eax
	addq	$1, %rsi
	testl	%eax, %eax
	je	.L5
.L4:
