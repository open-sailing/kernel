/*
 * arch/arm64/kernel/kprobes.c
 *
 * Kprobes support for ARM64
 *
 * Copyright (C) 2013 Linaro Limited.
 * Author: Sandeepa Prabhu <sandeepa.prabhu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/stringify.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>
#include <asm/system_misc.h>
#include <asm/fixmap.h>
#include <asm/insn.h>
#include <linux/uaccess.h>
#include <asm-generic/sections.h>

#include "kprobes.h"
#include "kprobes-arm64.h"

static int modifying_code __read_mostly;
static DEFINE_PER_CPU(int, save_modifying_code);

#define MIN_STACK_SIZE(addr)	min((unsigned long)MAX_STACK_SIZE,	\
	(unsigned long)current_thread_info() + THREAD_START_SP - (addr))

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

static void __kprobes
post_kprobe_handler(struct kprobe_ctlblk *, struct pt_regs *);

static void __kprobes arch_prepare_ss_slot(struct kprobe *p)
{
	/* prepare insn slot */
	p->ainsn.insn[0] = cpu_to_le32(p->opcode);

	flush_icache_range((uintptr_t) (p->ainsn.insn),
			   (uintptr_t) (p->ainsn.insn) + MAX_INSN_SIZE);

	/*
	 * Needs restoring of return address after stepping xol.
	 */
	p->ainsn.restore.addr = (unsigned long) p->addr +
	  sizeof(kprobe_opcode_t);
	p->ainsn.restore.type = RESTORE_PC;
}

static void __kprobes arch_prepare_simulate(struct kprobe *p)
{
	if (p->ainsn.prepare)
		p->ainsn.prepare(p->opcode, &p->ainsn);

	/* This instructions is not executed xol. No need to adjust the PC */
	p->ainsn.restore.addr = 0;
	p->ainsn.restore.type = NO_RESTORE;
}

static void __kprobes arch_simulate_insn(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (p->ainsn.handler)
		p->ainsn.handler((u32)p->opcode, (long)p->addr, regs);

	/* single step simulated, now go for post processing */
	post_kprobe_handler(kcb, regs);
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	kprobe_opcode_t insn;
	unsigned long probe_addr = (unsigned long)p->addr;
	extern char __start_rodata[];
	extern char __end_rodata[];

	if (probe_addr & 0x3)
		return -EINVAL;

	/* copy instruction */
	insn = le32_to_cpu(*p->addr);
	p->opcode = insn;

	if (in_exception_text(probe_addr))
		return -EINVAL;

	if (probe_addr >= (unsigned long)__start_rodata &&
		probe_addr <= (unsigned long)__end_rodata)
			return -EINVAL;

	/* decode instruction */
	switch (arm_kprobe_decode_insn(insn, &p->ainsn)) {
	case INSN_REJECTED:	/* insn not supported */
		return -EINVAL;

	case INSN_GOOD_NO_SLOT:	/* insn need simulation */
		p->ainsn.insn = NULL;
		break;

	case INSN_GOOD:	/* instruction uses slot */
		p->ainsn.insn = get_insn_slot();
		if (!p->ainsn.insn)
			return -ENOMEM;
		break;
	};

	/* prepare the instruction */
	if (p->ainsn.insn)
		arch_prepare_ss_slot(p);
	else
		arch_prepare_simulate(p);

	return 0;
}

#define MOD_CODE_WRITE_FLAG (1 << 31)  /* set when NMI should do the write */
static atomic_t nmi_running = ATOMIC_INIT(0);
static int mod_code_status;            /* holds return value of text write */
static void *mod_code_ip;              /* holds the IP to write to */
static const void *mod_code_newcode;   /* holds the text to write to the IP */
static unsigned nmi_wait_count;
static atomic_t nmi_update_count = ATOMIC_INIT(0);

struct aarch64_kprobe_mod {
	void        **text_addrs;
	u32     *new_insns;
	int     insn_cnt;
	atomic_t    cpu_count;
};

