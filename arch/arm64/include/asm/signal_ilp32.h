/*
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
#ifndef __ASM_SIGNAL_ILP32_H
#define __ASM_SIGNAL_ILP32_H

#ifdef CONFIG_ARM64_ILP32

#include <linux/compat.h>

static inline int put_sigset_t(compat_sigset_t __user *uset, sigset_t *set)
{
	compat_sigset_t cset;

	cset.sig[0] = set->sig[0] & 0xffffffffull;
	cset.sig[1] = set->sig[0] >> 32;

	return copy_to_user(uset, &cset, sizeof(*uset));
}

static inline int get_sigset_t(sigset_t *set,
			       const compat_sigset_t __user *uset)
{
	compat_sigset_t s32;

	if (copy_from_user(&s32, uset, sizeof(*uset)))
		return -EFAULT;

	set->sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
	return 0;
}

int ilp32_setup_rt_frame(int usig, struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs);

#else

static inline int ilp32_setup_rt_frame(int usig, struct ksignal *ksig,
				       sigset_t *set, struct pt_regs *regs)
{
	return -ENOSYS;
}

#endif /* CONFIG_ARM64_ILP32 */

#endif /* __ASM_SIGNAL_ILP32_H */
