/*
 * arch/arm64/kernel/entry-ftrace.S
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/ftrace.h>
#include <asm/insn.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

/*
 * Gcc with -pg will put the following code in the beginning of each function:
 *      mov x0, x30
 *      bl _mcount
 *	[function's body ...]
 * "bl _mcount" may be replaced to "bl ftrace_caller" or NOP if dynamic
 * ftrace is enabled.
 *
 * Please note that x0 as an argument will not be used here because we can
 * get lr(x30) of instrumented function at any time by winding up call stack
 * as long as the kernel is compiled without -fomit-frame-pointer.
 * (or CONFIG_FRAME_POINTER, this is forced on arm64)
 *
 * stack layout after mcount_enter in _mcount():
 *
 * current sp/fp =>  0:+-----+
 * in _mcount()        | x29 | -> instrumented function's fp
 *                     +-----+
 *                     | x30 | -> _mcount()'s lr (= instrumented function's pc)
 * old sp       => +16:+-----+
 * when instrumented   |     |
 * function calls      | ... |
 * _mcount()           |     |
 *                     |     |
 * instrumented => +xx:+-----+
 * function's fp       | x29 | -> parent's fp
 *                     +-----+
 *                     | x30 | -> instrumented function's lr (= parent's pc)
 *                     +-----+
 *                     | ... |
 */

	.macro mcount_enter
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	.endm

	/* save parameter registers & corruptible registers */
	.macro save_mcount_regs
	sub	sp, sp, #S_FRAME_SIZE
	stp	x0, x1, [sp]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	str	x30, [sp, #S_LR]
	.endm

	.macro mcount_exit
	ldp	x29, x30, [sp], #16
	ret
	.endm

	/* restore parameter registers & corruptible registers */
	.macro restore_mcount_regs
	ldp	x0, x1, [sp]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldr	x30, [sp, #S_LR]
	add	sp, sp, #S_FRAME_SIZE
	.endm

	.macro mcount_adjust_addr rd, rn
	sub	\rd, \rn, #AARCH64_INSN_SIZE
	.endm

	/* for instrumented function's parent */
	.macro mcount_get_parent_fp reg
	ldr	\reg, [x29]
	ldr	\reg, [\reg]
	.endm

	/* for instrumented function's parent */
	.macro fentry_get_parent_fp reg
	ldr	\reg, [x29]
	.endm

	/* for instrumented function */
	.macro mcount_get_pc0 reg
	mcount_adjust_addr	\reg, x30
	.endm

	/* for instrumented function */
	.macro fentry_get_pc0 reg
	mcount_adjust_addr	\reg, x30
	.endm

	.macro mcount_get_pc reg
	ldr	\reg, [x29, #8]
	mcount_adjust_addr	\reg, \reg
	.endm

	.macro fentry_get_pc reg, base
	ldr	\reg, [\base, #S_LR]
	mcount_adjust_addr	\reg, \reg
	.endm

	.macro mcount_get_lr reg
	ldr	\reg, [x29]
	ldr	\reg, [\reg, #8]
	mcount_adjust_addr	\reg, \reg
	.endm

	.macro fentry_get_lr reg, base
	ldr	\reg, [\base, #72]	//S_X9
	mcount_adjust_addr	\reg, \reg
	.endm

	.macro mcount_get_lr_addr reg
	ldr	\reg, [x29]
	add	\reg, \reg, #8
	.endm

	.macro fentry_get_lr_addr reg, base
	add	\reg, \base, #72	//S_X9
	.endm

#ifdef	CC_USING_FENTRY
#define	function_hook	__fentry__
#else
#define	function_hook	_mcount
#endif

#ifndef CONFIG_DYNAMIC_FTRACE
/*
 * void _mcount(unsigned long return_address)
 * @return_address: return address to instrumented function
 *
 * This function makes calls, if enabled, to:
 *     - tracer function to probe instrumented function's entry,
 *     - ftrace_graph_caller to set up an exit hook
 */
ENTRY(function_hook)
	mcount_enter
	save_mcount_regs

	adrp	x0, ftrace_trace_function
	ldr	x2, [x0, #:lo12:ftrace_trace_function]
	adr	x0, ftrace_stub
	cmp	x0, x2			// if (ftrace_trace_function
	b.eq	skip_ftrace_call	//     != ftrace_stub) {

#ifdef CC_USING_FENTRY
	fentry_get_pc	x0, sp		//       function's pc
	fentry_get_lr	x1, sp		//       function's lr (= parent's pc)
#else
	mcount_get_pc	x0		//       function's pc
	mcount_get_lr	x1		//       function's lr (= parent's pc)
#endif
	blr	x2			//   (*ftrace_trace_function)(pc, lr);

#ifndef CONFIG_FUNCTION_GRAPH_TRACER
skip_ftrace_call:			//   return;
	restore_mcount_regs
	mcount_exit			// }
#else
	restore_mcount_regs
	mcount_exit			//   return;
					// }
skip_ftrace_call:
	adrp	x1, ftrace_graph_return
	ldr	x2, [x1, #:lo12:ftrace_graph_return]
	cmp	x0, x2			//   if ((ftrace_graph_return
	b.ne	ftrace_graph_caller	//        != ftrace_stub)

	adrp	x1, ftrace_graph_entry	//     || (ftrace_graph_entry
	adrp	x0, ftrace_graph_entry_stub //     != ftrace_graph_entry_stub))
	ldr	x2, [x1, #:lo12:ftrace_graph_entry]
	add	x0, x0, #:lo12:ftrace_graph_entry_stub
	cmp	x0, x2
	b.ne	ftrace_graph_caller	//     ftrace_graph_caller();

	restore_mcount_regs
	mcount_exit
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
ENDPROC(function_hook)

#else /* CONFIG_DYNAMIC_FTRACE */
/*
 * _mcount() is used to build the kernel with -pg option, but all the branch
 * instructions to _mcount() are replaced to NOP initially at kernel start up,
 * and later on, NOP to branch to ftrace_caller() when enabled or branch to
 * NOP when disabled per-function base.
 */
ENTRY(function_hook)
	ret
ENDPROC(function_hook)

/*
 * void ftrace_caller(unsigned long return_address)
 * @return_address: return address to instrumented function
 *
 * This function is a counterpart of _mcount() in 'static' ftrace, and
 * makes calls to:
 *     - tracer function to probe instrumented function's entry,
 *     - ftrace_graph_caller to set up an exit hook
 */
ENTRY(ftrace_caller)
	mcount_enter
	save_mcount_regs

	adrp	x0, function_trace_op
	ldr	x2, [x0, #:lo12:function_trace_op]
#ifdef CC_USING_FENTRY
	fentry_get_pc0	x0		//     function's pc
	fentry_get_lr	x1, sp		//     function's lr
#else
	mcount_get_pc0	x0		//     function's pc
	mcount_get_lr	x1		//     function's lr
#endif
	mov	x3, #0

	.global ftrace_call
ftrace_call:				// tracer(pc, lr);
	nop				// This will be replaced with "bl xxx"
					// where xxx can be any kind of tracer.

ftrace_return:
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.global ftrace_graph_call
ftrace_graph_call:			// ftrace_graph_caller();
	nop				// If enabled, this will be replaced
					// "b ftrace_graph_caller"
#endif

	restore_mcount_regs
	mcount_exit
ENDPROC(ftrace_caller)

ENTRY(ftrace_regs_caller)
	mcount_enter
	save_mcount_regs

	/* Save the rest of pt_regs */
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]
	str	x30, [sp, #S_LR]

	/* Save sp before profile calling */
	add	x9, sp, #S_FRAME_SIZE + 16
	str	x9, [sp, #S_SP]

	/* PC of pt_regs saving lr, and can be changed by handler */
	str x30, [sp, #S_PC]

	/* Save flags */
	mrs	x9, spsr_el1
	str	x9, [sp, #S_PSTATE]

	adrp	x0, function_trace_op
	ldr	x2, [x0, #:lo12:function_trace_op]
#ifdef CC_USING_FENTRY
	fentry_get_pc0	x0		//     function's pc
	fentry_get_lr	x1, sp		//     function's lr
#else
	mcount_get_pc0	x0		//     function's pc
	mcount_get_lr	x1		//     function's lr
#endif
	mov	x3, sp

	.global ftrace_regs_call
ftrace_regs_call:			// tracer(pc, lr);
	nop				// This will be replaced with "bl xxx"
					// where xxx can be any kind of tracer.
	/* Handlers can change the PC */
	ldr	x9, [sp, #S_PC]
	str	x9, [x29, #8]

	/* Restore the rest of pt_regs */
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldr	x28, [sp, #224]
	/* x29 & x30 should be restored by mcount_exit*/

	/* Restore flags */
	ldr	x9, [sp, #S_PSTATE]
	msr	spsr_el1, x9

	b	ftrace_return
ENDPROC(ftrace_regs_caller)
#endif /* CONFIG_DYNAMIC_FTRACE */

ENTRY(ftrace_stub)
	ret
ENDPROC(ftrace_stub)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* save return value regs*/
	.macro save_return_regs
	sub sp, sp, #64
	stp x0, x1, [sp]
	stp x2, x3, [sp, #16]
	stp x4, x5, [sp, #32]
	stp x6, x7, [sp, #48]
	.endm

	/* restore return value regs*/
	.macro restore_return_regs
	ldp x0, x1, [sp]
	ldp x2, x3, [sp, #16]
	ldp x4, x5, [sp, #32]
	ldp x6, x7, [sp, #48]
	add sp, sp, #64
	.endm

/*
 * void ftrace_graph_caller(void)
 *
 * Called from _mcount() or ftrace_caller() when function_graph tracer is
 * selected.
 * This function w/ prepare_ftrace_return() fakes link register's value on
 * the call stack in order to intercept instrumented function's return path
 * and run return_to_handler() later on its exit.
 */
ENTRY(ftrace_graph_caller)
#ifdef CC_USING_FENTRY
	fentry_get_lr_addr	x0, sp	//     pointer to function's saved lr
	fentry_get_pc		x1, sp	//     function's pc
	fentry_get_parent_fp	x2	//     parent's fp
#else
	mcount_get_lr_addr	  x0	//     pointer to function's saved lr
	mcount_get_pc		  x1	//     function's pc
	mcount_get_parent_fp	  x2	//     parent's fp
#endif
	bl	prepare_ftrace_return	// prepare_ftrace_return(&lr, pc, fp)

	restore_mcount_regs
	mcount_exit
ENDPROC(ftrace_graph_caller)

/*
 * void return_to_handler(void)
 *
 * Run ftrace_return_to_handler() before going back to parent.
 * @fp is checked against the value passed by ftrace_graph_caller()
 * only when CONFIG_HAVE_FUNCTION_GRAPH_FP_TEST is enabled.
 */
ENTRY(return_to_handler)
	save_return_regs
	mov	x0, x29			//     parent's fp
	bl	ftrace_return_to_handler// addr = ftrace_return_to_hander(fp);
	mov	x30, x0			// restore the original return address
	restore_return_regs
	ret
END(return_to_handler)
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