static void clear_mod_flag(void)
{
	int old = atomic_read(&nmi_running);

	for (;;) {
		int new = old & ~MOD_CODE_WRITE_FLAG;

		if (old == new)
			break;

		old = atomic_cmpxchg(&nmi_running, old, new);
	}
}

static void __kprobes *patch_map(void *addr, int fixmap)
{
	unsigned long uintaddr = (uintptr_t) addr;
	bool module = !core_kernel_text(uintaddr);
	struct page *page;

	if (module && IS_ENABLED(CONFIG_DEBUG_SET_MODULE_RONX))
		page = vmalloc_to_page(addr);
	else if (!module && IS_ENABLED(CONFIG_DEBUG_RODATA))
		page = virt_to_page(addr);
	else
		return addr;

	BUG_ON(!page);
	return (void *)set_fixmap_offset(fixmap, page_to_phys(page) +
			(uintaddr & ~PAGE_MASK));
}

static void __kprobes patch_unmap(int fixmap)
{
	clear_fixmap(fixmap);
}

static void __kprobes aarch64_kprobe_mod_code(void)
{
	void *waddr = patch_map(mod_code_ip, FIX_TEXT_POKE0);
	mod_code_status = probe_kernel_write(waddr, mod_code_newcode, AARCH64_INSN_SIZE);
	patch_unmap(FIX_TEXT_POKE0);

	if (mod_code_status)
		clear_mod_flag();
}

void kprobes_nmi_enter(void)
{
	__this_cpu_write(save_modifying_code, modifying_code);

	if (!__this_cpu_read(save_modifying_code))
		return;

	if (atomic_inc_return(&nmi_running) & MOD_CODE_WRITE_FLAG) {
		smp_rmb();
		aarch64_kprobe_mod_code();
		flush_icache_range((uintptr_t)mod_code_ip,
				(uintptr_t)mod_code_ip + AARCH64_INSN_SIZE);
		atomic_inc(&nmi_update_count);
	}
	/* Must have previous changes seen before executions */
	smp_mb();
}

void kprobes_nmi_exit(void)
{
	if (!__this_cpu_read(save_modifying_code))
		return;

	/* Finish all executions before clearing nmi_running */
	smp_mb();
	atomic_dec(&nmi_running);
}

static void wait_for_nmi_and_set_mod_flag(void)
{
	if (!atomic_cmpxchg(&nmi_running, 0, MOD_CODE_WRITE_FLAG))
		return;

	do {
		cpu_relax();
	} while (atomic_cmpxchg(&nmi_running, 0, MOD_CODE_WRITE_FLAG));

	nmi_wait_count++;
}

static void wait_for_nmi(void)
{
	if (!atomic_read(&nmi_running))
		return;

	do {
		cpu_relax();
	} while (atomic_read(&nmi_running));

	nmi_wait_count++;
}

static int __kprobes do_aarch64_kprobe_mod_code(void *addr, u32 insn)
{
	insn = cpu_to_le32(insn);

	mod_code_ip = patch_map(addr, FIX_TEXT_POKE0);
	mod_code_newcode = &insn;

	smp_mb();

	wait_for_nmi_and_set_mod_flag();

	smp_mb();

	aarch64_kprobe_mod_code();

	smp_mb();

	clear_mod_flag();
	wait_for_nmi();

	return mod_code_status;
}

static int __kprobes aarch64_kprobe_patch_text_nosync(void *addr, u32 insn)
{
	u32 *tp = addr;
	int ret;

	/* A64 instructions must be word aligned */
	if ((uintptr_t)tp & 0x3)
		return -EINVAL;

	ret = do_aarch64_kprobe_mod_code(tp, insn);
	if (ret == 0)
		flush_icache_range((uintptr_t)tp,
			(uintptr_t)tp + AARCH64_INSN_SIZE);

	return ret;
}

struct aarch64_kprobe_patch {
	void        **text_addrs;
	u32     *new_insns;
	int     insn_cnt;
	atomic_t    cpu_count;
};

