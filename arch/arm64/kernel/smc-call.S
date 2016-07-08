/*
 * Huawei driver
 *
 * Copyright (C)
 * Author: Huawei majun258@huawei.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/linkage.h>

#define SMC_CALLBACK_ID 0x83000001

	.text

	/* int smc_call(uint64_t cmd, uint64_t func); */

	.globl	smc_call
ENTRY(smc_call)
	smc	#0
	ret
ENDPROC(smc_call)


ENTRY(hisi_nmi_handler_swapper)
	/* save elr_el1, spsr_el1, esr_el1 in stack */
	mrs	x22, elr_el1
	mrs	x23, spsr_el1
	mrs	x24, esr_el1
	mrs	x25, mdscr_el1
	stp	x22, x23, [sp, #-16]!
	stp	x24, x25, [sp, #-16]!

	/* put a dummy stack */
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp

	/* call hisi_nmi_handler */
	bl	hisi_nmi_handler

	/* restore elr_el1, spsr_el1, esr_el1 */
	add	sp, sp, #16
	ldp	x24, x25, [sp], #16
	ldp	x22, x23, [sp], #16
	msr	mdscr_el1, x25
	msr	esr_el1, x24
	msr	spsr_el1, x23
	msr	elr_el1, x22

	/* smc to EL3 */
	ldr	x0, =SMC_CALLBACK_ID
	mov	x1, #0
	mov	x2, #0
	smc	#0
ENDPROC(hisi_nmi_handler_swapper)
