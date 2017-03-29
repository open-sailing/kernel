/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>

#include "hns_basic.h"
#include "hnae.h"
#include "hns_dsaf_mac.h"
#include "hns_dsaf_main.h"
#include "hns_dsaf_misc.h"
#include "hns_dsaf_ppe.h"
#include "hns_dsaf_rcb.h"

#define AE_NAME_PORT_ID_IDX 6
#define RST_FORBID_WAIT_STEP 10
#define MAX_RST_FORBID_WAIT_CNT 1000

int hns_reset_forbid_inc(struct hnae_handle *handle)
{
	int i;

	read_lock(&handle->rst_lock);
	for (i = 0; i < MAX_RST_FORBID_WAIT_CNT; i++) {
		if (!test_bit(HNS_PORT_IN_RSTING, &handle->port_unavailable)) {
			atomic_inc(&handle->rst_forbid_cnt);
			read_unlock(&handle->rst_lock);
			return 0;
		}

		read_unlock(&handle->rst_lock);
		if (in_atomic_preempt_off())
			mdelay(RST_FORBID_WAIT_STEP);
		else
			msleep(RST_FORBID_WAIT_STEP);
		read_lock(&handle->rst_lock);
	}

	read_unlock(&handle->rst_lock);
	dev_err(handle->owner_dev, "timeout: wait for resetting over 10s.");

	return -EBUSY;
}

void hns_reset_forbid_dec(struct hnae_handle *handle)
{
	atomic_dec(&handle->rst_forbid_cnt);
}

static struct hns_mac_cb *hns_get_mac_cb(struct hnae_handle *handle)
{
	struct  hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);

	return vf_cb->mac_cb;
}

/**
 * hns_ae_map_eport_to_dport - translate enet port id to dsaf port id
 * @port_id: enet port id
 *: debug port 0-1, service port 2 -7 (dsaf mode only 2)
 * return: dsaf port id
 *: service ports 0 - 5, debug port 6-7
 **/
static int hns_ae_map_eport_to_dport(u32 port_id)
{
	int port_index;

	if (port_id < DSAF_DEBUG_NW_NUM)
		port_index = port_id + DSAF_SERVICE_PORT_NUM_PER_DSAF;
	else
		port_index = port_id - DSAF_DEBUG_NW_NUM;

	return port_index;
}

static struct dsaf_device *hns_ae_get_dsaf_dev(struct hnae_ae_dev *dev)
{
	return container_of(dev, struct dsaf_device, ae_dev);
}

static struct hns_ppe_cb *hns_get_ppe_cb(struct hnae_handle *handle)
{
	int ppe_index;
	int ppe_common_index;
	struct ppe_common_cb *ppe_comm;
	struct  hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);

	if (vf_cb->port_index < DSAF_SERVICE_PORT_NUM_PER_DSAF) {
		ppe_index = vf_cb->port_index;
		ppe_common_index = 0;
	} else {
		ppe_index = 0;
		ppe_common_index =
			vf_cb->port_index - DSAF_SERVICE_PORT_NUM_PER_DSAF + 1;
	}
	ppe_comm = vf_cb->dsaf_dev->ppe_common[ppe_common_index];
	return &ppe_comm->ppe_cb[ppe_index];
}

static int hns_ae_get_q_num_per_vf(
	struct dsaf_device *dsaf_dev, int port)
{
	int common_idx = hns_dsaf_get_comm_idx_by_port(port);

	return dsaf_dev->rcb_common[common_idx]->max_q_per_vf;
}

static int hns_ae_get_vf_num_per_port(
	struct dsaf_device *dsaf_dev, int port)
{
	int common_idx = hns_dsaf_get_comm_idx_by_port(port);

	return dsaf_dev->rcb_common[common_idx]->max_vfn;
}

static struct ring_pair_cb *hns_ae_get_base_ring_pair(
	struct dsaf_device *dsaf_dev, int port)
{
	int common_idx = hns_dsaf_get_comm_idx_by_port(port);
	struct rcb_common_cb *rcb_comm = dsaf_dev->rcb_common[common_idx];
	int q_num = rcb_comm->max_q_per_vf;
	int vf_num = rcb_comm->max_vfn;

	if (common_idx == HNS_DSAF_COMM_SERVICE_NW_IDX)
		return &rcb_comm->ring_pair_cb[port * q_num * vf_num];
	else
		return &rcb_comm->ring_pair_cb[0];
}

static struct ring_pair_cb *hns_ae_get_ring_pair(struct hnae_queue *q)
{
	return container_of(q, struct ring_pair_cb, q);
}

struct hnae_handle *hns_ae_get_handle(struct hnae_ae_dev *dev,
				      u32 port_id)
{
	int port_idx;
	int vfnum_per_port;
	int qnum_per_vf;
	int i;
	struct dsaf_device *dsaf_dev;
	struct hnae_handle *ae_handle;
	struct ring_pair_cb *ring_pair_cb;
	struct hnae_vf_cb *vf_cb;
	struct hns_ppe_cb *ppe_cb;

	dsaf_dev = hns_ae_get_dsaf_dev(dev);
	port_idx = hns_ae_map_eport_to_dport(port_id);

	ring_pair_cb = hns_ae_get_base_ring_pair(dsaf_dev, port_idx);
	vfnum_per_port = hns_ae_get_vf_num_per_port(dsaf_dev, port_idx);
	qnum_per_vf = hns_ae_get_q_num_per_vf(dsaf_dev, port_idx);

	vf_cb = kzalloc(sizeof(*vf_cb) +
			qnum_per_vf * sizeof(struct hnae_queue *), GFP_KERNEL);
	if (unlikely(!vf_cb)) {
		dev_err(dsaf_dev->dev, "malloc vf_cb fail!\n");
		ae_handle = ERR_PTR(-ENOMEM);
		goto handle_err;
	}
	ae_handle = &vf_cb->ae_handle;
	/* ae_handle Init  */
	ae_handle->owner_dev = dsaf_dev->dev;
	ae_handle->dev = dev;
	ae_handle->q_num = qnum_per_vf;
	ae_handle->coal_param = HNAE_LOWEST_LATENCY_COAL_PARAM;
	spin_lock_init(&ae_handle->coal_set_lock);

	/* find ring pair, and set vf id*/
	for (ae_handle->vf_id = 0;
		ae_handle->vf_id < vfnum_per_port; ae_handle->vf_id++) {
		if (!ring_pair_cb->used_by_vf)
			break;
		ring_pair_cb += qnum_per_vf;
	}
	if (ae_handle->vf_id >= vfnum_per_port) {
		dev_err(dsaf_dev->dev, "malloc queue fail!\n");
		ae_handle = ERR_PTR(-EINVAL);
		goto vf_id_err;
	}

	ae_handle->qs = (struct hnae_queue **)(&ae_handle->qs + 1);
	for (i = 0; i < qnum_per_vf; i++) {
		ae_handle->qs[i] = &ring_pair_cb->q;
		ae_handle->qs[i]->rx_ring.q = ae_handle->qs[i];
		ae_handle->qs[i]->tx_ring.q = ae_handle->qs[i];

		ring_pair_cb->used_by_vf = 1;
		ring_pair_cb++;
	}

	vf_cb->dsaf_dev = dsaf_dev;
	vf_cb->port_index = port_idx;
	vf_cb->mac_cb = &dsaf_dev->mac_cb[port_idx];

	ppe_cb = hns_get_ppe_cb(ae_handle);

	ae_handle->phy_if = vf_cb->mac_cb->phy_if;
	ae_handle->phy_node = vf_cb->mac_cb->phy_node;
	ae_handle->if_support = vf_cb->mac_cb->if_support;
	ae_handle->port_type = vf_cb->mac_cb->mac_type;
	ae_handle->media_type = vf_cb->mac_cb->media_type;
	ae_handle->dport_id = port_idx;
	ae_handle->rss_key = ppe_cb->rss_key;
	ae_handle->rss_indir_table = ppe_cb->rss_indir_table;
	ae_handle->tc_cnt = DSAF_TC_CHANNEL_NUM;
	ae_handle->pfc_en = 0;
	return ae_handle;
vf_id_err:
	kfree(vf_cb);
handle_err:
	return ae_handle;
}

