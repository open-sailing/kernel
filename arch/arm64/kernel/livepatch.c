/*
 * livepatch.c - arm64-specific Kernel Live Patching Core
 *
 * Copyright (C) 2014 Li Bin <huawei.libin@huawei.com>
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
#include <asm/livepatch.h>
#include <asm/stacktrace.h>
#include <asm/cacheflush.h>

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
	unsigned int readonly = 0;
	int ret;
#ifdef CONFIG_DEBUG_SET_MODULE_RONX
	if (loc < (unsigned long)mod->module_core + mod->core_ro_size)
		readonly = 1;
#endif

	if (readonly)
		set_memory_rw(loc&PAGE_MASK, 1);

	/* Perform the static relocation. */
	ret = static_relocate(mod, type, (void *)loc, value);

	if (readonly)
		set_memory_ro(loc&PAGE_MASK, 1);
	return ret;
}

struct walk_stackframe_args {
	struct klp_patch *patch;
	int enable;
	int ret;
};

static inline int klp_compare_address(unsigned long pc, unsigned long func_addr,
				unsigned long func_size, const char *func_name)
{
	if (pc >= func_addr && pc < func_addr + func_size) {
		pr_err("func %s is in use!\n", func_name);
		return -EBUSY;
	}
	return 0;
}

static int klp_check_activeness_func(struct stackframe *frame, void *data)
{
	struct walk_stackframe_args *args = data;
	struct klp_patch *patch = args->patch;
	struct klp_object *obj;
	struct klp_func *func;
	unsigned long func_addr, func_size;
	const char *func_name;

	if (args->ret)
		return args->ret;

	for (obj = patch->objs; obj->funcs; obj++) {
		for (func = obj->funcs; func->old_name; func++) {
			if (args->enable) {
				if (func->force)
					continue;
				func_addr = func->old_addr;
				func_size = func->old_size;
			} else {
				func_addr = (unsigned long)func->new_func;
				func_size = func->new_size;
			}
			func_name = func->old_name;
			args->ret = klp_compare_address(frame->pc, func_addr, func_size, func_name);
			if (args->ret)
				return args->ret;
		}
	}

	return args->ret;
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static unsigned long klp_ftrace_graph_addr(unsigned long addr,
		struct task_struct *tsk,
		int *graph)
{
	unsigned long ret_addr = 0;
	int index = tsk->curr_ret_stack;

	if ((addr + 4) != (unsigned long)return_to_handler)
		return ret_addr;

	if (!tsk->ret_stack || index < *graph)
		return ret_addr;

	index -= *graph;
	ret_addr = tsk->ret_stack[index].ret;

	(*graph)++;
	return ret_addr;
}
#else
static unsigned long klp_ftrace_graph_addr(unsigned long addr,
		struct task_struct *tsk,
		int *graph)
{
	return 0;
}
#endif

void notrace klp_walk_stackframe(struct stackframe *frame,
		int (*fn)(struct stackframe *, void *),
		struct task_struct *tsk, void *data)
{
	unsigned long addr;
	int graph = 0;

	while (1) {
		int ret;

		if (fn(frame, data))
			break;
		ret = unwind_frame(frame);
		if (ret < 0)
			break;

		addr = klp_ftrace_graph_addr(frame->pc, tsk, &graph);
		if (addr)
			frame->pc = addr;
	}
}

int klp_check_calltrace(struct klp_patch *patch, int enable)
{
	struct task_struct *g, *t;
	struct stackframe frame;
	int ret = 0;

	struct walk_stackframe_args args = {
		.patch = patch,
		.enable = enable,
		.ret = 0
	};

	do_each_thread(g, t) {
		frame.fp = thread_saved_fp(t);
		frame.sp = thread_saved_sp(t);
		frame.pc = thread_saved_pc(t);
		klp_walk_stackframe(&frame, klp_check_activeness_func, t, &args);
		if (args.ret) {
			ret = args.ret;
			pr_info("PID: %d Comm: %.20s\n", t->pid, t->comm);
			show_stack(t, NULL);
			goto out;
		}
	} while_each_thread(g, t);

out:
	return ret;
}
