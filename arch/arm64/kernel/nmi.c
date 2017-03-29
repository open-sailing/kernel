/*
 * Huawei driver
 *
 * Copyright (C)
 * Author: Huawei majun258@huawei.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) "ARM64 : NMI: " fmt

#include <linux/kernel.h>
#include <asm/smc_call.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/nmi.h>
#include <linux/hardirq.h>
#include <asm/smp_plat.h>
#include <asm/irqflags.h>
#include <asm/debug-monitors.h>

/* This value defined by the SMC Call manual*/
#define SMC_FUNCTION_REGISTE_ID 0x83000000
#define SMC_CALLBACK_ID 0x83000001
#define SMC_NMI_STATE_ID 0x83000002
#define SMC_NMI_TIMEOUT_ID 0x83000003

/* define the maximum timeout value is 60s*/
#define MAX_TIMEOUT_VALUE	60

/* set the handler default timeout value 100ms*/
#define HANDLER_TIME_LIMIT	100

struct hisi_nmi_watchdog cpu_nmi[NR_CPUS];

void __kprobes notrace hisi_nmi_handler(void)
{
	int ret;
	int (*nmi_handler)(void);
	int cpu = smp_processor_id();
	int ss_active = 0;

	ss_active = kernel_active_single_step();
	if (ss_active)
		kernel_disable_single_step_noregs();

	local_dbg_enable();
	nmi_enter();

	pr_debug("%s: cpu %d\n", __func__, cpu);

	cpu_nmi[cpu].jiffies_timeout = jiffies +
		msecs_to_jiffies(HANDLER_TIME_LIMIT);

	nmi_handler = cpu_nmi[cpu].nmi_handler;
	if (nmi_handler) {
		ret = nmi_handler();
		if (ret)
			pr_err("nmi handler return error\n");
	}

	if (time_after(jiffies, cpu_nmi[cpu].jiffies_timeout))
		pr_err("the cpu: %d nmi handler was running more than 100 ms(%d,%d).\n",
		       cpu, jiffies_to_msecs(jiffies),jiffies_to_msecs(cpu_nmi[cpu].jiffies_timeout));

	nmi_exit();
	if (ss_active) {
		local_dbg_disable();
		kernel_enable_single_step_noregs();
	}
}

static void hisi_register_nmi_handler(void *func)
{
	struct smc_param64 param = { 0 };

	param.a0 = SMC_FUNCTION_REGISTE_ID;
	param.a1 = (uint64_t)func;
	param.a2 = 0;

	pr_info("%s--start[%llx][%llx][%llx]: cpu: %d\n", __func__,
		 param.a0, param.a1, param.a2, smp_processor_id());

	smc_call(param.a0, param.a1, param.a2);
}

int register_nmi_handler(int cpuid, int (*func)(void))
{
	static int register_handler = 0;

	if (cpuid >= NR_CPUS) {
		pr_err("the cpu:%d is not exist\n", cpuid);
		return -EINVAL;
	}

	if (!cpu_online(cpuid)) {
		pr_err("the cpu:%d is not online\n", cpuid);
		return -EINVAL;
	}

	cpu_nmi[cpuid].id = cpuid;

	if (cpu_nmi[cpuid].nmi_handler)
		pr_debug("cpu: %d cover the old handler\n", cpuid);

	cpu_nmi[cpuid].nmi_handler = func;

	if (register_handler == 0) {
		hisi_register_nmi_handler(hisi_nmi_handler_swapper);
		register_handler = 1;
	}
	
	return 0;
}
EXPORT_SYMBOL(register_nmi_handler);

int nmi_set_active_state(int cpuid, int state)
{
	struct smc_param64 param = { 0 };
	u64 cpu_hwid = cpu_logical_map(cpuid);

	if (cpuid >= NR_CPUS) {
		pr_err("the cpu:%d is not exist\n", cpuid);
		return -EINVAL;
	}

	if (!cpu_online(cpuid)) {
		pr_err("the cpu:%d is not online\n", cpuid);
		return -EINVAL;
	}

	if ((state < 0) || (state >= NMI_WATCHDOG_STATE_MAX)){
		pr_err("invalid status value:%d\n", state);
		return -EINVAL;
	}

	param.a0 = SMC_NMI_STATE_ID;
	param.a1 = cpu_hwid;
	param.a2 = state;

	pr_debug("%s--start[%llx][%llx][%llx]:cpu: %d\n", __func__,
		 param.a0, param.a1, param.a2, smp_processor_id());

	cpu_nmi[cpuid].state = state;
	smc_call(param.a0, param.a1, param.a2);
	return 0;
}
EXPORT_SYMBOL(nmi_set_active_state);

int nmi_set_timeout(int cpuid, int time)
{
	struct smc_param64 param = { 0 };
	u64 cpu_hwid = cpu_logical_map(cpuid);

	if (cpuid >= NR_CPUS) {
		pr_err("the cpu:%d is not exist\n", cpuid);
		return -EINVAL;
	}

	if (!cpu_online(cpuid)) {
		pr_err("the cpu:%d is not online\n", cpuid);
		return -EINVAL;
	}

	if ((time <= 0) || (time > MAX_TIMEOUT_VALUE)) {
		pr_err("timeout value should be in [1,60]\n");
		return -EINVAL;
	}

	param.a0 = SMC_NMI_TIMEOUT_ID;
	param.a1 = cpu_hwid;
	param.a2 = time;

	pr_debug("%s--start[%llx][%llx][%llx]:cpu: %d\n", __func__,
		 param.a0, param.a1, param.a2, smp_processor_id());

	cpu_nmi[cpuid].timeout = time;
	smc_call(param.a0, param.a1, param.a2);

	return 0;
}
EXPORT_SYMBOL(nmi_set_timeout);

void touch_nmi_watchdog(void)
{
	/* add your function here to feed watchdog */
	touch_softlockup_watchdog();
}
EXPORT_SYMBOL(touch_nmi_watchdog);