static void hns_ae_put_handle(struct hnae_handle *handle)
{
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);
	int i;

	vf_cb->mac_cb	 = NULL;

	kfree(vf_cb);

	for (i = 0; i < handle->q_num; i++)
		hns_ae_get_ring_pair(handle->qs[i])->used_by_vf = 0;
}

static int hns_ae_wait_flow_down(struct hnae_handle *handle)
{
	struct dsaf_device *dsaf_dev;
	struct hns_ppe_cb *ppe_cb;
	struct hnae_vf_cb *vf_cb;
	int ret;
	int i;

	for (i = 0; i < handle->q_num; i++) {
		ret = hns_rcb_wait_tx_ring_clean(handle->qs[i]);
		if (ret)
			return ret;
	}

	ppe_cb = hns_get_ppe_cb(handle);
	ret = hns_ppe_wait_tx_fifo_clean(ppe_cb);
	if (ret)
		return ret;

	dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	if (!dsaf_dev)
		return -EINVAL;
	ret = hns_dsaf_wait_pkt_clean(dsaf_dev, handle->dport_id);
	if (ret)
		return ret;

	vf_cb = hns_ae_get_vf_cb(handle);
	ret = hns_mac_wait_fifo_clean(vf_cb->mac_cb);
	if (ret)
		return ret;

	mdelay(10);
	return 0;
}

static void hns_ae_ring_enable_all(struct hnae_handle *handle, int val)
{
	int q_num = handle->q_num;
	int i;

	for (i = 0; i < q_num; i++)
		hns_rcb_ring_enable_hw(handle->qs[i], val);
}

static void hns_ae_init_queue(struct hnae_queue *q)
{
	struct ring_pair_cb *ring =
		container_of(q, struct ring_pair_cb, q);

	hns_rcb_init_hw(ring);
}

static void hns_ae_fini_queue(struct hnae_queue *q)
{
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(q->handle);

	if (vf_cb->mac_cb->mac_type == HNAE_PORT_SERVICE)
		hns_rcb_reset_ring_hw(q);
}

static int hns_ae_set_mac_address(struct hnae_handle *handle, void *p)
{
	int ret;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	if (!p || !is_valid_ether_addr((const u8 *)p)) {
		dev_err(handle->owner_dev, "is not valid ether addr !\n");
		return -EADDRNOTAVAIL;
	}

	ret = hns_mac_change_vf_addr(mac_cb, handle->vf_id, p);
	if (ret != 0) {
		dev_err(handle->owner_dev,
			"set_mac_address fail, ret=%d!\n", ret);
		return ret;
	}

	return 0;
}

static int hns_ae_set_mac_address_safe(struct hnae_handle *handle, void *p)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_set_mac_address(handle, p);
	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_add_rx_vlan(struct hnae_handle *handle, u16 vid)
{
	struct dsaf_device *dsaf_dev;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	(void)hns_mac_add_rx_vlan(mac_cb, vid);

	/* set port type to TRUNK mode when first vlan dev is added */
	if (mac_cb->vids == 1) {
		dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
		hns_dsaf_sw_port_type_cfg(dsaf_dev, mac_cb->mac_id,
					  DSAF_SW_PORT_TYPE_TRUNK);
	}

	return 0;
}

static int hns_ae_add_rx_vlan_safe(struct hnae_handle *handle, u16 vid)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_add_rx_vlan(handle, vid);
	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_del_rx_vlan(struct hnae_handle *handle, u16 vid)
{
	struct dsaf_device *dsaf_dev;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	(void)hns_mac_del_rx_vlan(mac_cb, vid);

	/* set port type to NON_VLAN mode when last vlan dev is deleted */
	if (mac_cb->vids == 0) {
		dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
		hns_dsaf_sw_port_type_cfg(dsaf_dev, mac_cb->mac_id,
					  DSAF_SW_PORT_TYPE_NON_VLAN);
	}

	return 0;
}

static int hns_ae_del_rx_vlan_safe(struct hnae_handle *handle, u16 vid)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_del_rx_vlan(handle, vid);
	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_set_multicast_one(struct hnae_handle *handle, void *addr)
{
	int ret;
	char *mac_addr = (char *)addr;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	u8 port_num;

	assert(mac_cb);

	if (mac_cb->mac_type != HNAE_PORT_SERVICE)
		return 0;

	ret = hns_mac_set_multi(mac_cb, mac_cb->mac_id, mac_addr, true);
	if (ret) {
		dev_err(handle->owner_dev,
			"mac add mul_mac:%pM port%d  fail, ret = %#x!\n",
			mac_addr, mac_cb->mac_id, ret);
		return ret;
	}

	ret = hns_mac_get_inner_port_num(mac_cb, 0, &port_num);
	if (ret)
		return ret;

	ret = hns_mac_set_multi(mac_cb, port_num, mac_addr, true);
	if (ret)
		dev_err(handle->owner_dev,
			"mac add mul_mac:%pM port%d  fail, ret = %#x!\n",
			mac_addr, DSAF_BASE_INNER_PORT_NUM, ret);

	return ret;
}

static int hns_ae_set_multicast_one_safe(struct hnae_handle *handle, void *addr)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_set_multicast_one(handle, addr);
	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_clr_multicast(struct hnae_handle *handle)
{
	int i;
	int ret = 0, rc = 0;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_tbl_tcam_key mac_key;
	struct dsaf_tbl_tcam_mcast_cfg mac_data;
	u8 addr[ETH_ALEN];
	u8 port;

	const u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	for (i = 0; i < DSAF_TCAM_SUM; i++) {
		hns_dsaf_tcam_mc_get(dsaf_dev, i, (struct dsaf_tbl_tcam_data *)
				     (&mac_key), &mac_data);

		hns_dsaf_tcam_addr_get(&mac_key, addr);
		port = dsaf_get_field(mac_key.low.bits.port_vlan,
				      DSAF_TBL_TCAM_KEY_PORT_M,
				      DSAF_TBL_TCAM_KEY_PORT_S);
		/* check valid tcam mc entry */
		if (mac_data.tbl_mcast_item_vld &&
		    port == mac_cb->mac_id &&
		    mac_key.high.bits.mac_0 & 0x01 &&
		    memcmp(addr, bc_addr, ETH_ALEN)) {
			/* clear mac-bit */
			rc = hns_mac_set_multi(mac_cb, mac_cb->mac_id,
					       addr, false);
			if (rc) {
				ret = -EINVAL;
				continue;
			}

			/* set mac out_port in case other vm-bit is in use */
			hns_dsaf_tcam_mc_get(dsaf_dev, i,
					     (struct dsaf_tbl_tcam_data *)
					     (&mac_key), &mac_data);
			if (mac_data.tbl_mcast_item_vld) {
				rc = hns_mac_set_multi(mac_cb, mac_cb->mac_id,
						       addr, true);
				if (rc) {
					ret = -EINVAL;
					continue;
				}
			}
		}
	}

	return ret;
}

static int hns_ae_clr_multicast_safe(struct hnae_handle *handle)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_clr_multicast(handle);

	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_set_unicast(struct hnae_handle *handle, void *addr)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_mac_single_dest_entry mac_entry;
	struct dsaf_drv_mac_single_dest_entry entry_tmp;
	struct dsaf_drv_mac_single_dest_entry entry_mask;

	memset(&mac_entry, 0, sizeof(mac_entry));
	memcpy(mac_entry.addr, addr, sizeof(mac_entry.addr));
	mac_entry.in_vlan_id = 0;	/* vlan_id field is not used by tcam */
	mac_entry.in_port_num = mac_cb->mac_id;
	(void)hns_mac_get_inner_port_num(mac_cb, (u8)handle->vf_id,
					 &mac_entry.port_num);

	memcpy(&entry_tmp, &mac_entry, sizeof(entry_tmp));
	memset(&entry_mask, 0xff, sizeof(entry_mask));
	entry_tmp.in_vlan_id = 0x00;
	entry_mask.in_vlan_id = 0x00;

	return hns_dsaf_set_mac_uc_entry(dsaf_dev, &entry_tmp, &entry_mask);
}

static int hns_ae_set_unicast_safe(struct hnae_handle *handle, void *addr)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_set_unicast(handle, addr);

	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_clr_unicast(struct hnae_handle *handle)
{
	int i;
	int ret = 0, rc = 0;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_tbl_tcam_key mac_key;
	struct dsaf_tbl_tcam_ucast_cfg mac_data;
	u8 port_num;
	u8 addr[ETH_ALEN];
	u8 port, vlan;
	const u8 zero_addr[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	for (i = 0; i < DSAF_TCAM_SUM; i++) {
		hns_dsaf_tcam_uc_get(dsaf_dev, i, (struct dsaf_tbl_tcam_data *)
				     (&mac_key), &mac_data);

		port = dsaf_get_field(mac_key.low.bits.port_vlan,
				      DSAF_TBL_TCAM_KEY_PORT_M,
				      DSAF_TBL_TCAM_KEY_PORT_S);
		vlan = dsaf_get_field(mac_key.low.bits.port_vlan,
				      DSAF_TBL_TCAM_KEY_VLAN_M,
				      DSAF_TBL_TCAM_KEY_VLAN_S);
		/* check valid tcam uc entry */
		if (mac_data.tbl_ucast_item_vld &&
		    port == mac_cb->mac_id &&
		    (mac_key.high.bits.mac_0 & 0x01) == 0) {
			hns_dsaf_tcam_addr_get(&mac_key, addr);

			/* promisc tcam table should not clear */
			if (memcmp(addr, zero_addr, ETH_ALEN) == 0)
				continue;

			rc = hns_mac_get_inner_port_num(mac_cb, handle->vf_id,
							&port_num);
			if (rc) {
				ret = -EINVAL;
				continue;
			}

			/* unicast entry not used locally should not clear */
			if (port_num != mac_data.tbl_ucast_out_port)
				continue;

			rc = hns_dsaf_del_mac_entry(dsaf_dev, vlan, port, addr);
			if (rc) {
				ret = -EINVAL;
				continue;
			}
		}
	}

	return ret;
}

static int hns_ae_clr_unicast_safe(struct hnae_handle *handle)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_clr_unicast(handle);

	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_set_mtu(struct hnae_handle *handle, int new_mtu)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	int i;
	int ret;
	u32 rx_buf_size;
	struct hnae_queue *q;

	/* when buf_size is 2048, max mtu is 6K for rx ring max bd num is 3. */
	if (!AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver)) {
		if (new_mtu <= BD_SIZE_2048_MAX_MTU)
			rx_buf_size = 2048;
		else
			rx_buf_size = 4096;
	} else {
		rx_buf_size = mac_cb->dsaf_dev->buf_size;
	}

	ret = hns_mac_set_mtu(mac_cb, new_mtu, rx_buf_size);

	if (!ret) {
		/* reinit ring buf_size */
		for (i = 0; i < handle->q_num; i++) {
			q = handle->qs[i];
			q->rx_ring.buf_size = rx_buf_size;
			hns_rcb_set_rx_ring_bs(q, rx_buf_size);
		}
	}

	return ret;
}

static int hns_ae_start(struct hnae_handle *handle)
{
	int ret;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	ret = hns_mac_vm_config_bc_en(mac_cb, 0, true);
	if (ret)
		return ret;

	hns_ae_ring_enable_all(handle, 1);
	mdelay(100);

	hns_mac_start(mac_cb);

	return 0;
}

void hns_ae_stop(struct hnae_handle *handle)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	/* just clean tx fbd, neednot rx fbd*/
	hns_rcb_wait_fbd_clean(handle->qs, handle->q_num, RCB_INT_FLAG_TX);

	mdelay(20);

	hns_mac_stop(mac_cb);

	mdelay(20);

	hns_ae_ring_enable_all(handle, 0);

	/* clean rx fbd. */
	hns_rcb_wait_fbd_clean(handle->qs, handle->q_num, RCB_INT_FLAG_RX);

	(void)hns_mac_vm_config_bc_en(mac_cb, 0, false);
}

static int hns_ae_reset(struct hnae_handle *handle)
{
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	int ret = 0;

	if ((vf_cb->mac_cb->mac_type == HNAE_PORT_SERVICE) &&
	    !AE_IS_VER1(vf_cb->dsaf_dev->dsaf_ver)) {
		hns_mac_disable(mac_cb, MAC_COMM_MODE_RX);
		ret = hns_ae_wait_flow_down(handle);
		if (ret) {
			netdev_err(hns_get_netdev(handle), "port reset fail\n");
			hns_mac_enable(mac_cb, MAC_COMM_MODE_RX);
			return -EBUSY;
		}
		mdelay(20);

		ret = hns_ppe_rcb_rst_hw(vf_cb->dsaf_dev, handle->dport_id);
		ret |= hns_xbar_rst_hw(vf_cb->dsaf_dev, handle->dport_id);
		ret |= hns_xbar_rst_hw(vf_cb->dsaf_dev, handle->dport_id + 6);

		hns_dsaf_commit_pause_mode(vf_cb->dsaf_dev,
					   handle->dport_id, handle->pfc_en);

		hns_mac_reset(vf_cb->mac_cb);
		/* restore dsaf sw port type */
		hns_dsaf_sw_port_type_rstr(vf_cb->dsaf_dev, handle->dport_id);
	} else if (vf_cb->mac_cb->mac_type == HNAE_PORT_DEBUG) {
		u8 ppe_common_index =
			vf_cb->port_index - DSAF_SERVICE_PORT_NUM_PER_DSAF + 1;

		hns_mac_reset(vf_cb->mac_cb);
		hns_ppe_reset_common(vf_cb->dsaf_dev, ppe_common_index);
	}

	return ret;
}

static int hns_ae_dsaf_reinit(struct hnae_handle *handle)
{
	struct dsaf_device *dsaf_dev;
	int ret;
	int i;

	dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	/* other irqs will be disable in port down */
	hns_rcb_irq_set(dsaf_dev->rcb_common[0], 0);

	ret = hns_dsaf_init_hw(dsaf_dev);
	if (ret) {
		dev_err(dsaf_dev->dev, "dsaf hw init failed in dsaf reinit.\n");
		goto out;
	}

	for (i = 0; i < DSAF_MAX_PORT_NUM_PER_CHIP; i++)
		hns_mac_init_hw(&dsaf_dev->mac_cb[i]);

	hns_ppe_prepare_init(dsaf_dev);
	for (i = 0; i < HNS_PPE_COM_NUM; i++)
		hns_ppe_reset_common(dsaf_dev, i);

	hns_dsaf_waterline_ex_init(dsaf_dev);

out:
	hns_rcb_irq_set(dsaf_dev->rcb_common[0], 1);
	return ret;
}

void hns_ae_toggle_ring_irq(struct hnae_ring *ring, u32 mask)
{
	u32 flag;

	if (is_tx_ring(ring))
		flag = RCB_INT_FLAG_TX;
	else
		flag = RCB_INT_FLAG_RX;

	hns_rcb_int_ctrl_hw(ring->q, flag, mask);
}

void hns_aev2_toggle_ring_irq(struct hnae_ring *ring, u32 mask)
{
	u32 flag;

	if (is_tx_ring(ring))
		flag = RCB_INT_FLAG_TX;
	else
		flag = RCB_INT_FLAG_RX;

	hns_rcbv2_int_ctrl_hw(ring->q, flag, mask);
}

static void hns_ae_toggle_queue_status(struct hnae_queue *queue, u32 val)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(queue->dev);

	if (AE_IS_VER1(dsaf_dev->dsaf_ver))
		hns_rcb_int_clr_hw(queue, RCB_INT_FLAG_TX | RCB_INT_FLAG_RX);
	else
		hns_rcbv2_int_clr_hw(queue, RCB_INT_FLAG_TX | RCB_INT_FLAG_RX);
}

static int hns_ae_get_link_status(struct hnae_handle *handle)
{
	u32 link_status;
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	hns_mac_get_link_status(mac_cb, &link_status);

	return !!link_status;
}

static int hns_ae_get_link_status_safe(struct hnae_handle *handle)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_get_link_status(handle);
	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_get_mac_info(struct hnae_handle *handle,
			       u8 *auto_neg, u16 *speed, u8 *duplex)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	return hns_mac_get_port_info(mac_cb, auto_neg, speed, duplex);
}

static int hns_ae_get_mac_info_safe(struct hnae_handle *handle,
				    u8 *auto_neg, u16 *speed, u8 *duplex)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_get_mac_info(handle, auto_neg, speed, duplex);
	hns_reset_forbid_dec(handle);
	return ret;
}

static bool hns_ae_need_adjust_link(struct hnae_handle *handle, int speed,
				    int duplex)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	return hns_mac_need_adjust_link(mac_cb, speed, duplex);
}

static void hns_ae_adjust_link(struct hnae_handle *handle, int speed,
			       int duplex)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	switch (mac_cb->dsaf_dev->dsaf_ver) {
	case AE_VERSION_1:
		hns_mac_adjust_link(mac_cb, speed, duplex);
		break;

	case AE_VERSION_2:
		/* chip need to clear all pkt inside */
		hns_mac_disable(mac_cb, MAC_COMM_MODE_RX);
		if (hns_ae_wait_flow_down(handle)) {
			hns_mac_enable(mac_cb, MAC_COMM_MODE_RX);
			break;
		}

		hns_mac_adjust_link(mac_cb, speed, duplex);
		hns_mac_enable(mac_cb, MAC_COMM_MODE_RX);
		break;

	default:
		break;
	}

	return;
}

static void hns_ae_adjust_link_safe(struct hnae_handle *handle, int speed,
				    int duplex)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_adjust_link(handle, speed, duplex);
	hns_reset_forbid_dec(handle);
}

static void hns_ae_get_ring_bdnum_limit(struct hnae_queue *queue,
					u32 *uplimit)
{
	*uplimit = HNS_RCB_RING_MAX_PENDING_BD;
}

static void hns_ae_get_pauseparam(struct hnae_handle *handle,
				  u32 *auto_neg, u32 *rx_en, u32 *tx_en)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;

	if (auto_neg)
		hns_mac_get_autoneg(mac_cb, auto_neg);

	hns_mac_get_pauseparam(mac_cb, rx_en, tx_en);

	/* Service port's pause feature is provided by DSAF, not mac */
	if (handle->port_type == HNAE_PORT_SERVICE)
		hns_dsaf_get_rx_mac_pause_en(dsaf_dev, mac_cb->mac_id, rx_en);
}

static void hns_ae_get_pauseparam_safe(struct hnae_handle *handle,
				       u32 *auto_neg, u32 *rx_en, u32 *tx_en)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_get_pauseparam(handle, auto_neg, rx_en, tx_en);
	hns_reset_forbid_dec(handle);
}

static void hns_ae_set_promisc_mode(struct hnae_handle *handle, u32 en)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	hns_dsaf_set_promisc(dsaf_dev, handle->dport_id, en);
}

static void hns_ae_set_promisc_safe(struct hnae_handle *handle, u32 en)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_set_promisc_mode(handle, en);
	hns_reset_forbid_dec(handle);
}

static int hns_ae_set_pauseparam(struct hnae_handle *handle,
				 u32 autoneg, u32 rx_en, u32 tx_en)
{
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	int ret;

	ret = hns_mac_set_autoneg(mac_cb, autoneg);
	if (ret)
		return ret;

	/* Service port's pause feature is provided by DSAF, not mac */
	if (handle->port_type == HNAE_PORT_SERVICE) {
		ret = hns_dsaf_set_rx_mac_pause_en(dsaf_dev,
						   mac_cb->mac_id, !!rx_en);
		if (ret)
			return ret;
		rx_en = 0;
	}
	return hns_mac_set_pauseparam(mac_cb, !!rx_en, !!tx_en);
}

static int hns_ae_set_pauseparam_safe(struct hnae_handle *handle,
				      u32 autoneg, u32 rx_en, u32 tx_en)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_set_pauseparam(handle, autoneg, rx_en, tx_en);
	hns_reset_forbid_dec(handle);
	return ret;
}

static void hns_ae_get_coalesce_usecs(struct hnae_handle *handle,
				      u32 *tx_usecs, u32 *rx_usecs)
{
	struct ring_pair_cb *ring_pair =
		container_of(handle->qs[0], struct ring_pair_cb, q);

	*tx_usecs = hns_rcb_get_coalesce_usecs(ring_pair->rcb_common,
					       ring_pair->port_id_in_comm);
	*rx_usecs = hns_rcb_get_coalesce_usecs(ring_pair->rcb_common,
					       ring_pair->port_id_in_comm);
}

static void hns_ae_get_max_coalesced_frames(struct hnae_handle *handle,
					    u32 *tx_frames, u32 *rx_frames)
{
	struct ring_pair_cb *ring_pair =
		container_of(handle->qs[0], struct ring_pair_cb, q);
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	if (AE_IS_VER1(dsaf_dev->dsaf_ver) ||
	    handle->port_type == HNAE_PORT_DEBUG)
		*tx_frames = hns_rcb_get_rx_coalesced_frames(
			ring_pair->rcb_common, ring_pair->port_id_in_comm);
	else
		*tx_frames = hns_rcb_get_tx_coalesced_frames(
			ring_pair->rcb_common, ring_pair->port_id_in_comm);
	*rx_frames = hns_rcb_get_rx_coalesced_frames(ring_pair->rcb_common,
						  ring_pair->port_id_in_comm);
}

static void hns_ae_get_max_coalesced_frames_safe(struct hnae_handle *handle,
						 u32 *tx_frames,
						 u32 *rx_frames)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_get_max_coalesced_frames(handle, tx_frames, rx_frames);
	hns_reset_forbid_dec(handle);
}

static int hns_ae_set_coalesce_usecs(struct hnae_handle *handle,
				     u32 timeout)
{
	struct ring_pair_cb *ring_pair =
		container_of(handle->qs[0], struct ring_pair_cb, q);

	return hns_rcb_set_coalesce_usecs(
		ring_pair->rcb_common, ring_pair->port_id_in_comm, timeout);
}

static int hns_ae_set_coalesce_usecs_safe(struct hnae_handle *handle,
					  u32 timeout)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_set_coalesce_usecs(handle, timeout);
	hns_reset_forbid_dec(handle);
	return ret;
}

static int hns_ae_set_coalesce_frames(struct hnae_handle *handle,
				      u32 tx_frames, u32 rx_frames)
{
	int ret;
	struct ring_pair_cb *ring_pair =
		container_of(handle->qs[0], struct ring_pair_cb, q);
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	if (AE_IS_VER1(dsaf_dev->dsaf_ver) ||
	    handle->port_type == HNAE_PORT_DEBUG) {
		if (tx_frames != rx_frames)
			return -EINVAL;
		return hns_rcb_set_rx_coalesced_frames(
			ring_pair->rcb_common,
			ring_pair->port_id_in_comm, rx_frames);
	} else {
		if (tx_frames != 1)
			return -EINVAL;
		ret = hns_rcb_set_tx_coalesced_frames(
			ring_pair->rcb_common,
			ring_pair->port_id_in_comm, tx_frames);
		if (ret)
			return ret;

		return hns_rcb_set_rx_coalesced_frames(
			ring_pair->rcb_common,
			ring_pair->port_id_in_comm, rx_frames);
	}
}

static int hns_ae_set_coalesce_frames_safe(struct hnae_handle *handle,
					   u32 tx_frames, u32 rx_frames)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_set_coalesce_frames(handle, tx_frames, rx_frames);
	hns_reset_forbid_dec(handle);
	return ret;
}

static void hns_ae_get_coalesce_range(struct hnae_handle *handle,
				      u32 *tx_frames_low, u32 *rx_frames_low,
				      u32 *tx_frames_high, u32 *rx_frames_high,
				      u32 *tx_usecs_low, u32 *rx_usecs_low,
				      u32 *tx_usecs_high, u32 *rx_usecs_high)
{
	struct dsaf_device *dsaf_dev;

	assert(handle);

	dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	*tx_frames_low  = HNS_RCB_TX_FRAMES_LOW;
	*rx_frames_low  = HNS_RCB_RX_FRAMES_LOW;

	if (AE_IS_VER1(dsaf_dev->dsaf_ver) ||
	    handle->port_type == HNAE_PORT_DEBUG)
		*tx_frames_high =
			(dsaf_dev->desc_num - 1 > HNS_RCB_TX_FRAMES_HIGH) ?
			HNS_RCB_TX_FRAMES_HIGH : dsaf_dev->desc_num - 1;
	else
		*tx_frames_high = 1;

	*rx_frames_high = (dsaf_dev->desc_num - 1 > HNS_RCB_RX_FRAMES_HIGH) ?
		HNS_RCB_RX_FRAMES_HIGH : dsaf_dev->desc_num - 1;
	*tx_usecs_low   = HNS_RCB_TX_USECS_LOW;
	*rx_usecs_low   = HNS_RCB_RX_USECS_LOW;
	*tx_usecs_high  = HNS_RCB_TX_USECS_HIGH;
	*rx_usecs_high  = HNS_RCB_RX_USECS_HIGH;
}

void hns_ae_update_stats(struct hnae_handle *handle,
			 struct net_device_stats *net_stats)
{
	int port;
	int idx;
	struct dsaf_device *dsaf_dev;
	struct hns_mac_cb *mac_cb;
	struct hns_ppe_cb *ppe_cb;
	struct hnae_queue *queue;
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);
	u64 tx_bytes = 0, rx_bytes = 0, tx_packets = 0, rx_packets = 0;
	u64 rx_errors = 0, tx_errors = 0, tx_dropped = 0;
	u64 rx_missed_errors = 0;

	dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	if (!dsaf_dev)
		return;
	port = vf_cb->port_index;
	ppe_cb = hns_get_ppe_cb(handle);
	mac_cb = hns_get_mac_cb(handle);

	for (idx = 0; idx < handle->q_num; idx++) {
		queue = handle->qs[idx];
		hns_rcb_update_stats(queue);

		tx_bytes += queue->tx_ring.stats.tx_bytes;
		tx_packets += queue->tx_ring.stats.tx_pkts;
		rx_bytes += queue->rx_ring.stats.rx_bytes;
		rx_packets += queue->rx_ring.stats.rx_pkts;

		rx_errors += queue->rx_ring.stats.err_pkt_len
				+ queue->rx_ring.stats.l2_err
				+ queue->rx_ring.stats.l3l4_csum_err;
	}

	hns_ppe_update_stats(ppe_cb);
	rx_missed_errors = ppe_cb->hw_stats.rx_drop_no_buf;
	tx_errors += ppe_cb->hw_stats.tx_err_checksum
		+ ppe_cb->hw_stats.tx_err_fifo_empty;

	if (mac_cb->mac_type == HNAE_PORT_SERVICE) {
		hns_dsaf_update_stats(dsaf_dev, port);
		/* for port upline direction, i.e., rx. */
		rx_missed_errors += dsaf_dev->hw_stats[port].bp_drop;
		rx_missed_errors += dsaf_dev->hw_stats[port].pad_drop;
		rx_missed_errors += dsaf_dev->hw_stats[port].crc_false;

		/* for port downline direction, i.e., tx. */
		port = port + DSAF_PPE_INODE_BASE;
		hns_dsaf_update_stats(dsaf_dev, port);
		tx_dropped += dsaf_dev->hw_stats[port].bp_drop;
		tx_dropped += dsaf_dev->hw_stats[port].pad_drop;
		tx_dropped += dsaf_dev->hw_stats[port].crc_false;
		tx_dropped += dsaf_dev->hw_stats[port].rslt_drop;
		tx_dropped += dsaf_dev->hw_stats[port].vlan_drop;
		tx_dropped += dsaf_dev->hw_stats[port].stp_drop;
	}

	hns_mac_update_stats(mac_cb);
	rx_errors += mac_cb->hw_stats.rx_fifo_overrun_err;

	tx_errors += mac_cb->hw_stats.tx_bad_pkts
		+ mac_cb->hw_stats.tx_fragment_err
		+ mac_cb->hw_stats.tx_jabber_err
		+ mac_cb->hw_stats.tx_underrun_err
		+ mac_cb->hw_stats.tx_crc_err;

	net_stats->tx_bytes = tx_bytes;
	net_stats->tx_packets = tx_packets;
	net_stats->rx_bytes = rx_bytes;
	net_stats->rx_dropped = 0;
	net_stats->rx_packets = rx_packets;
	net_stats->rx_errors = rx_errors;
	net_stats->tx_errors = tx_errors;
	net_stats->tx_dropped = tx_dropped;
	net_stats->rx_missed_errors = rx_missed_errors;
	net_stats->rx_crc_errors = mac_cb->hw_stats.rx_fcs_err;
	net_stats->rx_frame_errors = mac_cb->hw_stats.rx_align_err;
	net_stats->rx_fifo_errors = mac_cb->hw_stats.rx_fifo_overrun_err;
	net_stats->rx_length_errors = mac_cb->hw_stats.rx_len_err;
	net_stats->multicast = mac_cb->hw_stats.rx_mc_pkts;
}

void hns_ae_update_stats_safe(struct hnae_handle *handle,
			      struct net_device_stats *net_stats)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_update_stats(handle, net_stats);
	hns_reset_forbid_dec(handle);
}

void hns_ae_get_stats(struct hnae_handle *handle, u64 *data)
{
	int idx;
	struct hns_mac_cb *mac_cb;
	struct hns_ppe_cb *ppe_cb;
	u64 *p = data;
	struct  hnae_vf_cb *vf_cb;

	if (!handle || !data) {
		pr_err("hns_ae_get_stats NULL handle or data pointer!\n");
		return;
	}

	vf_cb = hns_ae_get_vf_cb(handle);
	mac_cb = hns_get_mac_cb(handle);
	ppe_cb = hns_get_ppe_cb(handle);

	for (idx = 0; idx < handle->q_num; idx++) {
		hns_rcb_get_stats(handle->qs[idx], p);
		p += hns_rcb_get_ring_sset_count((int)ETH_SS_STATS);
	}

	hns_ppe_get_stats(ppe_cb, p);
	p += hns_ppe_get_sset_count((int)ETH_SS_STATS);

	hns_mac_get_stats(mac_cb, p);
	p += hns_mac_get_sset_count(mac_cb, (int)ETH_SS_STATS);

	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		hns_dsaf_get_stats(vf_cb->dsaf_dev, p, vf_cb->port_index);
}

void hns_ae_get_stats_safe(struct hnae_handle *handle, u64 *data)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_get_stats(handle, data);
	hns_reset_forbid_dec(handle);
}

void hns_ae_get_strings(struct hnae_handle *handle,
			u32 stringset, u8 *data)
{
	int port;
	int idx;
	struct hns_mac_cb *mac_cb;
	struct hns_ppe_cb *ppe_cb;
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	u8 *p = data;
	struct	hnae_vf_cb *vf_cb;

	assert(handle);

	vf_cb = hns_ae_get_vf_cb(handle);
	port = vf_cb->port_index;
	mac_cb = hns_get_mac_cb(handle);
	ppe_cb = hns_get_ppe_cb(handle);

	for (idx = 0; idx < handle->q_num; idx++) {
		hns_rcb_get_strings(stringset, p, idx);
		p += ETH_GSTRING_LEN * hns_rcb_get_ring_sset_count(stringset);
	}

	hns_ppe_get_strings(ppe_cb, stringset, p);
	p += ETH_GSTRING_LEN * hns_ppe_get_sset_count(stringset);

	hns_mac_get_strings(mac_cb, stringset, p);
	p += ETH_GSTRING_LEN * hns_mac_get_sset_count(mac_cb, stringset);

	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		hns_dsaf_get_strings(stringset, p, port, dsaf_dev);
}

int hns_ae_get_sset_count(struct hnae_handle *handle, int stringset)
{
	u32 sset_count = 0;
	struct hns_mac_cb *mac_cb;
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	assert(handle);

	mac_cb = hns_get_mac_cb(handle);

	sset_count += hns_rcb_get_ring_sset_count(stringset) * handle->q_num;
	sset_count += hns_ppe_get_sset_count(stringset);
	sset_count += hns_mac_get_sset_count(mac_cb, stringset);

	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		sset_count += hns_dsaf_get_sset_count(dsaf_dev, stringset);

	return sset_count;
}

static int hns_ae_config_loopback(struct hnae_handle *handle,
				  enum hnae_loop loop, int en)
{
	int ret;
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);

	switch (loop) {
	case MAC_INTERNALLOOP_PHY:
		ret = 0;
		break;
	case MAC_INTERNALLOOP_SERDES:
		ret = hns_mac_config_sds_loopback(vf_cb->mac_cb, en);
		break;
	case MAC_INTERNALLOOP_MAC:
		ret = hns_mac_config_mac_loopback(vf_cb->mac_cb, loop, en);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	hns_ae_set_promisc_mode(handle, en);
out:
	return ret;
}

void hns_ae_update_led_status(struct hnae_handle *handle)
{
	struct hns_mac_cb *mac_cb;

	assert(handle);
	mac_cb = hns_get_mac_cb(handle);
	if (!mac_cb->cpld_vaddr)
		return;
	hns_set_led_opt(mac_cb);
}

void hns_ae_update_led_status_safe(struct hnae_handle *handle)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_update_led_status(handle);
	hns_reset_forbid_dec(handle);
}

int hns_ae_cpld_set_led_id(struct hnae_handle *handle,
			   enum hnae_led_state status)
{
	struct hns_mac_cb *mac_cb;

	assert(handle);

	mac_cb = hns_get_mac_cb(handle);

	return hns_cpld_led_set_id(mac_cb, status);
}

int hns_ae_cpld_set_led_id_safe(struct hnae_handle *handle,
				enum hnae_led_state status)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_cpld_set_led_id(handle, status);
	hns_reset_forbid_dec(handle);
	return ret;
}

void hns_ae_get_regs(struct hnae_handle *handle, void *data)
{
	u32 *p = data;
	u32 rcb_com_idx;
	int i;
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);
	struct hns_ppe_cb *ppe_cb = hns_get_ppe_cb(handle);

	hns_ppe_get_regs(ppe_cb, p);
	p += hns_ppe_get_regs_count();

	rcb_com_idx = hns_dsaf_get_comm_idx_by_port(vf_cb->port_index);
	hns_rcb_get_common_regs(vf_cb->dsaf_dev->rcb_common[rcb_com_idx], p);
	p += hns_rcb_get_common_regs_count();

	for (i = 0; i < handle->q_num; i++) {
		hns_rcb_get_ring_regs(handle->qs[i], p);
		p += hns_rcb_get_ring_regs_count();
	}

	hns_mac_get_regs(vf_cb->mac_cb, p);
	p += hns_mac_get_regs_count(vf_cb->mac_cb);

	if (vf_cb->mac_cb->mac_type == HNAE_PORT_SERVICE)
		hns_dsaf_get_regs(vf_cb->dsaf_dev, vf_cb->port_index, p);
}

void hns_ae_get_regs_safe(struct hnae_handle *handle, void *data)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_get_regs(handle, data);
	hns_reset_forbid_dec(handle);
}

int hns_ae_get_regs_len(struct hnae_handle *handle)
{
	u32 total_num;
	struct hnae_vf_cb *vf_cb = hns_ae_get_vf_cb(handle);

	total_num = hns_ppe_get_regs_count();
	total_num += hns_rcb_get_common_regs_count();
	total_num += hns_rcb_get_ring_regs_count() * handle->q_num;
	total_num += hns_mac_get_regs_count(vf_cb->mac_cb);

	if (vf_cb->mac_cb->mac_type == HNAE_PORT_SERVICE)
		total_num += hns_dsaf_get_regs_count();

	return total_num;
}

static u32 hns_ae_get_rss_key_size(struct hnae_handle *handle)
{
	return HNS_PPEV2_RSS_KEY_SIZE;
}

static u32 hns_ae_get_rss_indir_size(struct hnae_handle *handle)
{
	return HNS_PPEV2_RSS_IND_TBL_SIZE;
}

static int hns_ae_get_rss(struct hnae_handle *handle, u32 *indir, u8 *key,
			  u8 *hfunc)
{
	struct hns_ppe_cb *ppe_cb = hns_get_ppe_cb(handle);

	/* currently we support only one type of hash function i.e. Toep hash */
	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	/* get the RSS Key required by the user */
	if (key)
		memcpy(key, ppe_cb->rss_key, HNS_PPEV2_RSS_KEY_SIZE);

	/* update the current hash->queue mappings from the shadow RSS table */
	if (indir)
		memcpy(indir, ppe_cb->rss_indir_table,
		       HNS_PPEV2_RSS_IND_TBL_SIZE  * sizeof(*indir));

	return 0;
}

static int hns_ae_set_rss(struct hnae_handle *handle, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct hns_ppe_cb *ppe_cb = hns_get_ppe_cb(handle);

	/* set the RSS Hash Key if specififed by the user */
	if (key) {
		memcpy(ppe_cb->rss_key, key, HNS_PPEV2_RSS_KEY_SIZE);
		hns_ppe_set_rss_key(ppe_cb, ppe_cb->rss_key);
	}

	if (indir) {
		/* update the shadow RSS table with user specified qids */
		memcpy(ppe_cb->rss_indir_table, indir,
		       HNS_PPEV2_RSS_IND_TBL_SIZE  * sizeof(*indir));

		/* now update the hardware */
		hns_ppe_set_indir_table(ppe_cb, ppe_cb->rss_indir_table);
	}

	return 0;
}

#define HNS_PHY_PAGE_ADDRESS (0x16)

int hns_ae_get_mdio_reg(struct hnae_handle *handle, unsigned int page,
			unsigned int reg, unsigned int *regvalue)

{
	struct phy_device *phy_dev;
	struct mii_bus *bus;
	struct net_device *netdev = hns_get_netdev(handle);
	int tmp_page;
	int ret;

	phy_dev = handle->phy_dev;
	if (!phy_dev) {
		netdev_info(netdev, "has no phy attached.\n");
		return -EPERM;
	}

	bus = phy_dev->bus;
	if (!bus) {
		netdev_info(netdev, "has no bus attached.\n");
		return -EPERM;
	}

	/* operate flow:
	 * 1 record current page address
	 * 2 jump to operated page
	 * 3 operate register(read or write)
	 * 4 come back to the page recorded in the first step.
	 */
	mutex_lock(&bus->mdio_lock);
	tmp_page = bus->read(bus, phy_dev->addr, HNS_PHY_PAGE_ADDRESS);
	if (tmp_page < 0) {
		mutex_unlock(&bus->mdio_lock);
		netdev_warn(netdev, "%s record current phy reg page failed.\n",
			    netdev->name);
		return tmp_page;
	}

	if (tmp_page == page) {
		ret = bus->read(bus, phy_dev->addr, reg);
		mutex_unlock(&bus->mdio_lock);
		if (ret >= 0) {
			*regvalue = ret;
			return 0;
		}

		return ret;
	}

	ret = bus->write(bus, phy_dev->addr, HNS_PHY_PAGE_ADDRESS, (u16)page);
	if (ret < 0) {
		mutex_unlock(&bus->mdio_lock);
		netdev_warn(netdev, "change phy reg page %u to %u failed.\n",
			    tmp_page, page);
		return ret;
	}

	ret = bus->read(bus, phy_dev->addr, reg);
	if (ret < 0) {
		netdev_warn(netdev, "read phy reg(%u-%u) failed.\n", page, reg);
		if (bus->write(bus, phy_dev->addr, HNS_PHY_PAGE_ADDRESS,
			       (u16)tmp_page) < 0) {
			netdev_err(netdev, "restore phy reg page %u failed after error read.\n",
				   tmp_page);
		}
		mutex_unlock(&bus->mdio_lock);
		return ret;
	}

	*regvalue = ret;

	ret = bus->write(bus, phy_dev->addr, HNS_PHY_PAGE_ADDRESS,
			 (u16)tmp_page);
	if (ret < 0) {
		mutex_unlock(&bus->mdio_lock);
		netdev_err(netdev, "restore phy reg page %u failed.\n",
			   tmp_page);
		return ret;
	}

	mutex_unlock(&bus->mdio_lock);
	return 0;
}

int hns_ae_set_mdio_reg(struct hnae_handle *handle, unsigned int page,
			unsigned int reg, unsigned int regvalue)
{
	struct phy_device *phy_dev;
	struct mii_bus *bus;
	struct net_device *netdev = hns_get_netdev(handle);
	int tmp_page;
	int ret;

	phy_dev = handle->phy_dev;
	if (!phy_dev) {
		netdev_info(netdev, "has no phy attached.\n");
		return -EPERM;
	}

	bus = phy_dev->bus;
	if (!bus) {
		netdev_info(netdev, "has no bus attached.\n");
		return -EPERM;
	}

	/* operate flow:
	 * 1 record current page address.
	 *   if the page is we want to operate, step 2 and 4 can ignore.
	 * 2 jump to operated page
	 * 3 operate register(read or write)
	 * 4 come back to the page recorded in the first step.
	 */
	mutex_lock(&bus->mdio_lock);
	tmp_page = bus->read(bus, phy_dev->addr, HNS_PHY_PAGE_ADDRESS);
	if (tmp_page < 0) {
		mutex_unlock(&bus->mdio_lock);
		netdev_warn(netdev, "record current phy reg page failed.\n");
		return tmp_page;
	}

	if (tmp_page == page) {
		ret = bus->write(bus, phy_dev->addr, reg, regvalue);
		mutex_unlock(&bus->mdio_lock);
		return ret;
	}

	ret = bus->write(bus, phy_dev->addr, HNS_PHY_PAGE_ADDRESS, (u16)page);
	if (ret < 0) {
		mutex_unlock(&bus->mdio_lock);
		netdev_warn(netdev, "change phy reg page %u to %u failed.\n",
			    tmp_page, page);
		return ret;
	}

	ret = bus->write(bus, phy_dev->addr, reg, regvalue);
	if (ret < 0) {
		netdev_warn(netdev, "read phy reg(%u-%u) failed.\n", page, reg);
		if (bus->write(bus, phy_dev->addr, HNS_PHY_PAGE_ADDRESS,
			       (u16)tmp_page) < 0) {
			netdev_err(netdev, "restore phy reg page %u failed after error read.\n",
				   tmp_page);
		}
		mutex_unlock(&bus->mdio_lock);
		return ret;
	}

	ret = bus->write(bus, phy_dev->addr, HNS_PHY_PAGE_ADDRESS,
			 (u16)tmp_page);
	if (ret < 0) {
		mutex_unlock(&bus->mdio_lock);
		netdev_err(netdev, "restore phy reg page %u failed.\n",
			   tmp_page);
		return ret;
	}

	mutex_unlock(&bus->mdio_lock);
	return 0;
}

static int hns_ae_port_irq_init(struct hnae_handle *handle)
{
	struct dsaf_device *dsaf_dev;
	struct hns_mac_cb *mac_cb;
	int ret;

	if (handle->irq_en)
		return 0;

	dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	/* only register for service port and 161x */
	if ((handle->dport_id >= DSAF_SERVICE_NW_NUM) ||
	    AE_IS_VER1(dsaf_dev->dsaf_ver))
		return 0;

	ret = hns_dsaf_xbar_irq_init(dsaf_dev, handle->dport_id);
	if (ret)
		goto xbar_irq_failed;

	ret = hns_ppe_irq_init(dsaf_dev, handle->dport_id);
	if (ret)
		goto ppe_irq_failed;

	mac_cb = hns_get_mac_cb(handle);
	ret = hns_mac_irq_init(mac_cb);
	if (ret)
		goto mac_irq_failed;

	handle->irq_en = 1;
	return 0;

mac_irq_failed:
	hns_ppe_irq_free(dsaf_dev, handle->dport_id);
ppe_irq_failed:
	hns_dsaf_xbar_irq_free(dsaf_dev, handle->dport_id);
xbar_irq_failed:
	return ret;
}

static int hns_ae_port_irq_init_safe(struct hnae_handle *handle)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return ret;

	ret = hns_ae_port_irq_init(handle);
	hns_reset_forbid_dec(handle);
	return ret;
}

static void hns_ae_port_irq_free(struct hnae_handle *handle)
{
	struct dsaf_device *dsaf_dev;
	struct hns_mac_cb *mac_cb;

	if (!handle->irq_en)
		return;

	dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);

	/* only register for service port and 161x */
	if ((handle->dport_id >= DSAF_SERVICE_NW_NUM) ||
	    AE_IS_VER1(dsaf_dev->dsaf_ver))
		return;

	mac_cb = hns_get_mac_cb(handle);

	hns_dsaf_xbar_irq_free(dsaf_dev, handle->dport_id);
	hns_ppe_irq_free(dsaf_dev, handle->dport_id);
	hns_mac_irq_free(mac_cb);

	handle->irq_en = 0;
}

#ifdef CONFIG_DCB
static void hns_ae_get_dcb_up2tc(struct hnae_handle *handle,
				 u8 *prio_tc)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	u32 prio_val, i;

	if (!prio_tc)
		return;

	hns_dsaf_ets_get_up2tc(dsaf_dev, mac_cb->mac_id, &prio_val);

	/* change prio_val to prio_tc */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		prio_tc[i] = (prio_val >> (3 * i)) & 0x7;
}

static void hns_ae_get_dcb_up2tc_safe(struct hnae_handle *handle,
				      u8 *prio_tc)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_get_dcb_up2tc(handle, prio_tc);
	hns_reset_forbid_dec(handle);
}

static void hns_ae_config_dcb_pfc(struct hnae_handle *handle, u8 pfc_en)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	bool is_ver1 = AE_IS_VER1(dsaf_dev->dsaf_ver);
	u8 tmp_prio_tc[IEEE_8021QAZ_MAX_TCS];
	u32 tc, prio;
	int enabled;

	if (!pfc_en) {
		hns_dsaf_pfc_en_cfg(dsaf_dev, mac_cb->mac_id, 0,
				    is_ver1, is_ver1);
		return;
	}

	hns_ae_get_dcb_up2tc(handle, tmp_prio_tc);

	for (tc = 0; tc < handle->tc_cnt; tc++) {
		enabled = 0;

		for (prio = 0; prio < IEEE_8021QAZ_MAX_TCS; prio++) {
			if (tmp_prio_tc[prio] == tc && (pfc_en & (1 << prio))) {
				enabled = 1;
				break;
			}
		}

		hns_dsaf_pfc_set_tc_en(dsaf_dev, mac_cb->mac_id,
				       tc, enabled);
	}
}

static void hns_ae_config_dcb_pfc_safe(struct hnae_handle *handle, u8 pfc_en)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_config_dcb_pfc(handle, pfc_en);
	hns_reset_forbid_dec(handle);
}

static void hns_ae_get_dcb_pfc_pause_state(struct hnae_handle *handle,
					   u8 *state)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	hns_dsaf_pfc_get_pause_en(dsaf_dev, mac_cb->mac_id, state);
}

static void hns_ae_get_dcb_pfc_pause_state_safe(struct hnae_handle *handle,
						u8 *state)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_get_dcb_pfc_pause_state(handle, state);
	hns_reset_forbid_dec(handle);
}

static void hns_ae_set_dcb_pfc_pause_state(struct hnae_handle *handle,
					   u8 state)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	hns_dsaf_pfc_set_pause_en(dsaf_dev, mac_cb->mac_id, state);
}

static void hns_ae_set_dcb_pfc_pause_state_safe(struct hnae_handle *handle,
						u8 state)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_set_dcb_pfc_pause_state(handle, state);
	hns_reset_forbid_dec(handle);
}

static void hns_ae_set_dcb_up2tc(struct hnae_handle *handle, u8 *prio_tc)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);
	u32 prio_val = 0;
	u32 i;

	if (!prio_tc)
		return;

	/* change prio_tc to prio_val */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		prio_val |= (((unsigned int)prio_tc[i]) << (i * 3));

	hns_dsaf_ets_set_up2tc(dsaf_dev, mac_cb->mac_id, prio_val);
}

void hns_ae_set_dcb_up2tc_safe(struct hnae_handle *handle,
			       u8 *prio_tc)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_set_dcb_up2tc(handle, prio_tc);
	hns_reset_forbid_dec(handle);
}

static void hns_ae_get_dcb_tc_cfg(struct hnae_handle *handle, u8 tc,
				  u8 *state)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	hns_dsaf_pfc_get_tc_en(dsaf_dev, mac_cb->mac_id, tc, state);
}

static void hns_ae_get_dcb_tc_cfg_safe(struct hnae_handle *handle, u8 tc,
				       u8 *state)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_get_dcb_tc_cfg(handle, tc, state);
	hns_reset_forbid_dec(handle);
}

static void hns_ae_set_dcb_tc_cfg(struct hnae_handle *handle, u8 tc,
				  u8 state)
{
	struct dsaf_device *dsaf_dev = hns_ae_get_dsaf_dev(handle->dev);
	struct hns_mac_cb *mac_cb = hns_get_mac_cb(handle);

	hns_dsaf_pfc_set_tc_en(dsaf_dev, mac_cb->mac_id, tc, state);
}

static void hns_ae_set_dcb_tc_cfg_safe(struct hnae_handle *handle, u8 tc,
				       u8 state)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_set_dcb_tc_cfg(handle, tc, state);
	hns_reset_forbid_dec(handle);
}
#endif

static void hns_ae_port_irq_free_safe(struct hnae_handle *handle)
{
	int ret;

	ret = hns_reset_forbid_inc(handle);
	if (ret)
		return;

	hns_ae_port_irq_free(handle);
	hns_reset_forbid_dec(handle);
}

int hns_wait_for_reset_permit(struct hnae_handle *handle)
{
	int cnt = 0;

	while (atomic_read(&handle->rst_forbid_cnt)) {
		mdelay(1);
		cnt++;
		if (cnt > MAX_RST_FORBID_WAIT_CNT) {
			netdev_err(hns_get_netdev(handle), "rst_forbid_cnt:%d\n",
				   atomic_read(&handle->rst_forbid_cnt));
			return -ETIMEDOUT;
		}
	}

	return 0;
}

void hns_set_port_reset_flag(struct hnae_handle *handle)
{
	set_bit(HNS_PORT_IN_RSTING, &handle->port_unavailable);
}

void hns_clear_port_reset_flag(struct hnae_handle *handle)
{
	clear_bit(HNS_PORT_IN_RSTING, &handle->port_unavailable);
}

static int hns_ae_get_rst_lock(struct hnae_handle *handle)
{
	unsigned long flags;
	int ret;

	write_lock_irqsave(&handle->rst_lock, flags);
	if (!test_bit(HNS_PORT_IN_RSTING, &handle->port_unavailable)) {
		ret = hns_wait_for_reset_permit(handle);
		if (ret) {
			write_unlock_irqrestore(&handle->rst_lock, flags);
			return ret;
		}
		hns_set_port_reset_flag(handle);
	}

	write_unlock_irqrestore(&handle->rst_lock, flags);

	return 0;
}

static void hns_ae_release_rst_lock(struct hnae_handle *handle)
{
	hns_clear_port_reset_flag(handle);
}

static struct hnae_ae_ops hns_dsaf_ops = {
	.get_handle = hns_ae_get_handle,
	.put_handle = hns_ae_put_handle,
	.init_queue = hns_ae_init_queue,
	.fini_queue = hns_ae_fini_queue,
	.start = hns_ae_start,
	.stop = hns_ae_stop,
	.reset = hns_ae_reset,
	.dsaf_reset = hns_ae_dsaf_reinit,
	.toggle_ring_irq = hns_ae_toggle_ring_irq,
	.toggle_queue_status = hns_ae_toggle_queue_status,
	.get_status = hns_ae_get_link_status_safe,
	.get_info = hns_ae_get_mac_info_safe,
	.adjust_link = hns_ae_adjust_link_safe,
	.need_adjust_link = hns_ae_need_adjust_link,
	.set_loopback = hns_ae_config_loopback,
	.get_ring_bdnum_limit = hns_ae_get_ring_bdnum_limit,
	.get_pauseparam = hns_ae_get_pauseparam_safe,
	.set_pauseparam = hns_ae_set_pauseparam_safe,
	.get_coalesce_usecs = hns_ae_get_coalesce_usecs,
	.get_max_coalesced_frames = hns_ae_get_max_coalesced_frames_safe,
	.set_coalesce_usecs = hns_ae_set_coalesce_usecs_safe,
	.set_coalesce_frames = hns_ae_set_coalesce_frames_safe,
	.get_coalesce_range = hns_ae_get_coalesce_range,
	.set_promisc_mode = hns_ae_set_promisc_safe,
	.set_mac_addr = hns_ae_set_mac_address_safe,
	.set_mc_addr = hns_ae_set_multicast_one_safe,
	.clr_mc_addr = hns_ae_clr_multicast_safe,
	.set_uc_addr = hns_ae_set_unicast_safe,
	.clr_uc_addr = hns_ae_clr_unicast_safe,
	.add_rx_vlan = hns_ae_add_rx_vlan_safe,
	.del_rx_vlan = hns_ae_del_rx_vlan_safe,
	.set_mtu = hns_ae_set_mtu,
	.update_stats = hns_ae_update_stats_safe,
	.get_stats = hns_ae_get_stats_safe,
	.get_strings = hns_ae_get_strings,
	.get_sset_count = hns_ae_get_sset_count,
	.update_led_status = hns_ae_update_led_status_safe,
	.set_led_id = hns_ae_cpld_set_led_id_safe,
	.get_regs = hns_ae_get_regs_safe,
	.get_regs_len = hns_ae_get_regs_len,
	.get_rss_key_size = hns_ae_get_rss_key_size,
	.get_rss_indir_size = hns_ae_get_rss_indir_size,
	.get_rss = hns_ae_get_rss,
	.set_rss = hns_ae_set_rss,
	.set_mdio_reg = hns_ae_set_mdio_reg,
	.get_mdio_reg = hns_ae_get_mdio_reg,
	.port_irq_init = hns_ae_port_irq_init_safe,
	.port_irq_free = hns_ae_port_irq_free_safe,

#ifdef CONFIG_DCB
	.config_dcb_pfc = hns_ae_config_dcb_pfc_safe,
	.get_dcb_pfc_pause_state = hns_ae_get_dcb_pfc_pause_state_safe,
	.set_dcb_pfc_pause_state = hns_ae_set_dcb_pfc_pause_state_safe,
	.get_dcb_up2tc = hns_ae_get_dcb_up2tc_safe,
	.set_dcb_up2tc = hns_ae_set_dcb_up2tc_safe,
	.get_dcb_tc_cfg = hns_ae_get_dcb_tc_cfg_safe,
	.set_dcb_tc_cfg = hns_ae_set_dcb_tc_cfg_safe,
#endif

	.get_rst_lock = hns_ae_get_rst_lock,
	.release_rst_lock = hns_ae_release_rst_lock,
};

int hns_dsaf_ae_init(struct dsaf_device *dsaf_dev)
{
	struct hnae_ae_dev *ae_dev = &dsaf_dev->ae_dev;
	static atomic_t id = ATOMIC_INIT(-1);

	switch (dsaf_dev->dsaf_ver) {
	case AE_VERSION_1:
		hns_dsaf_ops.toggle_ring_irq = hns_ae_toggle_ring_irq;
		break;
	case AE_VERSION_2:
		hns_dsaf_ops.toggle_ring_irq = hns_aev2_toggle_ring_irq;
		break;
	default:
		break;
	}

	snprintf(ae_dev->name, AE_NAME_SIZE, "%s%d", DSAF_DEVICE_NAME,
		 (int)atomic_inc_return(&id));
	ae_dev->ops = &hns_dsaf_ops;
	ae_dev->dev = dsaf_dev->dev;

	return hnae_ae_register(ae_dev, THIS_MODULE);
}

void hns_dsaf_ae_uninit(struct dsaf_device *dsaf_dev)
{
	hnae_ae_unregister(&dsaf_dev->ae_dev);
}
