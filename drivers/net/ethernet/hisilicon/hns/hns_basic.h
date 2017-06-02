/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_BASIC_H_
#define _HNS_BASIC_H_

#include <linux/netdevice.h>

#define MAX_REPORT_PORT_NUM 6
#define HNS_RESET_LIMIT_PERIOD 2000

/* nic device type */
enum nic_device_type {
	NIC_DEVICE_PORT = 0,	/* Twisted Pair Port */
	NIC_DEVICE_SFP		/* SFP */
};

/* nic event type */
enum nic_dev_event_type {
	NIC_DEVT_IN = 0,		/* device plug in */
	NIC_DEVT_OUT,			/* device pull out */
	NIC_DEVT_PORT_SPEED_UNMATCH,	/* speed unmatched */
	NIC_DEVT_PORT_CHIP_FAULT,	/* chip fault */
	NIC_DEVT_PORT_CHIP_RECOVER,	/* chip recover */
	NIC_DEVT_DSAF_CHIP_FAULT,	/* chip fault */
	NIC_DEVT_DSAF_CHIP_RECOVER,	/* chip recover */
	NIC_DEVT_NEED_PANIC,		/* such as: multi bit ecc */
};

enum netdev_node_state {
	HNS_PORT_RSTING_STATE = 0,
	HNS_DSAF_RSTING_STATE,
	HNS_PHY_RSTING_STATE,
	HNS_NEED_PANIC_STATE,
	HNS_OK_STATE
};

/**
 * nic_event_fn_t - nic event handler prototype
 * @port_cnt:	port count
 * @netdev:	net device
 * @nic_dev_event_type:	nic device event type
 * @nic_device_type:	nic device type
 */
typedef void (*nic_event_fn_t) (int port_cnt, struct net_device **netdev,
				enum nic_dev_event_type, enum nic_device_type);

/**
 * nic_register_event - register for nic event listening
 * @event_call:	nic event handler
 * return 0 - success , negative - fail
 */
int nic_register_event(nic_event_fn_t event_call);
void nic_unregister_event(void);
void nic_chip_recover_handler(int port_cnt, struct net_device **netdev);

#endif	/**__HNS_BASIC_H_ */
