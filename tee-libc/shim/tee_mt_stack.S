/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * void tee_mt_call_on_stack(void (*fn)(void *), void *arg, void *stack_top)
 *
 * Calls fn(arg) with sp = stack_top, then returns on the caller's stack. tee_mt.c runs the main
 * instance's computations on a stack in the shared pool this way: ggml keeps structures its worker
 * threads read (the compute plan) in the calling thread's stack frame, and the TA's own stack is
 * not mapped in worker instances.
 */
	.text
	.balign	4
	.global	tee_mt_call_on_stack
	.type	tee_mt_call_on_stack, %function
tee_mt_call_on_stack:
	.cfi_startproc
	stp	x29, x30, [sp, #-32]!
	.cfi_def_cfa_offset 32
	.cfi_offset x29, -32
	.cfi_offset x30, -24
	str	x19, [sp, #16]
	.cfi_offset x19, -16
	mov	x29, sp
	.cfi_def_cfa_register x29
	mov	x19, sp
	mov	sp, x2
	mov	x16, x0
	mov	x0, x1
	blr	x16
	mov	sp, x19
	ldr	x19, [sp, #16]
	ldp	x29, x30, [sp], #32
	.cfi_def_cfa sp, 0
	ret
	.cfi_endproc
	.size	tee_mt_call_on_stack, . - tee_mt_call_on_stack