static int __kprobes aarch64_kprobe_patch_text_cb(void *arg)
{
	int i, ret = 0;
	struct aarch64_kprobe_patch *pp = arg;

	/* The first CPU becomes master */
	if (atomic_inc_return(&pp->cpu_count) == 1) {
		for (i = 0; ret == 0 && i < pp->insn_cnt; i++)
			ret = aarch64_kprobe_patch_text_nosync(pp->text_addrs[i],
					pp->new_insns[i]);
		/*
		 * aarch64_insn_patch_text_nosync() calls flush_icache_range(),
		 * which ends with "dsb; isb" pair guaranteeing global
		 * visibility.
		 */
		/* Notify other processors with an additional increment. */
		atomic_inc(&pp->cpu_count);
	} else {
		while (atomic_read(&pp->cpu_count) <= num_online_cpus())
			cpu_relax();
		isb();
	}

	return ret;
}

static int __kprobes patch_text(kprobe_opcode_t *addr, u32 opcode)
{
	void *addrs[1];
	u32 insns[1];

	struct aarch64_kprobe_patch patch = {
		.text_addrs = addrs,
		.new_insns = insns,
		.insn_cnt = 1,
		.cpu_count = ATOMIC_INIT(0),
	};

	patch.text_addrs[0] = (void *)addr;
	patch.new_insns[0] = (u32)opcode;

	return stop_machine(aarch64_kprobe_patch_text_cb, &patch,
			cpu_online_mask);
}

/* arm kprobe: install breakpoint in text */
void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	modifying_code = 1;
	patch_text(p->addr, BRK64_OPCODE_KPROBES);
	modifying_code = 0;
}

/* disarm kprobe: remove breakpoint from text */
void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	modifying_code = 1;
	patch_text(p->addr, p->opcode);
	modifying_code = 0;
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, 0);
		p->ainsn.insn = NULL;
	}
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, kcb->prev_kprobe.kp);
	kcb->kprobe_status = kcb->prev_kprobe.status;
}

static void __kprobes set_current_kprobe(struct kprobe *p)
{
	__this_cpu_write(current_kprobe, p);
}

/*
 * The D-flag (Debug mask) is set (masked) upon exception entry.
 * Kprobes needs to clear (unmask) D-flag -ONLY- in case of recursive
 * probe i.e. when probe hit from kprobe handler context upon
 * executing the pre/post handlers. In this case we return with
 * D-flag clear so that single-stepping can be carried-out.
 *
 * Leave D-flag set in all other cases.
 */
static void __kprobes
spsr_set_debug_flag(struct pt_regs *regs, int mask)
{
	unsigned long spsr = regs->pstate;

	if (mask)
		spsr |= PSR_D_BIT;
	else
		spsr &= ~PSR_D_BIT;

	regs->pstate = spsr;
}

/*
 * Interrupts need to be disabled before single-step mode is set, and not
 * reenabled until after single-step mode ends.
 * Without disabling interrupt on local CPU, there is a chance of
 * interrupt occurrence in the period of exception return and  start of
 * out-of-line single-step, that result in wrongly single stepping
 * the interrupt handler.
 */
static void __kprobes kprobes_save_local_irqflag(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	kcb->saved_irqflag = regs->pstate;
	regs->pstate |= PSR_I_BIT;
}

static void __kprobes kprobes_restore_local_irqflag(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (kcb->saved_irqflag & PSR_I_BIT)
		regs->pstate |= PSR_I_BIT;
	else
		regs->pstate &= ~PSR_I_BIT;
}

static void __kprobes
set_ss_context(struct kprobe_ctlblk *kcb, unsigned long addr)
{
	kcb->ss_ctx.ss_status = KPROBES_STEP_PENDING;
	kcb->ss_ctx.match_addr = addr + sizeof(kprobe_opcode_t);
}

static void __kprobes clear_ss_context(struct kprobe_ctlblk *kcb)
{
	kcb->ss_ctx.ss_status = KPROBES_STEP_NONE;
	kcb->ss_ctx.match_addr = 0;
}

static void __kprobes
skip_singlestep_missed(struct kprobe_ctlblk *kcb, struct pt_regs *regs)
{
	/* set return addr to next pc to continue */
	instruction_pointer_set(regs,
			instruction_pointer(regs) + sizeof(kprobe_opcode_t));
}

