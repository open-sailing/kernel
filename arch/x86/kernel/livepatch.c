/*
 * livepatch.c - x86-specific Kernel Live Patching Core
 *
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 * Copyright (C) 2014 SUSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/livepatch.h>
#include <asm/cacheflush.h>
#include <asm/page_types.h>
#include <asm/elf.h>
#include <asm/livepatch.h>
#include <asm/stacktrace.h>

/**
 * klp_write_module_reloc() - write a relocation in a module
 * @mod:	module in which the section to be modified is found
 * @type:	ELF relocation type (see asm/elf.h)
 * @loc:	address that the relocation should be written to
 * @value:	relocation value (sym address + addend)
 *
 * This function writes a relocation to the specified location for
 * a particular module.
 */
int klp_write_module_reloc(struct module *mod, unsigned long type,
			   unsigned long loc, unsigned long value)
{
	int ret, numpages, size = 4;
	bool readonly;
	unsigned long val;
	unsigned long core = (unsigned long)mod->module_core;
	unsigned long core_size = mod->core_size;

	switch (type) {
	case R_X86_64_NONE:
		return 0;
	case R_X86_64_64:
		val = value;
		size = 8;
		break;
	case R_X86_64_32:
		val = (u32)value;
		break;
	case R_X86_64_32S:
		val = (s32)value;
		break;
	case R_X86_64_PC32:
		val = (u32)(value - loc);
		break;
	default:
		/* unsupported relocation type */
		return -EINVAL;
	}

	if (loc < core || loc >= core + core_size)
		/* loc does not point to any symbol inside the module */
		return -EINVAL;

	readonly = false;

#ifdef CONFIG_DEBUG_SET_MODULE_RONX
	if (loc < core + mod->core_ro_size)
		readonly = true;
#endif

	/* determine if the relocation spans a page boundary */
	numpages = ((loc & PAGE_MASK) == ((loc + size) & PAGE_MASK)) ? 1 : 2;

	if (readonly)
		set_memory_rw(loc & PAGE_MASK, numpages);

	ret = probe_kernel_write((void *)loc, &val, size);

	if (readonly)
		set_memory_ro(loc & PAGE_MASK, numpages);

	return ret;
}

struct walk_stackframe_args {
	struct klp_patch *patch;
	int enable;
	int ret;
};

static inline int klp_compare_address(unsigned long stack_addr, unsigned long func_addr,
					unsigned long func_size, const char *func_name)
{
	if (stack_addr >= func_addr && stack_addr < func_addr + func_size) {
		pr_err("func %s is in use!\n", func_name);
		return -EBUSY;
	}
	return 0;
}

static void klp_check_activeness_func(void *data, unsigned long address,
						int reliable)
{
	struct walk_stackframe_args *args = data;
	struct klp_patch *patch = args->patch;
	struct klp_object *obj;
	struct klp_func *func;
	unsigned long func_addr, func_size;
	const char *func_name;

	if (args->ret)
		return;

	for (obj = patch->objs; obj->funcs; obj++) {
		for (func = obj->funcs; func->old_name; func++) {
			if (args->enable) {
				func_addr = func->old_addr;
				func_size = func->old_size;
			} else {
				func_addr = (unsigned long)func->new_func;
				func_size = func->new_size;
			}
			func_name = func->old_name;
			args->ret = klp_compare_address(address, func_addr, func_size, func_name);
			if (args->ret)
				return;
		}
	}

	return;
}

static int klp_backtrace_stack(void *data, char *name)
{
	return 0;
}

static const struct stacktrace_ops klp_backtrace_ops = {
	.address = klp_check_activeness_func,
	.stack = klp_backtrace_stack,
	.walk_stack = print_context_stack_bp,
};

static void klp_print_trace_address(void *data, unsigned long addr, int reliable)
{
	if (reliable)
		pr_info("[<%p>] %pB\n", (void *)addr, (void *)addr);
}

static int klp_print_trace_stack(void *data, char *name)
{
	pr_cont(" <%s> ", name);
	return 0;
}

static const struct stacktrace_ops klp_print_trace_ops = {
	.address = klp_print_trace_address,
	.stack = klp_print_trace_stack,
	.walk_stack = print_context_stack,
};

int klp_check_calltrace(struct klp_patch *patch, int enable)
{
	struct task_struct *g, *t;
	int ret = 0;

	struct walk_stackframe_args args = {
		.patch = patch,
		.enable = enable,
		.ret = 0
	};

	do_each_thread(g, t) {
		dump_trace(t, NULL, NULL, 0, &klp_backtrace_ops, &args);
		if (args.ret) {
			ret = args.ret;
			pr_info("PID: %d Comm: %.20s\n", t->pid, t->comm);
			dump_trace(t, NULL, (unsigned long *)t->thread.sp,
					0, &klp_print_trace_ops, NULL);
			goto out;
		}
	} while_each_thread(g, t);
out:

	return ret;
}
