/*
 * AArch64- ILP32 specific system calls implementation
 *
 * Copyright (C) 2016 Cavium Inc.
 * Author: Andrew Pinski <apinski@cavium.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/msg.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <asm-generic/syscalls.h>

/* Using non-compat syscalls where necessary */
#define compat_sys_fadvise64_64        sys_fadvise64_64
#define compat_sys_fallocate           sys_fallocate
#define compat_sys_ftruncate64         sys_ftruncate
#define compat_sys_lookup_dcookie      sys_lookup_dcookie
#define compat_sys_pread64             sys_pread64
#define compat_sys_pwrite64            sys_pwrite64
#define compat_sys_readahead           sys_readahead
#define compat_sys_shmat               sys_shmat
#define compat_sys_sync_file_range     sys_sync_file_range
#define compat_sys_truncate64          sys_truncate
#define sys_llseek                     sys_lseek
#define sys_mmap2		       sys_mmap

asmlinkage long ilp32_sys_rt_sigreturn_wrapper(void);
#define compat_sys_rt_sigreturn        ilp32_sys_rt_sigreturn_wrapper

#define sys_ptrace		       compat_sys_ptrace

#include <asm/syscall.h>

#undef __SYSCALL
#undef __SC_COMP
#undef __SC_WRAP
#undef __SC_3264
#undef __SC_COMP_3264

#define __SYSCALL_COMPAT
#define __SYSCALL(nr, sym)	[nr] = (sym),
#define __SC_WRAP(nr, sym)	[nr] = (compat_##sym),

/*
 * The sys_call_ilp32_table array must be 4K aligned to be accessible from
 * kernel/entry.S.
 */
void *sys_call_ilp32_table[__NR_syscalls] __aligned(4096) = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd.h>
};
