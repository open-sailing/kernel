#ifndef _LINUX_KPROBES_IRQ_H
#define _LINUX_KPROBES_IRQ_H


#ifdef CONFIG_KPROBES_NMI_ENTER
extern void kprobes_nmi_enter(void);
extern void kprobes_nmi_exit(void);
#else
static inline void kprobes_nmi_enter(void) { }
static inline void kprobes_nmi_exit(void) { }
#endif

#endif /* _LINUX_KPROBES_IRQ_H */
