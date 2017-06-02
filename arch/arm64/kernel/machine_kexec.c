/*
 * kexec for arm64
 *
 * Copyright (C) Linaro.
 * Copyright (C) Huawei Futurewei Technologies.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/libfdt_env.h>
#include <linux/of_fdt.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/nmi.h>

#include <asm/cacheflush.h>
#include <asm/hisi-llc.h>
#include <asm/system_misc.h>

#include "cpu-reset.h"

/* Global variables for the arm64_relocate_new_kernel routine.  */
extern const unsigned char arm64_relocate_new_kernel[];
extern const unsigned long arm64_relocate_new_kernel_size;

bool in_crash_kexec;
static unsigned long kimage_start;
static int pgtable_levels = CONFIG_PGTABLE_LEVELS;
static int va_bits= CONFIG_ARM64_VA_BITS;
static int page_shift = PAGE_SHIFT;

#ifdef CONFIG_ARCH_HISI
extern void gic_clear_zombie_irqs(void);
extern void check_cpu_status(void);
extern bool gic_need_clear_status(void);
#endif


/**
 * kexec_image_info - For debugging output.
 */
#define kexec_image_info(_i) _kexec_image_info(__func__, __LINE__, _i)
static void _kexec_image_info(const char *func, int line,
	const struct kimage *kimage)
{
	unsigned long i;

	pr_debug("%s:%d:\n", func, line);
	pr_debug("  kexec kimage info:\n");
	pr_debug("    type:        %d\n", kimage->type);
	pr_debug("    start:       %lx\n", kimage->start);
	pr_debug("    head:        %lx\n", kimage->head);
	pr_debug("    nr_segments: %lu\n", kimage->nr_segments);

	for (i = 0; i < kimage->nr_segments; i++) {
		pr_debug("      segment[%lu]: %016lx - %016lx, 0x%lx bytes, %lu pages\n",
			i,
			kimage->segment[i].mem,
			kimage->segment[i].mem + kimage->segment[i].memsz,
			kimage->segment[i].memsz,
			kimage->segment[i].memsz /  PAGE_SIZE);
	}
}

void machine_kexec_cleanup(struct kimage *kimage)
{
	/* Empty routine needed to avoid build errors. */
}

/**
 * machine_kexec_prepare - Prepare for a kexec reboot.
 *
 * Called from the core kexec code when a kernel image is loaded.
 */
int machine_kexec_prepare(struct kimage *kimage)
{
	kimage_start = kimage->start;
	kexec_image_info(kimage);

	return 0;
}

/**
 * kexec_list_flush - Helper to flush the kimage list and source pages to PoC.
 */
static void kexec_list_flush(struct kimage *kimage)
{
	kimage_entry_t *entry;

	for (entry = &kimage->head; ; entry++) {
		unsigned int flag;
		void *addr;

		/* flush the list entries. */
		__flush_dcache_area(entry, sizeof(kimage_entry_t));
		flag = *entry & IND_FLAGS;
		if (flag == IND_DONE)
			break;

		addr = phys_to_virt(*entry & PAGE_MASK);


		switch (flag) {
			case IND_INDIRECTION:
				/* Set entry point just before the new list page. */
				entry = (kimage_entry_t *)addr - 1;
				break;
			case IND_SOURCE:
				/* flush the source pages. */
				__flush_dcache_area(addr, PAGE_SIZE);
				break;
			case IND_DESTINATION:
				break;
			default:
				BUG();
		}

	}
}

/**
 * kexec_segment_flush - Helper to flush the kimage segments to PoC.
 */
static void kexec_segment_flush(const struct kimage *kimage)
{
	unsigned long i;

	pr_debug("%s:\n", __func__);

	for (i = 0; i < kimage->nr_segments; i++) {
		pr_debug("  segment[%lu]: %016lx - %016lx, %lx bytes, %lu pages\n",
			i,
			kimage->segment[i].mem,
			kimage->segment[i].mem + kimage->segment[i].memsz,
			kimage->segment[i].memsz,
			kimage->segment[i].memsz /  PAGE_SIZE);

		__flush_dcache_area(phys_to_virt(kimage->segment[i].mem),
			kimage->segment[i].memsz);
 	}
}

/**
 * machine_kexec - Do the kexec reboot.
 *
 * Called from the core kexec code for a sys_reboot with LINUX_REBOOT_CMD_KEXEC.
 */