static void __kprobes setup_singlestep(struct kprobe *p,
				       struct pt_regs *regs,
				       struct kprobe_ctlblk *kcb, int reenter)
{
	unsigned long slot;

	if (reenter) {
		save_previous_kprobe(kcb);
		set_current_kprobe(p);
		kcb->kprobe_status = KPROBE_REENTER;
	} else {
		kcb->kprobe_status = KPROBE_HIT_SS;
	}

	if (p->ainsn.insn) {
		/* prepare for single stepping */
		slot = (unsigned long)p->ainsn.insn;

		set_ss_context(kcb, slot);	/* mark pending ss */

		if (kcb->kprobe_status == KPROBE_REENTER)
			spsr_set_debug_flag(regs, 0);

		/* IRQs and single stepping do not mix well. */
		kprobes_save_local_irqflag(regs);
		kernel_enable_single_step(regs);
		instruction_pointer_set(regs, slot);
	} else	{
		/* insn simulation */
		arch_simulate_insn(p, regs);
	}
}

static int __kprobes reenter_kprobe(struct kprobe *p,
				    struct pt_regs *regs,
				    struct kprobe_ctlblk *kcb)
{
	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SSDONE:
	case KPROBE_HIT_ACTIVE:
		if (!p->ainsn.check_condn ||
			p->ainsn.check_condn((u32)p->opcode, &p->ainsn, regs)) {
			kprobes_inc_nmissed_count(p);
			setup_singlestep(p, regs, kcb, 1);
		} else	{
			/* condition check failed, skip stepping */
			skip_singlestep_missed(kcb, regs);
		}
		break;
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		pr_warn("Unrecoverable kprobe detected at %p.\n", p->addr);
		dump_kprobe(p);
		BUG();
		break;
	default:
		WARN_ON(1);
		return 0;
	}

	return 1;
}

static void __kprobes
post_kprobe_handler(struct kprobe_ctlblk *kcb, struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();

	if (!cur)
		return;

	/* return addr restore if non-branching insn */
	if (cur->ainsn.restore.type == RESTORE_PC) {
		instruction_pointer_set(regs, cur->ainsn.restore.addr);
		if (!instruction_pointer(regs))
			BUG();
	}

	/* restore back original saved kprobe variables and continue */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		return;
	}
	/* call post handler */
	kcb->kprobe_status = KPROBE_HIT_SSDONE;
	if (cur->post_handler)	{
		/* post_handler can hit breakpoint and single step
		 * again, so we enable D-flag for recursive exception.
		 */
		cur->post_handler(cur, regs, 0);
	}

	reset_current_kprobe();
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, unsigned int fsr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the ip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		instruction_pointer_set(regs, (unsigned long)cur->addr);
		if (!instruction_pointer(regs))
			BUG();
		if (kcb->kprobe_status == KPROBE_REENTER)
			restore_previous_kprobe(kcb);
		else
			reset_current_kprobe();

		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * We increment the nmissed count for accounting,
		 * we can also use npre/npostfault count for accounting
		 * these specific fault cases.
		 */
		kprobes_inc_nmissed_count(cur);

		/*
		 * We come here because instructions in the pre/post
		 * handler caused the page_fault, this could happen
		 * if handler tries to access user space by
		 * copy_from_user(), get_user() etc. Let the
		 * user-specified handler try to fix it first.
		 */
		if (cur->fault_handler && cur->fault_handler(cur, regs, fsr))
			return 1;

		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		if (fixup_exception(regs))
			return 1;

		break;
	}
	return 0;
}

int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	return NOTIFY_DONE;
}

