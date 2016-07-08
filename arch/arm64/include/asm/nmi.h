/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef _ASM_ARCH64_NMI_H
#define _ASM_ARCH64_NMI_H

#ifdef CONFIG_HISI_AARCH64_NMI
int register_nmi_handler(int cpuid, int (*func)(void));
int nmi_set_active_state(int cpuid, int state);
int nmi_set_timeout(int cpuid, int time);
#else
static inline int register_nmi_handler(int cpuid, int (*func)(void))
{
}

static inline int nmi_set_active_state(int cpuid, int state)
{
}

static inline int nmi_set_timeout(int cpuid, int time)
{
}
#endif

struct hisi_nmi_watchdog {
	int id;
	int timeout;
	int (*nmi_handler)(void);
	int state;
	unsigned long jiffies_timeout;
};

enum {
	NMI_WATCHDOG_OFF,
	NMI_WATCHDOG_PERIODIC,
	NMI_WATCHDOG_ONESHOT,
	NMI_WATCHDOG_STATE_MAX,
};
extern struct hisi_nmi_watchdog cpu_nmi[NR_CPUS];

#endif /* _ASM_ARCH64_NMI_H */