void machine_kexec(struct kimage *kimage)
{
	phys_addr_t reboot_code_buffer_phys;
	void *reboot_code_buffer;

	BUG_ON((num_online_cpus() > 1) && !WARN_ON(in_crash_kexec));

	reboot_code_buffer_phys = page_to_phys(kimage->control_code_page);
	reboot_code_buffer = phys_to_virt(reboot_code_buffer_phys);

	kexec_image_info(kimage);

	pr_debug("%s:%d: control_code_page:        %p\n", __func__, __LINE__,
		kimage->control_code_page);
	pr_debug("%s:%d: reboot_code_buffer_phys:  %pa\n", __func__, __LINE__,
		&reboot_code_buffer_phys);
	pr_debug("%s:%d: reboot_code_buffer:       %p\n", __func__, __LINE__,
		reboot_code_buffer);
	pr_debug("%s:%d: relocate_new_kernel:      %p\n", __func__, __LINE__,
		arm64_relocate_new_kernel);
	pr_debug("%s:%d: relocate_new_kernel_size: 0x%lx(%lu) bytes\n",
		__func__, __LINE__, arm64_relocate_new_kernel_size,
		arm64_relocate_new_kernel_size);

	pr_debug("%s:%d: kimage_head:              %lx\n", __func__, __LINE__,
		kimage->head);
	pr_debug("%s:%d: kimage_start:             %lx\n", __func__, __LINE__,
		kimage_start);


	/*
	 * Copy arm64_relocate_new_kernel to the reboot_code_buffer for use
	 * after the kernel is shut down.
	 */
	memcpy(reboot_code_buffer, arm64_relocate_new_kernel,
		arm64_relocate_new_kernel_size);

	/* Set the variables in reboot_code_buffer. */

	/* Flush the reboot_code_buffer in preparation for its execution. */
	__flush_dcache_area(reboot_code_buffer, arm64_relocate_new_kernel_size);
	flush_icache_range((uintptr_t)reboot_code_buffer,
			   arm64_relocate_new_kernel_size);
	/* Flush the kimage list. */
	kexec_list_flush(kimage);

	/* Flush the new image if already in place. */
	if (kimage->head & IND_DONE)
		kexec_segment_flush(kimage);

	pr_info("Bye!\n");

	/* Disable all DAIF exceptions. */
	asm volatile ("msr daifset, #0xf" : : : "memory");

	/*
	 * cpu_soft_restart will shutdown the MMU, disable data caches, then
	 * transfer control to the reboot_code_buffer which contains a copy of
	 * the arm64_relocate_new_kernel routine.  arm64_relocate_new_kernel
	 * uses physical addressing to relocate the new image to its final
	 * position and transfers control to the image entry point when the
	 * relocation is complete.
	 */
	cpu_soft_restart(!in_crash_kexec, reboot_code_buffer_phys, kimage->head,
			 kimage_start, 0);
	BUG(); /* Should never get here. */
}

static void machine_kexec_mask_interrupts(void)
{
	unsigned int i;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		struct irq_chip *chip;
		int ret;

		chip = irq_desc_get_chip(desc);
		if (!chip)
			continue;


		/*
		 * First try to remove the active state. If this
		 * fails, try to EOI the interrupt.
		 */
		if (desc->irq_data.hwirq > 15 && desc->irq_data.hwirq < 32) {
			bool active = false;
			ret = irq_get_irqchip_state(i, IRQCHIP_STATE_ACTIVE, &active);
			if (ret) {
				pr_err("Get irq acitve state failed.\n");
			} else {
				if (active)
					chip->irq_eoi(&desc->irq_data);
			}
		}

		ret = irq_set_irqchip_state(i, IRQCHIP_STATE_ACTIVE, false);
		if (ret || gic_need_clear_status()) {
			if (irqd_irq_inprogress(&desc->irq_data)
			    && chip->irq_eoi) {
				pr_info("start to eoi virq %d, hwirq:%ld\n", i, desc->irq_data.hwirq);
				chip->irq_eoi(&desc->irq_data);
			}
		}

		if (chip->irq_mask)
			chip->irq_mask(&desc->irq_data);

		if (chip->irq_disable && !irqd_irq_disabled(&desc->irq_data))
			chip->irq_disable(&desc->irq_data);
	}
}

/*
 * take care the interrupts status
 * during the stop
 */
void handle_interrupts_status(void)
{
	machine_kexec_mask_interrupts();
#ifdef CONFIG_ARCH_HISI
	gic_clear_zombie_irqs();
#endif
}

/**
 * machine_crash_shutdown - shutdown non-boot cpus and save registers
 */
void machine_crash_shutdown(struct pt_regs *regs)
{
	local_irq_disable();

	in_crash_kexec = true;

	/* shutdown non-crashing cpus */
	smp_send_crash_stop();

#ifdef CONFIG_HISI_AARCH64_NMI
	nmi_set_active_state(smp_processor_id(), NMI_WATCHDOG_OFF);
#endif
	/* for crashing cpu */
	crash_save_cpu(regs, smp_processor_id());

	handle_interrupts_status();

	check_cpu_status();
	pr_info("Starting crashdump kernel...\n");
}

void arch_crash_save_vmcoreinfo(void)
{
	VMCOREINFO_NUMBER(pgtable_levels);
	VMCOREINFO_NUMBER(va_bits);
	VMCOREINFO_NUMBER(page_shift);
}