void __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p, *cur;
	struct kprobe_ctlblk *kcb;
	unsigned long addr = instruction_pointer(regs);

	kcb = get_kprobe_ctlblk();
	cur = kprobe_running();

	p = get_kprobe((kprobe_opcode_t *) addr);

	if (p) {
		if (cur) {
			if (reenter_kprobe(p, regs, kcb))
				return;
		} else if (!p->ainsn.check_condn ||
			p->ainsn.check_condn((u32)p->opcode, &p->ainsn, regs)) {
			/* Probe hit and conditional execution check ok. */
			set_current_kprobe(p);
			kcb->kprobe_status = KPROBE_HIT_ACTIVE;

			/*
			 * If we have no pre-handler or it returned 0, we
			 * continue with normal processing.  If we have a
			 * pre-handler and it returned non-zero, it prepped
			 * for calling the break_handler below on re-entry,
			 * so get out doing nothing more here.
			 *
			 * pre_handler can hit a breakpoint and can step thru
			 * before return, keep PSTATE D-flag enabled until
			 * pre_handler return back.
			 */
			if (!p->pre_handler || !p->pre_handler(p, regs)) {
				kcb->kprobe_status = KPROBE_HIT_SS;
				setup_singlestep(p, regs, kcb, 0);
				return;
			}
		} else {
			/*
			 * Breakpoint hit but conditional check failed,
			 * so just skip the instruction (NOP behaviour)
			 */
			skip_singlestep_missed(kcb, regs);
			return;
		}
	} else if (le32_to_cpu(*(kprobe_opcode_t *) addr) != BRK64_OPCODE_KPROBES) {
		/*
		 * The breakpoint instruction was removed right
		 * after we hit it.  Another cpu has removed
		 * either a probepoint or a debugger breakpoint
		 * at this address.  In either case, no further
		 * handling of this interrupt is appropriate.
		 * Return back to original instruction, and continue.
		 */
		return;
	} else if (cur) {
		/* We probably hit a jprobe.  Call its break handler. */
		if (cur->break_handler && cur->break_handler(cur, regs)) {
			kcb->kprobe_status = KPROBE_HIT_SS;
			setup_singlestep(cur, regs, kcb, 0);
			return;
		}
	} else {
		/* breakpoint is removed, now in a race
		 * Return back to original instruction & continue.
		 */
	}
}

static int __kprobes
kprobe_ss_hit(struct kprobe_ctlblk *kcb, unsigned long addr)
{
	if ((kcb->ss_ctx.ss_status == KPROBES_STEP_PENDING)
	    && (kcb->ss_ctx.match_addr == addr)) {
		clear_ss_context(kcb);	/* clear pending ss */
		return DBG_HOOK_HANDLED;
	}
	/* not ours, kprobes should ignore it */
	return DBG_HOOK_ERROR;
}

int __kprobes
kprobe_single_step_handler(struct pt_regs *regs, unsigned int esr)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	int retval;

	/* return error if this is not our step */
	retval = kprobe_ss_hit(kcb, instruction_pointer(regs));

	if (retval == DBG_HOOK_HANDLED) {
		kprobes_restore_local_irqflag(regs);
		kernel_disable_single_step(regs);

		if (kcb->kprobe_status == KPROBE_REENTER)
			spsr_set_debug_flag(regs, 1);

		post_kprobe_handler(kcb, regs);
	}

	return retval;
}

int __kprobes
kprobe_breakpoint_handler(struct pt_regs *regs, unsigned int esr)
{
	kprobe_handler(regs);
	return DBG_HOOK_HANDLED;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	long stack_ptr = stack_pointer(regs);

	kcb->jprobe_saved_regs = *regs;
	memcpy(kcb->jprobes_stack, (void *)stack_ptr,
	       MIN_STACK_SIZE(stack_ptr));

	instruction_pointer_set(regs, (long)jp->entry);
	preempt_disable();
	pause_graph_tracing();
	return 1;
}

