	call	fflush@PLT
	movl	g_flag(%rip), %eax
	testl	%eax, %eax
	jne	.L4
	.p2align 1
	.p2align 4
	.p2align 3
.L5:
	jmp	.L5
	.p2align 4,,10
	.p2align 3
.L4:
