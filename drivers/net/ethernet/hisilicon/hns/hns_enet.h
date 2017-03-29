/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __HNS_ENET_H
#define __HNS_ENET_H

#include <linux/if_vlan.h>
#include <linux/netdevice.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "hnae.h"

enum hns_nic_state {
	NIC_STATE_TESTING = 0,
	NIC_STATE_RESETTING,
	NIC_STATE_REINITING,
	NIC_STATE_DOWN,
	NIC_STATE_DISABLED,
	NIC_STATE_REMOVING,
	NIC_STATE_SERVICE_INITED,
	NIC_STATE_SERVICE_SCHED,
	NIC_STATE2_RESET_REQUESTED,
	NIC_STATE_MAX
};

struct hns_nic_ring_data {
	struct hnae_ring *ring;
	struct napi_struct napi;
	int queue_index;
	cpumask_t mask; /* affinity mask */
	int (*poll_one)(struct hns_nic_ring_data *, int, void *);
	void (*ex_process)(struct hns_nic_ring_data *, struct sk_buff *);
	bool (*fini_process)(struct hns_nic_ring_data *);
};

/* compatible the difference between two versions */
struct hns_nic_ops {
	void (*fill_desc)(struct hnae_ring *ring, void *priv,
			  int size, dma_addr_t dma, int frag_end,
			  int buf_num, enum hns_desc_type type, int mtu);
	int (*maybe_stop_tx)(struct sk_buff **out_skb,
			     int *bnum, struct hnae_ring *ring);
	void (*get_rxd_bnum)(u32 bnum_flag, int *out_bnum);
	void (*get_vlan)(u32 vlan_flag, u16 *vlan);
};

struct hns_link_config {
	/* Describes what we're trying to get. */
	u8 flowctrl;

	/* Describes what we actually have. */
	u8 active_flowctrl;
	u8 pause_autoneg;
};

struct nic_work_entry {
	struct work_struct work;
};

struct nic_event_cb {
	struct workqueue_struct *wq;
};

struct hns_nic_priv {
	const struct device_node *ae_node;
	u32 enet_ver;
	u32 port_id;
	int phy_mode;
	int phy_led_val;
	struct phy_device *phy;
	struct net_device *netdev;
	struct device *dev;
	struct hnae_handle *ae_handle;

	struct hns_nic_ops ops;

	/* SMP locking strategy:
	 *
	 * lock: Held during reset, PHY access, timer, and when
	 *       updating priv parameters..
	 */
	spinlock_t lock;

	/* the cb for nic to manage the ring buffer, the first half of the
	 * array is for tx_ring and vice versa for the second half
	 */
	struct hns_nic_ring_data *ring_data;

	/* The most recently read link state */
	u32 link;
	struct ethtool_pauseparam epause;
	struct hns_link_config link_config;
	u64 tx_timeout_count;

	unsigned long state;

	/* used for reset. */
	struct mutex nic_lock;

	/* DCB parameters */
	struct ieee_pfc *hns_ieee_pfc;
	struct ieee_ets *hns_ieee_ets;
	u8 dcbx_cap;

	struct timer_list service_timer;

	struct work_struct service_task;

	struct notifier_block notifier_block;

	/* vlan bitmap */
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	struct nic_work_entry nic_rst_task;
	unsigned long recover_state;
	int last_event_type;
	int last_dev_type;

	u16 tx_queue;
};

#define tx_ring_data(priv, idx) ((priv)->ring_data[idx])
#define rx_ring_data(priv, idx) \
	((priv)->ring_data[(priv)->ae_handle->q_num + (idx)])

void hns_ethtool_set_ops(struct net_device *ndev);
void hns_nic_net_reset(struct net_device *ndev, int ring_reset_en);
void hns_nic_net_reinit(struct net_device *netdev);
int hns_nic_init_phy(struct net_device *ndev, struct hnae_handle *h);
netdev_tx_t hns_nic_net_xmit_hw(struct net_device *ndev,
				struct sk_buff *skb,
				struct hns_nic_ring_data *ring_data);
int hns_nic_get_mdio_reg(struct net_device *ndev, unsigned int page,
			 unsigned int reg, unsigned int *regvalue);
int hns_nic_set_mdio_reg(struct net_device *ndev, unsigned int page,
			 unsigned int reg, unsigned int regvalue);
u32 hns_nic_get_status(struct net_device *net_dev, u32 old_link);
void hns_get_nic_lock(struct hns_nic_priv *priv);
void hns_release_nic_lock(struct hns_nic_priv *priv);
void hns_dcbnl_set_ops(struct net_device *ndev);
#endif	/**__HNS_ENET_H */