void __kprobes jprobe_return(void)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	/*
	 * Jprobe handler return by entering break exception,
	 * encoded same as kprobe, but with following conditions
	 * -a magic number in x0 to identify from rest of other kprobes.
	 * -restore stack addr to original saved pt_regs
	 */
	asm volatile ("ldr x0, [%0]\n\t"
		      "mov sp, x0\n\t"
		      "ldr x0, =" __stringify(JPROBES_MAGIC_NUM) "\n\t"
		      "BRK %1\n\t"
		      "NOP\n\t"
		      :
		      : "r"(&kcb->jprobe_saved_regs.sp),
		      "I"(BRK64_ESR_KPROBES)
		      : "memory");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	long stack_addr = kcb->jprobe_saved_regs.sp;
	long orig_sp = stack_pointer(regs);
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	if (regs->regs[0] == JPROBES_MAGIC_NUM) {
		if (orig_sp != stack_addr) {
			struct pt_regs *saved_regs =
			    (struct pt_regs *)kcb->jprobe_saved_regs.sp;
			pr_err("current sp %lx does not match saved sp %lx\n",
			       orig_sp, stack_addr);
			pr_err("Saved registers for jprobe %p\n", jp);
			show_regs(saved_regs);
			pr_err("Current registers\n");
			show_regs(regs);
			BUG();
		}
		unpause_graph_tracing();
		*regs = kcb->jprobe_saved_regs;
		memcpy((void *)stack_addr, kcb->jprobes_stack,
		       MIN_STACK_SIZE(stack_addr));
		preempt_enable_no_resched();
		return 1;
	}
	return 0;
}

/*
 * When a retprobed function returns, this code saves registers and
 * calls trampoline_handler() runs, which calls the kretprobe's handler.
 */
static void __used __kprobes kretprobe_trampoline_holder(void)
{
	asm volatile (".global kretprobe_trampoline\n"
			"kretprobe_trampoline:\n"
			"sub sp, sp, %0\n"
			SAVE_REGS_STRING
			"mov x0, sp\n"
			"bl trampoline_probe_handler\n"
			/* Replace trampoline address in lr with actual
			   orig_ret_addr return address. */
			"str x0, [sp, #16 * 15]\n"
			RESTORE_REGS_STRING
			"add sp, sp, %0\n"
			"ret\n"
		      : : "I"(sizeof(struct pt_regs)) : "memory");
}

static void __kprobes __used *trampoline_probe_handler(struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *tmp;
	unsigned long flags, orig_ret_addr = 0;
	unsigned long trampoline_address =
		(unsigned long)&kretprobe_trampoline;

	INIT_HLIST_HEAD(&empty_rp);
	kretprobe_hash_lock(current, &head, &flags);

	/*
	 * It is possible to have multiple instances associated with a given
	 * task either because multiple functions in the call path have
	 * a return probe installed on them, and/or more than one return
	 * probe was registered for a target function.
	 *
	 * We can handle this because:
	 *     - instances are always inserted at the head of the list
	 *     - when multiple return probes are registered for the same
	 *       function, the first instance's ret_addr will point to the
	 *       real return address, and all the rest will point to
	 *       kretprobe_trampoline
	 */
	hlist_for_each_entry_safe(ri, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		if (ri->rp && ri->rp->handler) {
			__this_cpu_write(current_kprobe, &ri->rp->kp);
			get_kprobe_ctlblk()->kprobe_status = KPROBE_HIT_ACTIVE;
			ri->rp->handler(ri, regs);
			__this_cpu_write(current_kprobe, NULL);
		}

		orig_ret_addr = (unsigned long)ri->ret_addr;
		recycle_rp_inst(ri, &empty_rp);

		if (orig_ret_addr != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	kretprobe_assert(ri, orig_ret_addr, trampoline_address);
	/* restore the original return address */
	instruction_pointer_set(regs, orig_ret_addr);
	reset_current_kprobe();
	kretprobe_hash_unlock(current, &flags);

	hlist_for_each_entry_safe(ri, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}

	/* return 1 so that post handlers not called */
	return (void *) orig_ret_addr;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->regs[30];

	/* replace return addr (x30) with trampoline */
	regs->regs[30] = (long)&kretprobe_trampoline;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}

bool __kprobes arch_within_kprobe_blacklist(unsigned long addr)
{
	return  (addr >= (unsigned long)__kprobes_text_start &&
		addr < (unsigned long)__kprobes_text_end) ||
		(addr >= (unsigned long)__entry_text_start &&
		 addr < (unsigned long)__entry_text_end);
}

int __init arch_init_kprobes(void)
{
	return 0;
}
