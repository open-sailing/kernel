/*
 * Copyright (C) 2014-2015 Pratyush Anand <panand@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_UPROBES_H
#define _ASM_UPROBES_H

#include <asm/debug-monitors.h>
#include <asm/insn.h>
#include <asm/probes.h>

#define MAX_UINSN_BYTES		AARCH64_INSN_SIZE

#define UPROBE_SWBP_INSN	BRK64_OPCODE_UPROBES
#define UPROBE_SWBP_INSN_SIZE	4
#define UPROBE_XOL_SLOT_BYTES	MAX_UINSN_BYTES

struct arch_uprobe_task {
	unsigned long saved_fault_code;
};

struct arch_uprobe {
	union {
		u8 insn[MAX_UINSN_BYTES];
		u8 ixol[MAX_UINSN_BYTES];
	};
	struct arch_specific_insn ainsn;
	bool simulate;
};

extern void flush_uprobe_xol_access(struct page *page, unsigned long uaddr,
		void *kaddr, unsigned long len);
#endif
