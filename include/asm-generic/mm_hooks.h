/*
 * Define generic no-op hooks for arch_dup_mmap, arch_exit_mmap
 * and arch_unmap to be included in asm-FOO/mmu_context.h for any
 * arch FOO which doesn't need to hook these.
 */
#ifndef _ASM_GENERIC_MM_HOOKS_H
#define _ASM_GENERIC_MM_HOOKS_H

static inline void arch_dup_mmap(struct mm_struct *oldmm,
				 struct mm_struct *mm)
{
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
}

static inline void arch_unmap(struct mm_struct *mm,
			struct vm_area_struct *vma,
			unsigned long start, unsigned long end)
{
#ifdef CONFIG_ARCH_WANT_VDSO_MAP
	if (start <= mm->context.vdso && mm->context.vdso < end)
		mm->context.vdso = 0;
#endif  /* CONFIG_ARCH_WANT_VDSO_MAP */
}

static inline void arch_bprm_mm_init(struct mm_struct *mm,
				     struct vm_area_struct *vma)
{
}

#endif	/* _ASM_GENERIC_MM_HOOKS_H */
