/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>

#include "hns_basic.h"
#include "hnae.h"
#include "hns_enet.h"

#define NIC_MAX_Q_PER_VF 16
#define HNS_NIC_TX_TIMEOUT (5 * HZ)

#define SERVICE_TIMER_HZ (1 * HZ)

#define NIC_TX_CLEAN_MAX_NUM 256
#define NIC_RX_CLEAN_MAX_NUM 64

#define RCB_IRQ_NOT_INITED 0
#define RCB_IRQ_INITED 1
#define HNS_BUFFER_SIZE_2048 2048

#define BD_MAX_SEND_SIZE 8191
#define SKB_TMP_LEN(SKB) \
	(((SKB)->transport_header - (SKB)->mac_header) + tcp_hdrlen(SKB))

#define INVALID_TX_RING    0xffff

struct nic_dev_node {
	struct list_head node;
	struct net_device *net_dev;
};

static nic_event_fn_t nic_event_call;
static struct list_head netdev_list;
static struct workqueue_struct *nic_event_work_queue;

static void fill_v2_desc(struct hnae_ring *ring, void *priv,
			 int size, dma_addr_t dma, int frag_end,
			 int buf_num, enum hns_desc_type type, int mtu)
{
	struct hnae_desc *desc = &ring->desc[ring->next_to_use];
	struct hnae_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	struct iphdr *iphdr;
	struct ipv6hdr *ipv6hdr;
	struct sk_buff *skb;
	__be16 protocol;
	u8 bn_pid = 0;
	u8 rrcfv = 0;
	u8 ip_offset = 0;
	u8 tvsvsn = 0;
	u16 mss = 0;
	u8 l4_len = 0;
	u16 paylen = 0;

	desc_cb->priv = priv;
	desc_cb->length = size;
	desc_cb->dma = dma;
	desc_cb->type = type;

	desc->addr = cpu_to_le64(dma);
	desc->tx.send_size = cpu_to_le16((u16)size);

	/*config bd buffer end */
	hnae_set_bit(rrcfv, HNSV2_TXD_VLD_B, 1);
	hnae_set_field(bn_pid, HNSV2_TXD_BUFNUM_M,
		       HNSV2_TXD_BUFNUM_S, buf_num - 1);
	/* field port_id in the tx bd for sending manage pkgs.*/
	hnae_set_field(bn_pid, HNSV2_TXD_PORTID_M,
		       HNSV2_TXD_PORTID_S, ring->q->handle->dport_id);

	if (type == DESC_TYPE_SKB) {
		skb = (struct sk_buff *)priv;

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			skb_reset_mac_len(skb);
			protocol = skb->protocol;
			ip_offset = ETH_HLEN;

			if (protocol == htons(ETH_P_8021Q)) {
				ip_offset += VLAN_HLEN;
				protocol = vlan_get_protocol(skb);
				skb->protocol = protocol;
			}

			if (skb->protocol == htons(ETH_P_IP)) {
				iphdr = ip_hdr(skb);
				hnae_set_bit(rrcfv, HNSV2_TXD_L3CS_B, 1);
				hnae_set_bit(rrcfv, HNSV2_TXD_L4CS_B, 1);

				/* check for tcp/udp header */
				if (iphdr->protocol == IPPROTO_TCP &&
				    skb_is_gso(skb)) {
					hnae_set_bit(tvsvsn,
						     HNSV2_TXD_TSE_B, 1);
					l4_len = tcp_hdrlen(skb);
					mss = skb_shinfo(skb)->gso_size;
					paylen = skb->len - SKB_TMP_LEN(skb);
				}
			} else if (skb->protocol == htons(ETH_P_IPV6)) {
				hnae_set_bit(tvsvsn, HNSV2_TXD_IPV6_B, 1);
				ipv6hdr = ipv6_hdr(skb);
				hnae_set_bit(rrcfv, HNSV2_TXD_L4CS_B, 1);

				/* check for tcp/udp header */
				if ((ipv6hdr->nexthdr == IPPROTO_TCP) &&
				    (skb_is_gso(skb) && skb_is_gso_v6(skb))) {
					hnae_set_bit(tvsvsn,
						     HNSV2_TXD_TSE_B, 1);
					l4_len = tcp_hdrlen(skb);
					mss = skb_shinfo(skb)->gso_size;
					paylen = skb->len - SKB_TMP_LEN(skb);
				}
			}
			desc->tx.ip_offset = ip_offset;
			desc->tx.tse_vlan_snap_v6_sctp_nth = tvsvsn;
			desc->tx.mss = cpu_to_le16(mss);
			desc->tx.l4_len = l4_len;
			desc->tx.paylen = cpu_to_le16(paylen);
		}
	}

	hnae_set_bit(rrcfv, HNSV2_TXD_FE_B, frag_end);

	desc->tx.bn_pid = bn_pid;
	desc->tx.ra_ri_cs_fe_vld = rrcfv;

	ring_ptr_move_fw(ring, next_to_use);
}

static void fill_desc(struct hnae_ring *ring, void *priv,
		      int size, dma_addr_t dma, int frag_end,
		      int buf_num, enum hns_desc_type type, int mtu)
{
	struct hnae_desc *desc = &ring->desc[ring->next_to_use];
	struct hnae_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_use];
	struct sk_buff *skb;
	__be16 protocol;
	u32 ip_offset;
	u32 asid_bufnum_pid = 0;
	u32 flag_ipoffset = 0;

	desc_cb->priv = priv;
	desc_cb->length = size;
	desc_cb->dma = dma;
	desc_cb->type = type;

	desc->addr = cpu_to_le64(dma);
	desc->tx.send_size = cpu_to_le16((u16)size);

	/*config bd buffer end */
	flag_ipoffset |= 1 << HNS_TXD_VLD_B;

	asid_bufnum_pid |= buf_num << HNS_TXD_BUFNUM_S;

	if (type == DESC_TYPE_SKB) {
		skb = (struct sk_buff *)priv;

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			protocol = skb->protocol;
			ip_offset = ETH_HLEN;

			/*if it is a SW VLAN check the next protocol*/
			if (protocol == htons(ETH_P_8021Q)) {
				ip_offset += VLAN_HLEN;
				protocol = vlan_get_protocol(skb);
				skb->protocol = protocol;
			}

			if (skb->protocol == htons(ETH_P_IP)) {
				flag_ipoffset |= 1 << HNS_TXD_L3CS_B;
				/* check for tcp/udp header */
				flag_ipoffset |= 1 << HNS_TXD_L4CS_B;

			} else if (skb->protocol == htons(ETH_P_IPV6)) {
				/* ipv6 has not l3 cs, check for L4 header */
				flag_ipoffset |= 1 << HNS_TXD_L4CS_B;
			}

			flag_ipoffset |= ip_offset << HNS_TXD_IPOFFSET_S;
		}
	}

	flag_ipoffset |= frag_end << HNS_TXD_FE_B;

	desc->tx.asid_bufnum_pid = cpu_to_le16(asid_bufnum_pid);
	desc->tx.flag_ipoffset = cpu_to_le32(flag_ipoffset);

	ring_ptr_move_fw(ring, next_to_use);
}

static void unfill_desc(struct hnae_ring *ring)
{
	ring_ptr_move_bw(ring, next_to_use);
}

static int hns_nic_maybe_stop_tx(
	struct sk_buff **out_skb, int *bnum, struct hnae_ring *ring)
{
	struct sk_buff *skb = *out_skb;
	struct sk_buff *new_skb = NULL;
	int buf_num;

	/* no. of segments (plus a header) */
	buf_num = skb_shinfo(skb)->nr_frags + 1;

	if (unlikely(buf_num > ring->max_desc_num_per_pkt)) {
		if (ring_space(ring) < 1)
			return -EBUSY;

		new_skb = skb_copy(skb, GFP_ATOMIC);
		if (!new_skb)
			return -ENOMEM;

		dev_kfree_skb_any(skb);
		*out_skb = new_skb;
		buf_num = 1;
	} else if (buf_num > ring_space(ring)) {
		return -EBUSY;
	}

	*bnum = buf_num;
	return 0;
}

static int hns_nic_maybe_stop_tso(
	struct sk_buff **out_skb, int *bnum, struct hnae_ring *ring)
{
	int i;
	int size;
	int buf_num;
	int frag_num;
	struct sk_buff *skb = *out_skb;
	struct sk_buff *new_skb = NULL;
	struct skb_frag_struct *frag;

	size = skb_headlen(skb);
	buf_num = (size + BD_MAX_SEND_SIZE - 1) / BD_MAX_SEND_SIZE;

	frag_num = skb_shinfo(skb)->nr_frags;
	for (i = 0; i < frag_num; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		size = skb_frag_size(frag);
		buf_num += (size + BD_MAX_SEND_SIZE - 1) / BD_MAX_SEND_SIZE;
	}

	if (unlikely(buf_num > ring->max_desc_num_per_pkt)) {
		buf_num = (skb->len + BD_MAX_SEND_SIZE - 1) / BD_MAX_SEND_SIZE;
		if (ring_space(ring) < buf_num)
			return -EBUSY;
		/* manual split the send packet */
		new_skb = skb_copy(skb, GFP_ATOMIC);
		if (!new_skb)
			return -ENOMEM;
		dev_kfree_skb_any(skb);
		*out_skb = new_skb;

	} else if (ring_space(ring) < buf_num) {
		return -EBUSY;
	}

	*bnum = buf_num;
	return 0;
}

static void fill_tso_desc(struct hnae_ring *ring, void *priv,
			  int size, dma_addr_t dma, int frag_end,
			  int buf_num, enum hns_desc_type type, int mtu)
{
	int frag_buf_num;
	int sizeoflast;
	int k;

	frag_buf_num = (size + BD_MAX_SEND_SIZE - 1) / BD_MAX_SEND_SIZE;
	sizeoflast = size % BD_MAX_SEND_SIZE;
	sizeoflast = sizeoflast ? sizeoflast : BD_MAX_SEND_SIZE;

	/* when the frag size is bigger than hardware, split this frag */
	for (k = 0; k < frag_buf_num; k++)
		fill_v2_desc(ring, priv,
			     (k == frag_buf_num - 1) ?
					sizeoflast : BD_MAX_SEND_SIZE,
			     dma + BD_MAX_SEND_SIZE * k,
			     frag_end && (k == frag_buf_num - 1) ? 1 : 0,
			     buf_num,
			     (type == DESC_TYPE_SKB && !k) ?
					DESC_TYPE_SKB : DESC_TYPE_PAGE,
			     mtu);
}

netdev_tx_t hns_nic_net_xmit_hw(struct net_device *ndev,
				struct sk_buff *skb,
				struct hns_nic_ring_data *ring_data)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct device *dev = priv->dev;
	struct hnae_ring *ring = ring_data->ring;
	struct netdev_queue *dev_queue;
	struct skb_frag_struct *frag;
	int buf_num;
	int seg_num;
	dma_addr_t dma;
	int size, next_to_use;
	int i;

	switch (priv->ops.maybe_stop_tx(&skb, &buf_num, ring)) {
	case -EBUSY:
		ring->stats.tx_busy++;
		goto out_net_tx_busy;
	case -ENOMEM:
		ring->stats.sw_err_cnt++;
		netdev_err(ndev, "no memory to xmit!\n");
		goto out_err_tx_ok;
	default:
		break;
	}

	/* no. of segments (plus a header) */
	seg_num = skb_shinfo(skb)->nr_frags + 1;
	next_to_use = ring->next_to_use;

	/* fill the first part */
	size = skb_headlen(skb);
	dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma)) {
		netdev_err(ndev, "TX head DMA map failed\n");
		ring->stats.sw_err_cnt++;
		goto out_err_tx_ok;
	}
	priv->ops.fill_desc(ring, skb, size, dma, seg_num == 1 ? 1 : 0,
			    buf_num, DESC_TYPE_SKB, ndev->mtu);

	/* fill the fragments */
	for (i = 1; i < seg_num; i++) {
		frag = &skb_shinfo(skb)->frags[i - 1];
		size = skb_frag_size(frag);
		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma)) {
			netdev_err(ndev, "TX frag(%d) DMA map failed\n", i);
			ring->stats.sw_err_cnt++;
			goto out_map_frag_fail;
		}
		priv->ops.fill_desc(ring, skb_frag_page(frag), size, dma,
				    seg_num - 1 == i ? 1 : 0, buf_num,
				    DESC_TYPE_PAGE, ndev->mtu);
	}

	/*complete translate all packets*/
	dev_queue = netdev_get_tx_queue(ndev, skb->queue_mapping);
	netdev_tx_sent_queue(dev_queue, skb->len);

	ndev->trans_start = jiffies;
	ndev->stats.tx_bytes += skb->len;
	ndev->stats.tx_packets++;

	wmb(); /* commit all data before submit */
	assert(skb->queue_mapping < priv->ae_handle->q_num);
	hnae_queue_xmit(priv->ae_handle->qs[skb->queue_mapping], buf_num);
	ring->stats.tx_pkts++;
	ring->stats.tx_bytes += skb->len;

	return NETDEV_TX_OK;

out_map_frag_fail:

	while (ring->next_to_use != next_to_use) {
		unfill_desc(ring);
		if (ring->next_to_use != next_to_use)
			dma_unmap_page(dev,
				       ring->desc_cb[ring->next_to_use].dma,
				       ring->desc_cb[ring->next_to_use].length,
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(dev,
					 ring->desc_cb[next_to_use].dma,
					 ring->desc_cb[next_to_use].length,
					 DMA_TO_DEVICE);
	}

out_err_tx_ok:

	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;

out_net_tx_busy:

	netif_stop_subqueue(ndev, skb->queue_mapping);

	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it.
	 */
	smp_mb();
	return NETDEV_TX_BUSY;
}

/**
 * hns_nic_get_headlen - determine size of header for RSC/LRO/GRO/FCOE
 * @data: pointer to the start of the headers
 * @max: total length of section to find headers in
 *
 * This function is meant to determine the length of headers that will
 * be recognized by hardware for LRO, GRO, and RSC offloads.  The main
 * motivation of doing this is to only perform one pull for IPv4 TCP
 * packets so that we can do basic things like calculating the gso_size
 * based on the average data per packet.
 **/
static unsigned int hns_nic_get_headlen(unsigned char *data, u32 flag,
					unsigned int max_size)
{
	unsigned char *network;
	u8 hlen;

	/* this should never happen, but better safe than sorry */
	if (max_size < ETH_HLEN)
		return max_size;

	/* initialize network frame pointer */
	network = data;

	/* set first protocol and move network header forward */
	network += ETH_HLEN;

	/* handle any vlan tag if present */
	if (hnae_get_field(flag, HNS_RXD_VLAN_M, HNS_RXD_VLAN_S)
		== HNS_RX_FLAG_VLAN_PRESENT) {
		if ((typeof(max_size))(network - data) > (max_size - VLAN_HLEN))
			return max_size;

		network += VLAN_HLEN;
	}

	/* handle L3 protocols */
	if (hnae_get_field(flag, HNS_RXD_L3ID_M, HNS_RXD_L3ID_S)
		== HNS_RX_FLAG_L3ID_IPV4) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct iphdr)))
			return max_size;

		/* access ihl as a u8 to avoid unaligned access on ia64 */
		hlen = (network[0] & 0x0F) << 2;

		/* verify hlen meets minimum size requirements */
		if (hlen < sizeof(struct iphdr))
			return network - data;

		/* record next protocol if header is present */
	} else if (hnae_get_field(flag, HNS_RXD_L3ID_M, HNS_RXD_L3ID_S)
		== HNS_RX_FLAG_L3ID_IPV6) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct ipv6hdr)))
			return max_size;

		/* record next protocol */
		hlen = sizeof(struct ipv6hdr);
	} else {
		return network - data;
	}

	/* relocate pointer to start of L4 header */
	network += hlen;

	/* finally sort out TCP/UDP */
	if (hnae_get_field(flag, HNS_RXD_L4ID_M, HNS_RXD_L4ID_S)
		== HNS_RX_FLAG_L4ID_TCP) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct tcphdr)))
			return max_size;

		/* access doff as a u8 to avoid unaligned access on ia64 */
		hlen = (network[12] & 0xF0) >> 2;

		/* verify hlen meets minimum size requirements */
		if (hlen < sizeof(struct tcphdr))
			return network - data;

		network += hlen;
	} else if (hnae_get_field(flag, HNS_RXD_L4ID_M, HNS_RXD_L4ID_S)
		== HNS_RX_FLAG_L4ID_UDP) {
		if ((typeof(max_size))(network - data) >
		    (max_size - sizeof(struct udphdr)))
			return max_size;

		network += sizeof(struct udphdr);
	}

	/* If everything has gone correctly network should be the
	 * data section of the packet and will be the end of the header.
	 * If not then it probably represents the end of the last recognized
	 * header.
	 */
	if ((typeof(max_size))(network - data) < max_size)
		return network - data;
	else
		return max_size;
}

static void hns_nic_reuse_page(struct sk_buff *skb, int i,
			       struct hnae_ring *ring, int pull_len,
			       struct hnae_desc_cb *desc_cb)
{
	struct hnae_desc *desc;
	int truesize, size;
	int last_offset = 0;
	bool twobufs;

	desc = &ring->desc[ring->next_to_clean];
	size = le16_to_cpu(desc->rx.size);

	twobufs = ((PAGE_SIZE < 8192) && hnae_buf_size(ring) == HNS_BUFFER_SIZE_2048);
	if (twobufs)  {
		truesize = hnae_buf_size(ring);
	} else {
		truesize = ALIGN(size, L1_CACHE_BYTES);
		last_offset = hnae_page_size(ring) - hnae_buf_size(ring);
	}

	skb_add_rx_frag(skb, i, desc_cb->priv, desc_cb->page_offset + pull_len,
			size - pull_len, truesize - pull_len);

	/* avoid re-using remote pages,flag default unreuse */
	if (unlikely(page_to_nid(desc_cb->priv) != numa_node_id()))
		return;

	if (twobufs) {
		/* if we are only owner of page we can reuse it */
		if (likely(page_count(desc_cb->priv) == 1)) {
			/* flip page offset to other buffer */
			desc_cb->page_offset ^= truesize;
			desc_cb->reuse_flag = 1;

			/* bump ref count on page before it is given*/
			get_page(desc_cb->priv);
		}
		return;
	}

	/* move offset up to the next cache line */
	desc_cb->page_offset += truesize;

	if (desc_cb->page_offset <= last_offset) {
		desc_cb->reuse_flag = 1;
		/* bump ref count on page before it is given*/
		get_page(desc_cb->priv);
	}
}

static void get_vlan_v2(u32 vlan_flag, u16 *vlan)
{
	u16 vid = hnae_get_field(vlan_flag, HNSV2_RXD_VLANID_M,
				 HNSV2_RXD_VLANID_S);
	*vlan = le16_to_cpu(vid);
}

static void get_vlan(u32 vlan_flag, u16 *vlan)
{
	u16 vid = hnae_get_field(vlan_flag, HNS_RXD_VLANID_M,
				 HNS_RXD_VLANID_S);
	*vlan = le16_to_cpu(vid);
}

static void get_v2rx_desc_bnum(u32 bnum_flag, int *out_bnum)
{
	*out_bnum = hnae_get_field(bnum_flag,
				   HNS_RXD_BUFNUM_M, HNS_RXD_BUFNUM_S) + 1;
}

static void get_rx_desc_bnum(u32 bnum_flag, int *out_bnum)
{
	*out_bnum = hnae_get_field(bnum_flag,
				   HNS_RXD_BUFNUM_M, HNS_RXD_BUFNUM_S);
}

static int hns_nic_is_mgnt_pkt(struct sk_buff *skb, struct net_device *ndev)
{
	struct ethhdr *eth_hdr = (struct ethhdr *)skb->data;

	static const unsigned char
		lldp_pkt_dmac[ETH_ALEN] = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x0E};
	static const unsigned char
		bpdu_pkt_dmac[ETH_ALEN] = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x00};

	/* the rx mode is promisc */
	if (ndev->flags & IFF_PROMISC)
		return 0;

	/* check if dmac_addr equals to management packet dmac_addr */
	if (likely(memcmp(eth_hdr->h_dest, lldp_pkt_dmac, ETH_ALEN) &&
		   memcmp(eth_hdr->h_dest, bpdu_pkt_dmac, ETH_ALEN)))
		return 0;
	else
		return 1;
}

static int hns_nic_poll_rx_skb(struct hns_nic_ring_data *ring_data,
			       struct sk_buff **out_skb, int *out_bnum)
{
	struct hnae_ring *ring = ring_data->ring;
	struct net_device *ndev = ring_data->napi.dev;
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	struct hnae_desc *desc;
	struct hnae_desc_cb *desc_cb;
	unsigned char *va;
	int bnum, length, i;
	int pull_len;
	u32 bnum_flag;
	u32 vlan_flag;
	u16 vid;

	desc = &ring->desc[ring->next_to_clean];
	desc_cb = &ring->desc_cb[ring->next_to_clean];

	prefetch(desc);

	va = (unsigned char *)desc_cb->buf + desc_cb->page_offset;

	/* prefetch first cache line of first page */
	prefetch(va);
#if L1_CACHE_BYTES < 128
	prefetch(va + L1_CACHE_BYTES);
#endif

	skb = *out_skb = napi_alloc_skb(&ring_data->napi,
					HNS_RX_HEAD_SIZE);
	if (unlikely(!skb)) {
		netdev_err(ndev, "alloc rx skb fail\n");
		ring->stats.sw_err_cnt++;
		return -ENOMEM;
	}

	prefetchw(skb->data);
	length = le16_to_cpu(desc->rx.pkt_len);
	bnum_flag = le32_to_cpu(desc->rx.ipoff_bnum_pid_flag);
	priv->ops.get_rxd_bnum(bnum_flag, &bnum);
	vlan_flag = le32_to_cpu(desc->rx.vlan_pri_asid);
	priv->ops.get_vlan(vlan_flag, &vid);
	*out_bnum = bnum;

	if (length <= HNS_RX_HEAD_SIZE) {
		memcpy(__skb_put(skb, length), va, ALIGN(length, sizeof(long)));

		/* we can reuse buffer as-is, just make sure it is local */
		if (likely(page_to_nid(desc_cb->priv) == numa_node_id()))
			desc_cb->reuse_flag = 1;
		else /* this page cannot be reused so discard it */
			put_page(desc_cb->priv);

		ring_ptr_move_fw(ring, next_to_clean);

		if (unlikely(bnum != 1)) { /* check err*/
			*out_bnum = 1;
			goto out_bnum_err;
		}
	} else {
		ring->stats.seg_pkt_cnt++;

		pull_len = hns_nic_get_headlen(va, bnum_flag, HNS_RX_HEAD_SIZE);
		memcpy(__skb_put(skb, pull_len), va,
		       ALIGN(pull_len, sizeof(long)));

		hns_nic_reuse_page(skb, 0, ring, pull_len, desc_cb);
		ring_ptr_move_fw(ring, next_to_clean);

		if (unlikely(bnum >= (int)MAX_SKB_FRAGS)) { /* check err*/
			*out_bnum = 1;
			goto out_bnum_err;
		}
		for (i = 1; i < bnum; i++) {
			desc = &ring->desc[ring->next_to_clean];
			desc_cb = &ring->desc_cb[ring->next_to_clean];

			hns_nic_reuse_page(skb, i, ring, 0, desc_cb);
			ring_ptr_move_fw(ring, next_to_clean);
		}
	}

	/* filter out management packet */
	if (unlikely(hns_nic_is_mgnt_pkt(skb, ndev))) {
		dev_kfree_skb_any(skb);
		return -EFAULT;
	}

	/* check except process, free skb and jump the desc */
	if (unlikely((!bnum) || (bnum > ring->max_desc_num_per_pkt))) {
out_bnum_err:
		*out_bnum = *out_bnum ? *out_bnum : 1; /* ntc moved,cannot 0*/
		netdev_err(ndev, "invalid bnum(%d,%d,%d,%d),%016llx,%016llx\n",
			   bnum, ring->max_desc_num_per_pkt,
			   length, (int)MAX_SKB_FRAGS,
			   ((u64 *)desc)[0], ((u64 *)desc)[1]);
		ring->stats.err_bd_num++;
		dev_kfree_skb_any(skb);
		return -EDOM;
	}

	if (unlikely(!hnae_get_bit(bnum_flag, HNS_RXD_VLD_B))) {
		netdev_err(ndev, "no valid bd,%016llx,%016llx\n",
			   ((u64 *)desc)[0], ((u64 *)desc)[1]);
		ring->stats.non_vld_descs++;
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	if (unlikely((!desc->rx.pkt_len) ||
		     hnae_get_bit(bnum_flag, HNS_RXD_DROP_B))) {
		ring->stats.err_pkt_len++;
		dev_kfree_skb_any(skb);
		return -EFAULT;
	}

	if (unlikely(hnae_get_bit(bnum_flag, HNS_RXD_L2E_B))) {
		ring->stats.l2_err++;
		dev_kfree_skb_any(skb);
		return -EFAULT;
	}

	ring->stats.rx_pkts++;
	ring->stats.rx_bytes += skb->len;

	if (unlikely(hnae_get_bit(bnum_flag, HNS_RXD_L3E_B) ||
		     hnae_get_bit(bnum_flag, HNS_RXD_L4E_B))) {
		ring->stats.l3l4_csum_err++;
		return 0;
	}

	skb->ip_summed = CHECKSUM_UNNECESSARY;
	if ((ndev->features & NETIF_F_HW_VLAN_CTAG_RX) && (vid > 0)) {
		/* filter pkts tagged with inactive vlan ids when netdev
		 * in non-promisc mode
		 */
		if (!(ndev->flags & IFF_PROMISC) &&
		    !test_bit(vid, priv->active_vlans)) {
			ring->stats.rx_pkts--;
			ring->stats.rx_bytes -= skb->len;
			dev_kfree_skb_any(skb);
			return -EFAULT;
		}
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);
	}

	return 0;
}

static void
hns_nic_alloc_rx_buffers(struct hns_nic_ring_data *ring_data, int cleand_count)
{
	int i, ret;
	struct hnae_desc_cb res_cbs;
	struct hnae_desc_cb *desc_cb;
	struct hnae_ring *ring = ring_data->ring;
	struct net_device *ndev = ring_data->napi.dev;

	cleand_count += ring->desc_leak;

	for (i = 0; i < cleand_count; i++) {
		desc_cb = &ring->desc_cb[ring->next_to_use];
		if (desc_cb->reuse_flag) {
			ring->stats.reuse_pg_cnt++;
			hnae_reuse_buffer(ring, ring->next_to_use);
		} else {
			ret = hnae_reserve_buffer_map(ring, &res_cbs);
			if (ret) {
				ring->stats.sw_err_cnt++;
				netdev_err(ndev, "hnae reserve buffer map failed.\n");
				break;
			}
			hnae_replace_buffer(ring, ring->next_to_use, &res_cbs);
		}

		ring_ptr_move_fw(ring, next_to_use);
	}

	ring->desc_leak = cleand_count - i;

	wmb(); /* make all data has been write before submit */
	writel_relaxed(i, ring->io_base + RCB_REG_HEAD);
}

/* return error number for error or number of desc left to take
 */
static void hns_nic_rx_up_pro(struct hns_nic_ring_data *ring_data,
			      struct sk_buff *skb)
{
	struct net_device *ndev = ring_data->napi.dev;

	skb->protocol = eth_type_trans(skb, ndev);
	(void)napi_gro_receive(&ring_data->napi, skb);
	ndev->last_rx = jiffies;
}

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
};

static void hns_update_rate_lvl(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	struct hnae_handle *handle = ring_data->ring->q->handle;
	u32 bytes = ring->total_bytes;
	u32 timepassed_us = handle->coal_param;
	u8 rate_lvl = ring->rate_lvl;
	u64 bytes_perint;

	/* simple throttlerate management
	 *   0-10MB/s   lowest (100000 ints/s)
	 *  10-20MB/s   low    (20000 ints/s)
	 *  20-1249MB/s bulk   (8000 ints/s)
	 */
	bytes_perint = bytes / timepassed_us / ring->update_freq;

	switch (ring->rate_lvl) {
	case lowest_latency:
		if (bytes_perint > 10)
			rate_lvl = low_latency;
		break;
	case low_latency:
		if (bytes_perint > 20)
			rate_lvl = bulk_latency;
		else if (bytes_perint <= 10)
			rate_lvl = lowest_latency;
		break;
	default:
		if (bytes_perint <= 20)
			rate_lvl = low_latency;
		break;
	}

	/* clear work counters since we have the values we need */
	ring->total_bytes = 0;

	/* write updated itr to ring container */
	ring->rate_lvl = rate_lvl;
}

/**
 * hns_nic_adp_coalesce - self adapte coalesce according to flow rate
 * @ring_data: pointer to hns_nic_ring_data
 *
 * This function is meant to adapte the coalesce pramameters according
 * to flow rate.
 **/
static void hns_nic_adpt_coalesce(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	struct hnae_handle *handle = ring->q->handle;
	u32 new_coal_param = ring->coal_param;
	u32 old_coal_param = ring->coal_param;
	u32 current_jiffies = jiffies;

	/* control update freq */
	if (ring->update_freq++ < 16)
		return;

	hns_update_rate_lvl(ring_data);
	ring->update_freq = 0;

	switch (ring->rate_lvl) {
	case lowest_latency:
		new_coal_param = HNAE_LOWEST_LATENCY_COAL_PARAM;
		break;
	case low_latency:
		new_coal_param = HNAE_LOW_LATENCY_COAL_PARAM;
		break;
	case bulk_latency:
		new_coal_param = HNAE_BULK_LATENCY_COAL_PARAM;
		break;
	default:
		break;
	}

	if (new_coal_param != old_coal_param) {
		/* do an exponential smoothing */
		new_coal_param = (10 * new_coal_param * old_coal_param) /
				 ((9 * new_coal_param) + old_coal_param - 10);

		ring->coal_param = new_coal_param;
	}

	/* if this ring adapts the parameter at last, should keep alive */
	if (handle->adapt_ring_idx == ring_data->queue_index)
		handle->update_time = current_jiffies;

	/**
	 * Because all ring in one port has one coalesce param, when one ring
	 * calculate its own coalesce param, it cannot write to hardware at
	 * once. There are three conditions as follows:
	 *       1. current ring's coalesce param is larger than the hardware.
	 *       2. otherwise, the ring which adapt last time can change again.
	 *       3. when it timeout.
	 * Any conditions is true, the current ring's coalesce param will write
	 * to hardware.
	 */
	if (new_coal_param > handle->coal_param ||
	    (new_coal_param < handle->coal_param &&
	     handle->adapt_ring_idx == ring_data->queue_index) ||
	    current_jiffies - handle->update_time > HZ) {
		if (new_coal_param != handle->coal_param) {
			spin_lock_bh(&handle->coal_set_lock);
			if (is_coal_adapt(ring)) {
				handle->dev->ops->set_coalesce_usecs(handle,
							new_coal_param);
				handle->dev->ops->set_coalesce_frames(handle,
							1, new_coal_param);
				handle->coal_param = new_coal_param;
				handle->adapt_ring_idx = ring_data->queue_index;
			}
			spin_unlock_bh(&handle->coal_set_lock);
		}
		handle->update_time = jiffies;
	}
}

static int hns_nic_rx_poll_one(struct hns_nic_ring_data *ring_data,
			       int budget, void *v)
{
	struct hnae_ring *ring = ring_data->ring;
	struct sk_buff *skb;
	int num, bnum;
#define RCB_NOF_ALLOC_RX_BUFF_ONCE 16
	int recv_pkts, recv_bds, clean_count, err, total_bytes;

	num = readl_relaxed(ring->io_base + RCB_REG_FBDNUM) - ring->desc_leak;
	rmb(); /* make sure num taken effect before the other data is touched */

	recv_pkts = 0, recv_bds = 0, clean_count = 0, total_bytes = 0;

	while (recv_pkts < budget && recv_bds < num) {
		/* reuse or realloc buffers */
		if (clean_count >= RCB_NOF_ALLOC_RX_BUFF_ONCE) {
			hns_nic_alloc_rx_buffers(ring_data, clean_count);
			clean_count = 0;
		}

		/* poll one pkt */
		err = hns_nic_poll_rx_skb(ring_data, &skb, &bnum);
		if (unlikely(!skb)) /* this fault cannot be repaired */
			goto out;

		recv_bds += bnum;
		clean_count += bnum;
		if (unlikely(err)) {  /* do jump the err */
			recv_pkts++;
			continue;
		}

		/* tcp ack don't check */
		if (skb->len > 66)
			total_bytes += skb->len;

		/* do update ip stack process*/
		((void (*)(struct hns_nic_ring_data *, struct sk_buff *))v)(
							ring_data, skb);
		recv_pkts++;
	}

out:
	/* make all data has been write before submit */
	if (clean_count + ring->desc_leak > 0)
		hns_nic_alloc_rx_buffers(ring_data, clean_count);

	ring_data->ring->total_bytes += total_bytes;

	return recv_pkts;
}

static bool hns_nic_rx_fini_pro_v2(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	int num;

	num = readl_relaxed(ring->io_base + RCB_REG_FBDNUM);

	if (num < 4) {
		if (is_coal_adapt(ring))
			hns_nic_adpt_coalesce(ring_data);

		return true;
	} else {
		return false;
	}
}

static bool hns_nic_rx_fini_pro(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	int num = 0;

	ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
		ring_data->ring, 0);

	/* for hardware bug fixed */
	num = readl_relaxed(ring->io_base + RCB_REG_FBDNUM);

	if (num > 0) {
		ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
			ring_data->ring, 1);

		return false;
	} else {
		return true;
	}
}

static inline void hns_nic_reclaim_one_desc(struct hnae_ring *ring,
					    int *bytes, int *pkts)
{
	struct hnae_desc_cb *desc_cb = &ring->desc_cb[ring->next_to_clean];

	(*pkts) += (desc_cb->type == DESC_TYPE_SKB);
	(*bytes) += desc_cb->length;
	/* desc_cb will be cleaned, after hnae_free_buffer_detach*/
	hnae_free_buffer_detach(ring, ring->next_to_clean);

	ring_ptr_move_fw(ring, next_to_clean);
}

static int is_valid_clean_head(struct hnae_ring *ring, int h)
{
	int u = ring->next_to_use;
	int c = ring->next_to_clean;

	if (unlikely(h >= ring->desc_num || c >= ring->desc_num))
		return 0;

	assert(u > 0 && u < ring->desc_num);
	assert(c > 0 && c < ring->desc_num);
	assert(u != c && h != c); /* must be checked before call this func */

	return u > c ? (h > c && h <= u) : (h > c || h <= u);
}

/* netif_tx_lock will turn down the performance, set only when necessary */
#ifdef CONFIG_NET_POLL_CONTROLLER
#define NETIF_TX_LOCK(ring) spin_lock(&ring->lock)
#define NETIF_TX_UNLOCK(ring) spin_unlock(&ring->lock)
#else
#define NETIF_TX_LOCK(ring)
#define NETIF_TX_UNLOCK(ring)
#endif
/* reclaim all desc in one budget
 * return error or number of desc left
 */
static int hns_nic_tx_poll_one(struct hns_nic_ring_data *ring_data,
			       int budget, void *v)
{
	struct hnae_ring *ring = ring_data->ring;
	struct net_device *ndev = ring_data->napi.dev;
	struct netdev_queue *dev_queue;
	struct hns_nic_priv *priv = netdev_priv(ndev);
	int head;
	int bytes, pkts;

	NETIF_TX_LOCK(ring);

	head = readl_relaxed(ring->io_base + RCB_REG_HEAD);
	rmb(); /* make sure head is ready before touch any data */

	if (is_ring_empty(ring) || head == ring->next_to_clean) {
		NETIF_TX_UNLOCK(ring);
		return 0; /* no data to poll */
	}

	if (!is_valid_clean_head(ring, head)) {
		netdev_err(ndev, "wrong head (%d, %d-%d)\n", head,
			   ring->next_to_use, ring->next_to_clean);
		ring->stats.io_err_cnt++;
		NETIF_TX_UNLOCK(ring);
		return -EIO;
	}

	bytes = 0;
	pkts = 0;
	while (pkts < budget && head != ring->next_to_clean) {
		hns_nic_reclaim_one_desc(ring, &bytes, &pkts);
		/* issue prefetch for next Tx descriptor */
		prefetch(&ring->desc_cb[ring->next_to_clean]);
	}

	NETIF_TX_UNLOCK(ring);

	dev_queue = netdev_get_tx_queue(ndev, ring_data->queue_index);
	netdev_tx_completed_queue(dev_queue, pkts, bytes);

	if (unlikely(pkts && netif_carrier_ok(ndev) &&
		     (ring_space(ring) >= ring->max_desc_num_per_pkt * 2))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (netif_tx_queue_stopped(dev_queue) &&
		    !test_bit(NIC_STATE_DOWN, &priv->state)) {
			netif_tx_wake_queue(dev_queue);
			ring->stats.restart_queue++;
		}
	}
	return pkts;
}

static bool hns_nic_tx_fini_pro_v2(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	int head = readl_relaxed(ring->io_base + RCB_REG_HEAD);

	if (head == ring->next_to_clean)
		return true;
	else
		return false;
}

static bool hns_nic_tx_fini_pro(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	int head;

	ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
		ring_data->ring, 0);

	head = readl_relaxed(ring->io_base + RCB_REG_HEAD);

	if (head != ring->next_to_clean) {
		ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
			ring_data->ring, 1);

		return false;
	} else {
		return true;
	}
}

static void hns_nic_tx_clr_all_bufs(struct hns_nic_ring_data *ring_data)
{
	struct hnae_ring *ring = ring_data->ring;
	struct net_device *ndev = ring_data->napi.dev;
	struct netdev_queue *dev_queue;
	int head;
	int bytes, pkts;

	NETIF_TX_LOCK(ring);

	head = ring->next_to_use; /* ntu :soft setted ring position*/
	bytes = 0;
	pkts = 0;
	while (head != ring->next_to_clean)
		hns_nic_reclaim_one_desc(ring, &bytes, &pkts);

	NETIF_TX_UNLOCK(ring);

	dev_queue = netdev_get_tx_queue(ndev, ring_data->queue_index);
	netdev_tx_reset_queue(dev_queue);
}

static int hns_nic_common_poll(struct napi_struct *napi, int budget)
{
	int clean_complete = 0;
	struct hns_nic_ring_data *ring_data =
		container_of(napi, struct hns_nic_ring_data, napi);
	struct hnae_ring *ring = ring_data->ring;

try_again:
	clean_complete += ring_data->poll_one(
				ring_data, budget - clean_complete,
				ring_data->ex_process);

	if (clean_complete < budget) {
		if (ring_data->fini_process(ring_data)) {
			napi_complete(napi);
			ring->q->handle->dev->ops->toggle_ring_irq(ring, 0);
		} else {
			goto try_again;
		}
	}

	return clean_complete;
}

static irqreturn_t hns_irq_handle(int irq, void *dev)
{
	struct hns_nic_ring_data *ring_data = (struct hns_nic_ring_data *)dev;

	ring_data->ring->q->handle->dev->ops->toggle_ring_irq(
		ring_data->ring, 1);
	napi_schedule(&ring_data->napi);

	return IRQ_HANDLED;
}

/**
 *hns_nic_adjust_link - adjust net work mode by the phy stat or new param
 *Becareful, hns_nic_cancel_phy_dwork_sync can not be involved in this func.
 *because the func perhaps already in delayed work.
 *@ndev: net device
 */
static void hns_nic_adjust_link(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	struct phy_device *phydev = priv->phy;
	u8 oldflowctrl, newflowctrl;
	u32 lcl_adv, rmt_adv = 0;
	int ret;

	/* When phy link down, do nothing */
	if (phydev->link == 0)
		return;

	if (h->dev->ops->need_adjust_link(h, ndev->phydev->speed,
					  ndev->phydev->duplex)) {
		/* because Hi161X chip don't support to change gmac
		 * speed and duplex with traffic. Delay 200ms to
		 * make sure there is no more data in chip FIFO.
		 */
		netif_carrier_off(ndev);
		msleep(200);
		h->dev->ops->adjust_link(h, ndev->phydev->speed,
					 ndev->phydev->duplex);
		netif_carrier_on(ndev);
	}

	if (!AE_IS_VER1(priv->enet_ver)) {
		spin_lock_bh(&priv->lock);
		if (priv->link_config.pause_autoneg != AUTONEG_ENABLE ||
		    phydev->autoneg != AUTONEG_ENABLE)
			goto flow_an_disable;

		oldflowctrl = priv->link_config.active_flowctrl;

		/* Only when link up and duplex full mode support MAC pause. */
		if (phydev->link && phydev->duplex == DUPLEX_FULL) {
			lcl_adv =
			mii_advertise_flowctrl(priv->link_config.flowctrl);

			if (phydev->pause)
				rmt_adv = LPA_PAUSE_CAP;
			if (phydev->asym_pause)
				rmt_adv |= LPA_PAUSE_ASYM;

			newflowctrl =
			mii_resolve_flowctrl_fdx(lcl_adv, rmt_adv);

			if (oldflowctrl == newflowctrl)
				goto end;

			ret = h->dev->ops->set_pauseparam(h,
				priv->link_config.pause_autoneg,
				newflowctrl & FLOW_CTRL_RX,
				newflowctrl & FLOW_CTRL_TX);
			if (ret)
				goto end;

			priv->link_config.active_flowctrl = newflowctrl;
		}

		spin_unlock_bh(&priv->lock);
		return;

flow_an_disable:
	if (priv->link_config.active_flowctrl == priv->link_config.flowctrl)
		goto end;

	ret = h->dev->ops->set_pauseparam(priv->ae_handle, AUTONEG_DISABLE,
				   priv->link_config.flowctrl & FLOW_CTRL_RX,
				   priv->link_config.flowctrl & FLOW_CTRL_TX);
	if (ret)
		goto end;

	priv->link_config.active_flowctrl = priv->link_config.flowctrl;

end:
	spin_unlock_bh(&priv->lock);
	return;
	}
}

/**
 *hns_nic_init_phy - init phy
 *@ndev: net device
 *@h: ae handle
 * Return 0 on success, negative on failure
 */
int hns_nic_init_phy(struct net_device *ndev, struct hnae_handle *h)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct phy_device *phy_dev = NULL;

	if (!h->phy_node)
		return 0;

	if (h->phy_if != PHY_INTERFACE_MODE_XGMII)
		phy_dev = of_phy_connect(ndev, h->phy_node,
					 hns_nic_adjust_link, 0, h->phy_if);
	else
		phy_dev = of_phy_attach(ndev, h->phy_node, 0, h->phy_if);

	if (unlikely(!phy_dev) || IS_ERR(phy_dev))
		return !phy_dev ? -ENODEV : PTR_ERR(phy_dev);

	phy_dev->supported &= h->if_support;
	phy_dev->supported |= SUPPORTED_Pause | SUPPORTED_Asym_Pause;
	phy_dev->advertising = phy_dev->supported;

	if (h->phy_if == PHY_INTERFACE_MODE_XGMII)
		phy_dev->autoneg = false;

	/* Stop phy in probe. */
	if (h->phy_if == PHY_INTERFACE_MODE_SGMII)
		phy_stop(phy_dev);

	h->phy_dev = phy_dev;
	priv->phy = phy_dev;

	return 0;
}

static int hns_nic_ring_open(struct net_device *netdev, int idx)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	napi_enable(&priv->ring_data[idx].napi);

	enable_irq(priv->ring_data[idx].ring->irq);
	h->dev->ops->toggle_ring_irq(priv->ring_data[idx].ring, 0);

	return 0;
}

static int hns_nic_net_set_mac_address(struct net_device *ndev, void *p)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	struct sockaddr *mac_addr = p;
	int ret;

	if (!mac_addr || !is_valid_ether_addr((const u8 *)mac_addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = h->dev->ops->set_mac_addr(h, mac_addr->sa_data);
	if (ret) {
		netdev_err(ndev, "set_mac_address fail, ret=%d!\n", ret);
		return ret;
	}

	memcpy(ndev->dev_addr, mac_addr->sa_data, ndev->addr_len);

	return 0;
}

static int
hns_nic_vlan_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;
	int ret;

	ret = h->dev->ops->add_rx_vlan(h, vid);
	if (ret) {
		netdev_err(netdev, "add_rx_vlan fail, ret=%d!\n", ret);
		return ret;
	}

	/* add vid to vlan bitmap */
	set_bit(vid, priv->active_vlans);

	return 0;
}

static int
hns_nic_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;
	int ret;

	ret = h->dev->ops->del_rx_vlan(h, vid);
	if (ret) {
		netdev_err(netdev, "del_rx_vlan fail, ret=%d!\n", ret);
		return ret;
	}

	/* remove vid from vlan bitmap */
	clear_bit(vid, priv->active_vlans);

	return 0;
}

void hns_nic_update_stats(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	h->dev->ops->update_stats(h, &netdev->stats);
}

/* set mac addr if it is configed. or leave it to the AE driver */
static void hns_init_mac_addr(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct device_node *node = priv->dev->of_node;
	const void *mac_addr_temp;

	mac_addr_temp = of_get_mac_address(node);
	if (mac_addr_temp && is_valid_ether_addr(mac_addr_temp)) {
		memcpy(ndev->dev_addr, mac_addr_temp, ndev->addr_len);
	} else {
		eth_hw_addr_random(ndev);
		dev_warn(priv->dev, "No valid mac, use random mac %pM",
			 ndev->dev_addr);
	}
}

static void hns_nic_ring_close(struct net_device *netdev, int idx)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	h->dev->ops->toggle_ring_irq(priv->ring_data[idx].ring, 1);
	disable_irq(priv->ring_data[idx].ring->irq);

	napi_disable(&priv->ring_data[idx].napi);
}

static int hns_nic_init_affinity_mask(int q_num, int ring_idx,
				      struct hnae_ring *ring, cpumask_t *mask)
{
	int cpu;

	/* Diffrent irq banlance between 16core and 32core.
	 * The cpu mask set by ring index according to the ring flag
	 * which indicate the ring is tx or rx.
	 */
	if (q_num == num_possible_cpus()) {
		if (is_tx_ring(ring))
			cpu = ring_idx;
		else
			cpu = ring_idx - q_num;
	} else {
		if (is_tx_ring(ring))
			cpu = ring_idx * 2;
		else
			cpu = (ring_idx - q_num) * 2 + 1;
	}

	cpumask_clear(mask);
	cpumask_set_cpu(cpu, mask);

	return cpu;
}

static int hns_nic_init_irq(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	struct net_device *netdev = priv->netdev;
	struct hns_nic_ring_data *rd;
	int i;
	int ret;
	int cpu;

	for (i = 0; i < h->q_num * 2; i++) {
		rd = &priv->ring_data[i];

		if (rd->ring->irq_init_flag == RCB_IRQ_INITED)
			break;

		snprintf(rd->ring->ring_name, RCB_RING_NAME_LEN,
			 "%s-%s%d", priv->netdev->name,
			 (is_tx_ring(rd->ring) ? "tx" : "rx"), rd->queue_index);

		rd->ring->ring_name[RCB_RING_NAME_LEN - 1] = '\0';

		ret = request_irq(rd->ring->irq,
				  hns_irq_handle, 0, rd->ring->ring_name, rd);
		if (ret) {
			netdev_err(priv->netdev, "request irq(%d) fail\n",
				   rd->ring->irq);
			return ret;
		}

		disable_irq(rd->ring->irq);

		cpu = hns_nic_init_affinity_mask(h->q_num, i,
						 rd->ring, &rd->mask);

		if (cpu_online(cpu)) {
			irq_set_affinity_hint(rd->ring->irq,
					      &rd->mask);

			/* if it is tx ring, init xps config */
			if (is_tx_ring(rd->ring)) {
				ret = netif_set_xps_queue(netdev, &rd->mask,
							  rd->queue_index);
				if (ret)
					netdev_err(priv->netdev,
						   "netif_set_xps_queue fail\n");
			}
		}

		rd->ring->irq_init_flag = RCB_IRQ_INITED;
	}

	ret = h->dev->ops->port_irq_init(h);
	if (ret)
		return ret;

	return 0;
}

static int hns_nic_net_up(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	int i, j, k;
	int ret;

	if (!test_bit(NIC_STATE_DOWN, &priv->state))
		return 0;

	ret = hns_nic_init_irq(priv);
	if (ret != 0) {
		netdev_err(ndev, "hns init irq failed! ret=%d\n", ret);
		return ret;
	}

	for (i = 0; i < h->q_num * 2; i++) {
		ret = hns_nic_ring_open(ndev, i);
		if (ret)
			goto out_has_some_queues;
	}

	for (k = 0; k < h->q_num; k++)
		h->dev->ops->toggle_queue_status(h->qs[k], 1);

	ret = h->dev->ops->set_mac_addr(h, ndev->dev_addr);
	if (ret)
		goto out_set_mac_addr_err;

	ret = h->dev->ops->start ? h->dev->ops->start(h) : 0;
	if (ret)
		goto out_start_err;

	if (priv->phy)
		phy_start(priv->phy);

	if (h->phy_if != PHY_INTERFACE_MODE_XGMII) {
		ret = ndev->ethtool_ops->set_pauseparam(ndev, &priv->epause);
		if (ret)
			netdev_err(ndev, "set ge pause failed,ret %d.\n", ret);
	}
	(void)mod_timer(&priv->service_timer, jiffies + SERVICE_TIMER_HZ);
	clear_bit(NIC_STATE_DOWN, &priv->state);

	return 0;

out_start_err:
	netif_stop_queue(ndev);
out_set_mac_addr_err:
	for (k = 0; k < h->q_num; k++)
		h->dev->ops->toggle_queue_status(h->qs[k], 0);
out_has_some_queues:
	for (j = i - 1; j >= 0; j--)
		hns_nic_ring_close(ndev, j);

	set_bit(NIC_STATE_DOWN, &priv->state);

	return ret;
}

static void hns_nic_net_down(struct net_device *ndev)
{
	int i;
	struct hnae_ae_ops *ops;
	struct hns_nic_priv *priv = netdev_priv(ndev);

	if (test_and_set_bit(NIC_STATE_DOWN, &priv->state))
		return;

	del_timer_sync(&priv->service_timer);
	cancel_work_sync(&priv->service_task);
	netif_tx_stop_all_queues(ndev);
	netif_carrier_off(ndev);
	netif_tx_disable(ndev);
	priv->link = 0;

	if (priv->phy)
		phy_stop(priv->phy);

	ops = priv->ae_handle->dev->ops;

	if (ops->stop)
		ops->stop(priv->ae_handle);

	netif_tx_stop_all_queues(ndev);

	for (i = priv->ae_handle->q_num - 1; i >= 0; i--) {
		hns_nic_ring_close(ndev, i);
		hns_nic_ring_close(ndev, i + priv->ae_handle->q_num);

		/* clean tx buffers*/
		hns_nic_tx_clr_all_bufs(priv->ring_data + i);
	}

	ops->port_irq_free(priv->ae_handle);
}

void hns_nic_net_reset(struct net_device *ndev, int ring_reset_en)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *handle = priv->ae_handle;
	int ret;

	while (test_and_set_bit(NIC_STATE_RESETTING, &priv->state))
		mdelay(1);

	if (priv->enet_ver == AE_VERSION_1)
		(void)hnae_reinit_handle(handle);
	else {
		if (ring_reset_en)
			ret = hnae_reinit_handle(handle);
		else
			ret = handle->dev->ops->reset(handle);
		if (ret) {
			/* don't set port unavailable when resetting fail
			 * for clearing FIFO failed
			 */
			if (ret != -EBUSY)
				set_bit(HNS_RING_RESET_ERR,
					&handle->port_unavailable);
			else
				clear_bit(HNS_RING_RESET_ERR,
					  &handle->port_unavailable);
			goto out;
		}

		clear_bit(HNS_RING_RESET_ERR, &handle->port_unavailable);
	}

	if (priv->phy) {
		(void)phy_init_hw(priv->phy);
		/* Fix phy supported */
		priv->phy->supported &= handle->if_support;
		priv->phy->supported |= SUPPORTED_Pause | SUPPORTED_Asym_Pause;
	}

	priv->link_config.active_flowctrl |= FLOW_CTRL_RX | FLOW_CTRL_TX;

out:
	clear_bit(NIC_STATE_RESETTING, &priv->state);
}

void hns_get_nic_lock(struct hns_nic_priv *priv)
{
	mutex_lock(&priv->nic_lock);
}

void hns_release_nic_lock(struct hns_nic_priv *priv)
{
	mutex_unlock(&priv->nic_lock);
}

void hns_nic_net_reinit(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	priv->netdev->trans_start = jiffies;
	while (test_and_set_bit(NIC_STATE_REINITING, &priv->state))
		mdelay(1);

	hns_get_nic_lock(priv);
	hns_nic_net_down(netdev);
	hns_nic_net_reset(netdev, 0);

	clear_bit(HNS_PORT_RSTING_STATE, &priv->recover_state);
	if (netif_running(netdev))
		hns_nic_net_up(netdev);

	hns_release_nic_lock(priv);
	clear_bit(NIC_STATE_REINITING, &priv->state);
}

static int hns_nic_net_open(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	int ret;

	if (test_bit(NIC_STATE_TESTING, &priv->state))
		return -EBUSY;

	hns_get_nic_lock(priv);

	priv->link = 0;
	netif_carrier_off(ndev);

	ret = netif_set_real_num_tx_queues(ndev, h->q_num);
	if (ret < 0) {
		netdev_err(ndev, "netif_set_real_num_tx_queues fail, ret=%d!\n",
			   ret);
		goto open_failed;
	}

	ret = netif_set_real_num_rx_queues(ndev, h->q_num);
	if (ret < 0) {
		netdev_err(ndev,
			   "netif_set_real_num_rx_queues fail, ret=%d!\n", ret);
		goto open_failed;
	}

	/**
	 * The MAC is not XGE, we select fixed xmit queue. Mac0 select tx1,
	 * mac1 select tx2, and so on.
	 */
	if (!(h->if_support & SUPPORTED_10000baseKR_Full))
		priv->tx_queue = h->eport_id + 1;

	ret = hns_nic_net_up(ndev);
	if (ret) {
		netdev_err(ndev,
			   "hns net up fail, ret=%d!\n", ret);
		goto open_failed;
	}

	hns_release_nic_lock(priv);
	return 0;

open_failed:
	hns_release_nic_lock(priv);
	return ret;
}

static int hns_nic_net_stop(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);

	hns_get_nic_lock(priv);
	hns_nic_net_down(ndev);
	hns_release_nic_lock(priv);
	return 0;
}

int hns_nic_get_mdio_reg(struct net_device *ndev, unsigned int page,
			 unsigned int reg, unsigned int *regvalue)

{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;

	return h->dev->ops->get_mdio_reg(h, page, reg, regvalue);
}

int hns_nic_set_mdio_reg(struct net_device *ndev, unsigned int page,
			 unsigned int reg, unsigned int regvalue)

{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;

	return h->dev->ops->set_mdio_reg(h, page, reg, regvalue);
}
static void hns_tx_timeout_reset(struct hns_nic_priv *priv);
static void hns_nic_net_timeout(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);

	hns_tx_timeout_reset(priv);
}

static int hns_nic_do_ioctl(struct net_device *netdev, struct ifreq *ifr,
			    int cmd)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct phy_device *phy_dev = priv->phy;

	if (!netif_running(netdev))
		return -EINVAL;

	if (!phy_dev)
		return -ENOTSUPP;

	return phy_mii_ioctl(phy_dev, ifr, cmd);
}

/* use only for netconsole to poll with the device without interrupt */
#ifdef CONFIG_NET_POLL_CONTROLLER
void hns_nic_poll_controller(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	unsigned long flags;
	int i;

	local_irq_save(flags);
	for (i = 0; i < priv->ae_handle->q_num * 2; i++)
		napi_schedule(&priv->ring_data[i].napi);
	local_irq_restore(flags);
}
#endif

static netdev_tx_t hns_nic_net_xmit(struct sk_buff *skb,
				    struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);

	assert(skb->queue_mapping < priv->ae_handle->q_num);

	return hns_nic_net_xmit_hw(ndev, skb,
				   &tx_ring_data(priv, skb->queue_mapping));
}

static void hns_nic_drop_rx_fetch(struct hns_nic_ring_data *ring_data,
				  struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
}

#define HNS_LB_TX_RING	0
static struct sk_buff *hns_assemble_skb(struct net_device *ndev)
{
	struct sk_buff *skb;
	struct ethhdr *ethhdr;
	int frame_len;

	/* allocate test skb */
	skb = alloc_skb(64, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_put(skb, 64);
	skb->dev = ndev;
	memset(skb->data, 0xFF, skb->len);

	/* must be tcp/ip package */
	ethhdr = (struct ethhdr *)skb->data;
	ethhdr->h_proto = htons(ETH_P_IP);

	frame_len = skb->len & (~1ul);
	memset(&skb->data[frame_len / 2], 0xAA,
	       frame_len / 2 - 1);

	skb->queue_mapping = HNS_LB_TX_RING;

	return skb;
}

static bool hns_enable_serdes_lb(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	struct hnae_ae_ops *ops = h->dev->ops;
	int speed, duplex;
	int ret;

	ret = ops->set_loopback(h, MAC_INTERNALLOOP_SERDES, 1);
	if (ret)
		return ret;

	ret = ops->start ? ops->start(h) : 0;
	if (ret)
		return ret;

	/* link adjust duplex*/
	if (h->phy_if != PHY_INTERFACE_MODE_XGMII)
		speed = 1000;
	else
		speed = 10000;
	duplex = 1;

	ops->adjust_link(h, speed, duplex);

	/* wait h/w ready */
	mdelay(300);

	return 0;
}

static void hns_disable_serdes_lb(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	struct hnae_ae_ops *ops = h->dev->ops;

	ops->stop(h);
	ops->set_loopback(h, MAC_INTERNALLOOP_SERDES, 0);
}

/**
 *hns_nic_clear_all_rx_fetch - clear the chip fetched descriptions. The
 *function as follows:
 *    1. if one rx ring has found the page_offset is not equal 0 between head
 *       and tail, it means that the chip fetched the wrong descs for the ring
 *       which buffer size is 4096.
 *    2. we set the chip serdes loopback and set rss indirection to the ring.
 *    3. construct 64-bytes ip broadcast packages, wait the associated rx ring
 *       recieving all packages and it will fetch new descriptions.
 *    4. recover to the original state.
 *
 *@ndev: net device
 */
static int hns_nic_clear_all_rx_fetch(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	struct hnae_ae_ops *ops = h->dev->ops;
	struct hns_nic_ring_data *rd;
	struct hnae_ring *ring;
	struct sk_buff *skb;
	u32 *org_indir;
	u32 *cur_indir;
	int indir_size;
	int head, tail;
	int fetch_num;
	int i, j;
	bool found;
	int retry_times;
	int ret = 0;

	/* alloc indir memory */
	indir_size = ops->get_rss_indir_size(h) * sizeof(*org_indir);
	org_indir = kzalloc(indir_size, GFP_KERNEL);
	if (!org_indir)
		return -ENOMEM;

	/* store the orginal indirection */
	ops->get_rss(h, org_indir, NULL, NULL);

	cur_indir = kzalloc(indir_size, GFP_KERNEL);
	if (!cur_indir) {
		ret = -ENOMEM;
		goto cur_indir_alloc_err;
	}

	/* set loopback */
	if (hns_enable_serdes_lb(ndev)) {
		ret = -EINVAL;
		goto enable_serdes_lb_err;
	}

	/* foreach every rx ring to clear fetch desc */
	for (i = 0; i < h->q_num; i++) {
		ring = &h->qs[i]->rx_ring;
		head = readl_relaxed(ring->io_base + RCB_REG_HEAD);
		tail = readl_relaxed(ring->io_base + RCB_REG_TAIL);
		found = false;
		fetch_num = ring_dist(ring, head, tail);

		while (head != tail) {
			if (ring->desc_cb[head].page_offset != 0) {
				found = true;
				break;
			}

			head++;
			if (head == ring->desc_num)
				head = 0;
		}

		if (found) {
			for (j = 0; j < indir_size / sizeof(*org_indir); j++)
				cur_indir[j] = i;
			ops->set_rss(h, cur_indir, NULL, 0);

			for (j = 0; j < fetch_num; j++) {
				/* alloc one skb and init */
				skb = hns_assemble_skb(ndev);
				if (!skb)
					goto out;
				rd = &tx_ring_data(priv, skb->queue_mapping);
				hns_nic_net_xmit_hw(ndev, skb, rd);

				retry_times = 0;
				while (retry_times++ < 10) {
					mdelay(10);
					/* clean rx */
					rd = &rx_ring_data(priv, i);
					if (rd->poll_one(rd, fetch_num,
							 hns_nic_drop_rx_fetch))
						break;
				}

				retry_times = 0;
				while (retry_times++ < 10) {
					mdelay(10);
					/* clean tx ring 0 send package */
					rd = &tx_ring_data(priv,
							   HNS_LB_TX_RING);
					if (rd->poll_one(rd, fetch_num, NULL))
						break;
				}
			}
		}
	}

out:
	/* restore everything */
	ops->set_rss(h, org_indir, NULL, 0);
	hns_disable_serdes_lb(ndev);
enable_serdes_lb_err:
	kfree(cur_indir);
cur_indir_alloc_err:
	kfree(org_indir);

	return ret;
}

static int hns_nic_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	bool if_running = netif_running(ndev);
	int ret;

	/* MTU < 68 is an error and causes problems on some kernels */
	if (new_mtu < 68)
		return -EINVAL;

	/* MTU no change */
	if (new_mtu == ndev->mtu)
		return 0;

	if (!h->dev->ops->set_mtu)
		return -ENOTSUPP;

	if (if_running) {
		(void)hns_nic_net_stop(ndev);
		msleep(100);
	}

	if (priv->enet_ver != AE_VERSION_1 &&
	    ndev->mtu <= BD_SIZE_2048_MAX_MTU &&
	    new_mtu > BD_SIZE_2048_MAX_MTU) {
		/* update desc */
		hnae_reinit_all_ring_desc(h);

		/* clear the package which the chip has fetched */
		ret = hns_nic_clear_all_rx_fetch(ndev);

		/* the page offset must be consist with desc */
		hnae_reinit_all_ring_page_off(h);

		if (ret) {
			netdev_err(ndev, "clear the fetched desc fail\n");
			goto out;
		}
	}

	ret = h->dev->ops->set_mtu(h, new_mtu);
	if (ret) {
		netdev_err(ndev, "set mtu fail, return value %d\n",
			   ret);
		goto out;
	}

	/* finally, set new mtu to netdevice */
	ndev->mtu = new_mtu;

out:
	if (if_running) {
		if (hns_nic_net_open(ndev)) {
			netdev_err(ndev, "hns net open fail\n");
			ret = -EINVAL;
		}
	}

	return ret;
}

static int hns_nic_set_features(
	struct net_device *netdev, netdev_features_t features)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	switch (priv->enet_ver) {
	case AE_VERSION_1:
		if ((features & NETIF_F_TSO) || (features & NETIF_F_TSO6))
			netdev_info(netdev, "enet v1 do not support tso!\n");
		break;
	default:
		break;
	}
	netdev->features = features;
	return 0;
}

static netdev_features_t hns_nic_fix_features(
	struct net_device *netdev, netdev_features_t features)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	switch (priv->enet_ver) {
	case AE_VERSION_1:
		features &= ~(NETIF_F_TSO | NETIF_F_TSO6 |
			NETIF_F_HW_VLAN_CTAG_FILTER);
		break;
	default:
		break;
	}
	return features;
}

/**
 * nic_set_multicast_list - set mutl mac address
 * @netdev: net device
 * @p: mac address
 *
 * return void
 */
void hns_set_multicast_list(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	struct netdev_hw_addr *ha = NULL;

	if (!h)	{
		netdev_err(ndev, "hnae handle is null\n");
		return;
	}

	if (h->dev->ops->clr_mc_addr)
		if (h->dev->ops->clr_mc_addr(h))
			netdev_err(ndev, "clear multicast address fail\n");

	if (h->dev->ops->set_mc_addr) {
		netdev_for_each_mc_addr(ha, ndev)
			if (h->dev->ops->set_mc_addr(h, ha->addr))
				netdev_err(ndev, "set multicast fail\n");
	}
}

/**
 * hns_set_unicast_list - set unicast mac address to tcam table
 * @ndev: net device
 *
 * return void
 */
void hns_set_unicast_list(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;
	int cnt, ret = 0, rc;
	struct netdev_hw_addr *ha;

	if (!h)	{
		netdev_err(ndev, "hnae handle is null\n");
		return;
	}

	if (!(h->dev->ops->clr_uc_addr && h->dev->ops->set_uc_addr)) {
		netdev_err(ndev, "vlan dev mac addr setting not supported\n");
		return;
	}

	/* del all unicast tcam entry */
	if (h->dev->ops->clr_uc_addr(h))
		netdev_err(ndev, "error occur when deleting unicast entries\n");

	/* add real device's mac addr tcam entry */
	rc = h->dev->ops->set_mac_addr(h, ndev->dev_addr);
	if (rc)
		ret = -1;

	/* add all unicast mac addr tcam entry */
	cnt = netdev_uc_count(ndev);
	if (cnt > 0) {
		netdev_for_each_uc_addr(ha, ndev) {
			rc = h->dev->ops->set_uc_addr(h, ha->addr);
			if (rc)
				ret = -1;
		}
	}

	if (ret)
		netdev_err(ndev, "set mac addr fail\n");
}

void hns_nic_set_rx_mode(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;

	if (h->dev->ops->set_promisc_mode) {
		if (ndev->flags & IFF_PROMISC)
			h->dev->ops->set_promisc_mode(h, 1);
		else
			h->dev->ops->set_promisc_mode(h, 0);
	}

	hns_set_multicast_list(ndev);
	hns_set_unicast_list(ndev);
}

struct rtnl_link_stats64 *hns_nic_get_stats64(struct net_device *ndev,
					      struct rtnl_link_stats64 *stats)
{
	int idx = 0;
	u64 tx_bytes = 0;
	u64 rx_bytes = 0;
	u64 tx_pkts = 0;
	u64 rx_pkts = 0;
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h = priv->ae_handle;

	for (idx = 0; idx < h->q_num; idx++) {
		tx_bytes += h->qs[idx]->tx_ring.stats.tx_bytes;
		tx_pkts += h->qs[idx]->tx_ring.stats.tx_pkts;
		rx_bytes += h->qs[idx]->rx_ring.stats.rx_bytes;
		rx_pkts += h->qs[idx]->rx_ring.stats.rx_pkts;
	}

	stats->tx_bytes = tx_bytes;
	stats->tx_packets = tx_pkts;
	stats->rx_bytes = rx_bytes;
	stats->rx_packets = rx_pkts;

	stats->rx_errors = ndev->stats.rx_errors;
	stats->multicast = ndev->stats.multicast;
	stats->rx_length_errors = ndev->stats.rx_length_errors;
	stats->rx_crc_errors = ndev->stats.rx_crc_errors;
	stats->rx_missed_errors = ndev->stats.rx_missed_errors;

	stats->tx_errors = ndev->stats.tx_errors;
	stats->rx_dropped = ndev->stats.rx_dropped;
	stats->tx_dropped = ndev->stats.tx_dropped;
	stats->collisions = ndev->stats.collisions;
	stats->rx_over_errors = ndev->stats.rx_over_errors;
	stats->rx_frame_errors = ndev->stats.rx_frame_errors;
	stats->rx_fifo_errors = ndev->stats.rx_fifo_errors;
	stats->tx_aborted_errors = ndev->stats.tx_aborted_errors;
	stats->tx_carrier_errors = ndev->stats.tx_carrier_errors;
	stats->tx_fifo_errors = ndev->stats.tx_fifo_errors;
	stats->tx_heartbeat_errors = ndev->stats.tx_heartbeat_errors;
	stats->tx_window_errors = ndev->stats.tx_window_errors;
	stats->rx_compressed = ndev->stats.rx_compressed;
	stats->tx_compressed = ndev->stats.tx_compressed;

	return stats;
}

static u32 hns_calc_tx_rss(u32 sip, u32 dip, u32 sport, u32 dport, u32 *rss_key)
{
	u32 rss = 0;
	int i;
	u32 port;

	port = (sport << 16) | dport;

	for (i = 0; i < 32; i++)
		if (sip & (1 << (31 - i)))
			rss ^= (rss_key[9] << i) |
				(u32)((u64)rss_key[8] >> (32 - i));

	for (i = 0; i < 32; i++)
		if (dip & (1 << (31 - i)))
			rss ^= (rss_key[8] << i) |
				(u32)((u64)rss_key[7] >> (32 - i));

	for (i = 0; i < 32; i++)
		if (port & (1 << (31 - i)))
			rss ^= (rss_key[7] << i) |
				(u32)((u64)rss_key[6] >> (32 - i));

	return rss;
}

/* if tcp or udp, then calc tx ring index */
static u16 hns_calc_tx_ring_idx(struct hns_nic_priv *priv,
				struct sk_buff *skb)
{
	struct hnae_handle *handle;
	struct iphdr *iphdr;
	struct tcphdr *tcphdr;
	u32 rss;
	int protocol;
	u16 ring = INVALID_TX_RING;

	if (skb->protocol == htons(ETH_P_IP)) {
		iphdr = ip_hdr(skb);
		protocol = iphdr->protocol;
		if (protocol == IPPROTO_TCP) {
			/* because tcp and udp dest and src port is same */
			tcphdr = tcp_hdr(skb);
			handle = priv->ae_handle;
			rss = hns_calc_tx_rss(ntohl(iphdr->daddr),
					      ntohl(iphdr->saddr),
					      ntohs(tcphdr->dest),
					      ntohs(tcphdr->source),
					      handle->rss_key);
			ring = handle->rss_indir_table[rss & 0xff] & 0xf;
		}
	}

	return ring;
}

static u16
hns_nic_select_queue(struct net_device *ndev, struct sk_buff *skb,
		     void *accel_priv, select_queue_fallback_t fallback)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct ethhdr *eth_hdr = (struct ethhdr *)skb->data;
	u16 ring;

	/* fix hardware broadcast/multicast packets loopback */
	if (unlikely(is_multicast_ether_addr(eth_hdr->h_dest)))
		return 0;

	/* if netdev init select queue, apply it. */
	if (priv->tx_queue)
		return priv->tx_queue;

	ring = hns_calc_tx_ring_idx(priv, skb);
	if (ring != INVALID_TX_RING)
		return ring;
	else
		return fallback(ndev, skb);
}

static struct net_device_ops hns_nic_netdev_ops = {
	.ndo_open = hns_nic_net_open,
	.ndo_stop = hns_nic_net_stop,
	.ndo_start_xmit = hns_nic_net_xmit,
	.ndo_tx_timeout = hns_nic_net_timeout,
	.ndo_set_mac_address = hns_nic_net_set_mac_address,
	.ndo_vlan_rx_add_vid = hns_nic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = hns_nic_vlan_rx_kill_vid,
	.ndo_change_mtu = hns_nic_change_mtu,
	.ndo_do_ioctl = hns_nic_do_ioctl,
	.ndo_set_features = hns_nic_set_features,
	.ndo_fix_features = hns_nic_fix_features,
	.ndo_get_stats64 = hns_nic_get_stats64,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = hns_nic_poll_controller,
#endif
	.ndo_set_rx_mode = hns_nic_set_rx_mode,
};

/**
 *hns_nic_get_status - get net status
 *@net_dev: net_device
 *@old_link: net old link state
 *retuen 0 - status down , nonzero --status up
 */
u32 hns_nic_get_status(struct net_device *net_dev, u32 old_link)
{
	struct hns_nic_priv *priv = netdev_priv(net_dev);
	u32 link_stat = old_link;
	struct hnae_handle *h = priv->ae_handle;

	if (priv->phy) {
		if (!genphy_read_status(priv->phy))
			link_stat = priv->phy->link;
		else
			link_stat = 0;
	}

	if (h->dev && h->dev->ops && h->dev->ops->get_status)
		link_stat = link_stat && h->dev->ops->get_status(h);
	else
		link_stat = 0;

	return link_stat;
}

static void hns_nic_update_link_status(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	u32 state = hns_nic_get_status(netdev, 1);
	struct hnae_handle *h = priv->ae_handle;

	if (state != priv->link) {
		if (state) {
			/* if the port has phy, netif_carrier_on will be called
			 * in phy_state_machine.
			 */
			if (h->phy_if == PHY_INTERFACE_MODE_XGMII ||
			    (!h->phy_node))
				netif_carrier_on(netdev);
			netif_tx_wake_all_queues(netdev);
			netdev_info(netdev, "link up\n");
		} else {
			if (h->phy_if == PHY_INTERFACE_MODE_XGMII ||
			    (!h->phy_node))
				netif_carrier_off(netdev);
			netdev_info(netdev, "link down\n");
		}
		priv->link = state;
	}
}

/* for dumping key regs*/
static void hns_nic_dump(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	struct hnae_ae_ops *ops = h->dev->ops;
	u32 *data, reg_num, i;

	if (ops->get_regs_len && ops->get_regs) {
		reg_num = ops->get_regs_len(priv->ae_handle);
		reg_num = (reg_num + 3ul) & ~3ul;
		data = kcalloc(reg_num, sizeof(u32), GFP_KERNEL);
		if (data) {
			ops->get_regs(priv->ae_handle, data);
			for (i = 0; i < reg_num; i += 4)
				pr_info("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
					i, data[i], data[i + 1],
					data[i + 2], data[i + 3]);
			kfree(data);
		}
	}

	for (i = 0; i < (u32)h->q_num; i++) {
		pr_info("tx_queue%d_next_to_clean:%d\n",
			i, h->qs[i]->tx_ring.next_to_clean);
		pr_info("tx_queue%d_next_to_use:%d\n",
			i, h->qs[i]->tx_ring.next_to_use);
		pr_info("rx_queue%d_next_to_clean:%d\n",
			i, h->qs[i]->rx_ring.next_to_clean);
		pr_info("rx_queue%d_next_to_use:%d\n",
			i, h->qs[i]->rx_ring.next_to_use);
	}
}

/* for resetting suntask*/
static void hns_nic_reset_subtask(struct hns_nic_priv *priv)
{
	enum hnae_port_type type = priv->ae_handle->port_type;

	if (!test_bit(NIC_STATE2_RESET_REQUESTED, &priv->state))
		return;
	clear_bit(NIC_STATE2_RESET_REQUESTED, &priv->state);

	/* If we're already down, removing or resetting, just bail */
	if (test_bit(NIC_STATE_DOWN, &priv->state) ||
	    test_bit(NIC_STATE_REMOVING, &priv->state) ||
	    test_bit(NIC_STATE_RESETTING, &priv->state))
		return;

	hns_nic_dump(priv);
	netdev_info(priv->netdev, "try to reset %s port!\n",
		    (type == HNAE_PORT_DEBUG ? "debug" : "service"));

	/* put off any impending NetWatchDogTimeout */
	priv->netdev->trans_start = jiffies;

	hns_get_nic_lock(priv);
	hns_nic_net_down(priv->netdev);
	hns_nic_net_up(priv->netdev);
	hns_release_nic_lock(priv);

}

/* for doing service complete*/
static void hns_nic_service_event_complete(struct hns_nic_priv *priv)
{
	WARN_ON(!test_bit(NIC_STATE_SERVICE_SCHED, &priv->state));

	smp_mb__before_atomic();
	clear_bit(NIC_STATE_SERVICE_SCHED, &priv->state);
}

static void hns_nic_service_task(struct work_struct *work)
{
	struct hns_nic_priv *priv
		= container_of(work, struct hns_nic_priv, service_task);
	struct hnae_handle *h = priv->ae_handle;

	hns_nic_update_link_status(priv->netdev);
	h->dev->ops->update_led_status(h);
	hns_nic_update_stats(priv->netdev);

	hns_nic_reset_subtask(priv);
	hns_nic_service_event_complete(priv);
}

static void hns_nic_task_schedule(struct hns_nic_priv *priv)
{
	if (!test_bit(NIC_STATE_DOWN, &priv->state) &&
	    !test_bit(NIC_STATE_REMOVING, &priv->state) &&
	    !test_and_set_bit(NIC_STATE_SERVICE_SCHED, &priv->state))
		(void)schedule_work(&priv->service_task);
}

static void hns_nic_service_timer(unsigned long data)
{
	struct hns_nic_priv *priv = (struct hns_nic_priv *)data;

	(void)mod_timer(&priv->service_timer, jiffies + SERVICE_TIMER_HZ);

	hns_nic_task_schedule(priv);
}

/**
 * hns_tx_timeout_reset - initiate reset due to Tx timeout
 * @priv: driver private struct
 **/
static void hns_tx_timeout_reset(struct hns_nic_priv *priv)
{
	/* Do the reset outside of interrupt context */
	if (!test_bit(NIC_STATE_DOWN, &priv->state)) {
		set_bit(NIC_STATE2_RESET_REQUESTED, &priv->state);
		netdev_warn(priv->netdev,
			    "initiating reset due to tx timeout(%llu,0x%lx)\n",
			    priv->tx_timeout_count, priv->state);
		priv->tx_timeout_count++;
		hns_nic_task_schedule(priv);
	}
}

static int hns_nic_init_ring_data(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	struct hns_nic_ring_data *rd;
	bool is_ver1 = AE_IS_VER1(priv->enet_ver);
	int i;

	if (h->q_num > NIC_MAX_Q_PER_VF) {
		netdev_err(priv->netdev, "too much queue (%d)\n", h->q_num);
		return -EINVAL;
	}

	priv->ring_data = kzalloc(h->q_num * sizeof(*priv->ring_data) * 2,
				  GFP_KERNEL);
	if (!priv->ring_data)
		return -ENOMEM;

	for (i = 0; i < h->q_num; i++) {
		rd = &priv->ring_data[i];
		rd->queue_index = i;
		rd->ring = &h->qs[i]->tx_ring;
		rd->poll_one = hns_nic_tx_poll_one;
		rd->fini_process = is_ver1 ? hns_nic_tx_fini_pro :
				hns_nic_tx_fini_pro_v2;

		netif_napi_add(priv->netdev, &rd->napi,
			       hns_nic_common_poll, NIC_TX_CLEAN_MAX_NUM);
		rd->ring->irq_init_flag = RCB_IRQ_NOT_INITED;
	}
	for (i = h->q_num; i < h->q_num * 2; i++) {
		rd = &priv->ring_data[i];
		rd->queue_index = i - h->q_num;
		rd->ring = &h->qs[i - h->q_num]->rx_ring;
		rd->poll_one = hns_nic_rx_poll_one;
		rd->ex_process = hns_nic_rx_up_pro;
		rd->fini_process = is_ver1 ? hns_nic_rx_fini_pro :
				hns_nic_rx_fini_pro_v2;

		netif_napi_add(priv->netdev, &rd->napi,
			       hns_nic_common_poll, NIC_RX_CLEAN_MAX_NUM);
		rd->ring->irq_init_flag = RCB_IRQ_NOT_INITED;
	}

	return 0;
}

static void hns_nic_uninit_ring_data(struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;
	int i;

	for (i = 0; i < h->q_num * 2; i++) {
		netif_napi_del(&priv->ring_data[i].napi);
		if (priv->ring_data[i].ring->irq_init_flag == RCB_IRQ_INITED) {
			(void)irq_set_affinity_hint(
				priv->ring_data[i].ring->irq,
				NULL);
			free_irq(priv->ring_data[i].ring->irq,
				 &priv->ring_data[i]);
		}

		priv->ring_data[i].ring->irq_init_flag = RCB_IRQ_NOT_INITED;
	}
	kfree(priv->ring_data);
}

static void hns_nic_set_priv_ops(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	if (AE_IS_VER1(priv->enet_ver)) {
		priv->ops.fill_desc = fill_desc;
		priv->ops.get_rxd_bnum = get_rx_desc_bnum;
		priv->ops.get_vlan = get_vlan;
		priv->ops.maybe_stop_tx = hns_nic_maybe_stop_tx;
	} else {
		priv->ops.get_rxd_bnum = get_v2rx_desc_bnum;
		priv->ops.get_vlan = get_vlan_v2;
		if ((netdev->features & NETIF_F_TSO) ||
		    (netdev->features & NETIF_F_TSO6)) {
			priv->ops.fill_desc = fill_tso_desc;
			priv->ops.maybe_stop_tx = hns_nic_maybe_stop_tso;
			/* The chip only support 7*4096 */
			netif_set_gso_max_size(netdev, 7 * 4096);
		} else {
			priv->ops.fill_desc = fill_v2_desc;
			priv->ops.maybe_stop_tx = hns_nic_maybe_stop_tx;
		}
	}
}

static void hns_nic_pause_init(struct net_device *ndev,
			       struct hns_nic_priv *priv)
{
	struct hnae_handle *h = priv->ae_handle;

	if (!h->phy_node)
		return;

	if (h->phy_if == PHY_INTERFACE_MODE_XGMII)
		priv->link_config.pause_autoneg = AUTONEG_DISABLE;
	else
		priv->link_config.pause_autoneg = AUTONEG_ENABLE;

	priv->epause.autoneg = priv->link_config.pause_autoneg;
	priv->epause.rx_pause = priv->link_config.flowctrl & FLOW_CTRL_RX;
	priv->epause.tx_pause = priv->link_config.flowctrl & FLOW_CTRL_TX;
}

static int hns_nic_try_get_ae(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct hnae_handle *h;
	int ret;

	h = hnae_get_handle(&priv->netdev->dev,
			    priv->ae_node, priv->port_id, NULL);
	if (IS_ERR_OR_NULL(h)) {
		ret = -ENODEV;
		dev_dbg(priv->dev, "has not handle, register notifier!\n");
		goto out;
	}
	priv->ae_handle = h;

	ret = hns_nic_init_phy(ndev, h);
	if (ret) {
		dev_err(priv->dev, "probe phy device fail!\n");
		goto out_init_phy;
	}

	ret = hns_nic_init_ring_data(priv);
	if (ret) {
		ret = -ENOMEM;
		goto out_init_ring_data;
	}

	hns_nic_set_priv_ops(ndev);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(priv->dev, "probe register netdev fail!\n");
		goto out_reg_ndev_fail;
	}

	hns_nic_pause_init(ndev, priv);

	return 0;

out_reg_ndev_fail:
	hns_nic_uninit_ring_data(priv);
	priv->ring_data = NULL;
out_init_phy:
out_init_ring_data:
	hnae_put_handle(priv->ae_handle);
	priv->ae_handle = NULL;
out:
	return ret;
}

static int hns_nic_notifier_action(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	struct hns_nic_priv *priv =
		container_of(nb, struct hns_nic_priv, notifier_block);

	assert(action == HNAE_AE_REGISTER);

	if (!hns_nic_try_get_ae(priv->netdev)) {
		hnae_unregister_notifier(&priv->notifier_block);
		priv->notifier_block.notifier_call = NULL;
	}
	return 0;
}

static int hns_nic_dsaf_reinit(void)
{
	struct hnae_handle *handle = NULL;
	struct nic_dev_node *netdev_node;
	struct hns_nic_priv *priv;
	int ret = 0;

	list_for_each_entry_rcu(netdev_node, &netdev_list, node) {
		priv = netdev_priv(netdev_node->net_dev);
		priv->netdev->trans_start = jiffies;
		handle = priv->ae_handle;

		while (test_and_set_bit(NIC_STATE_REINITING, &priv->state))
			mdelay(1);

		hns_get_nic_lock(priv);
		hns_nic_net_down(netdev_node->net_dev);

		if (handle->dev->ops->get_rst_lock(handle)) {
			ret = -EBUSY;
			goto out;
		}
	}

	if (handle)
		ret = handle->dev->ops->dsaf_reset(handle);

out:
	list_for_each_entry_rcu(netdev_node, &netdev_list, node) {
		priv = netdev_priv(netdev_node->net_dev);
		handle = priv->ae_handle;

		if (!ret)
			hns_nic_net_reset(netdev_node->net_dev, 1);

		handle->dev->ops->release_rst_lock(handle);

		clear_bit(HNS_PORT_RSTING_STATE, &priv->recover_state);
		clear_bit(HNS_DSAF_RSTING_STATE, &priv->recover_state);
	}

	/* we must up port after all ring reset */
	list_for_each_entry_rcu(netdev_node, &netdev_list, node) {
		priv = netdev_priv(netdev_node->net_dev);
		if (netif_running(netdev_node->net_dev))
			hns_nic_net_up(netdev_node->net_dev);

		hns_release_nic_lock(priv);
		clear_bit(NIC_STATE_REINITING, &priv->state);
	}

	return ret;
}

static void hns_enet_event_report(int dsaf_id, int port_id,
				  enum nic_dev_event_type event_type,
				  enum nic_device_type dev_type,
				  enum netdev_node_state flag)
{
	struct net_device *netdev[MAX_REPORT_PORT_NUM];
	struct nic_dev_node *netdev_node;
	struct hns_nic_priv *priv;
	int report_cnt = 0;

	if (dsaf_id > 0)
		pr_err("only support dsaf 0, dsaf_id(%d) error !\n", dsaf_id);

	memset(netdev, 0, sizeof(netdev));
	list_for_each_entry_rcu(netdev_node, &netdev_list, node) {
		priv = netdev_priv(netdev_node->net_dev);
		if (flag == HNS_PORT_RSTING_STATE) {
			if (priv->ae_handle->dport_id != port_id)
				continue;
		}

		if (!test_and_set_bit(flag, &priv->recover_state)) {
			priv->last_event_type = event_type;
			priv->last_dev_type = dev_type;
			netdev[report_cnt] = netdev_node->net_dev;
			report_cnt++;
		}
	}

	if (report_cnt && nic_event_call)
		nic_event_call(report_cnt, netdev, event_type, dev_type);
}

static void nic_rst_task(struct work_struct *work)
{
	struct nic_work_entry *work_entry;
	struct hns_nic_priv *priv;

	work_entry = container_of(work, struct nic_work_entry, work);
	priv = container_of(work_entry, struct hns_nic_priv, nic_rst_task);

	if (test_bit(HNS_DSAF_RSTING_STATE, &priv->recover_state)) {
		if (hns_nic_dsaf_reinit())
			hns_enet_event_report(0, 0, NIC_DEVT_DSAF_CHIP_FAULT,
					      NIC_DEVICE_PORT,
					      HNS_DSAF_RSTING_STATE);

		/* in order to limit the recovering frequence. */
		msleep(HNS_RESET_LIMIT_PERIOD);

	} else if (test_bit(HNS_PORT_RSTING_STATE, &priv->recover_state)) {
		hns_nic_net_reinit(priv->netdev);
	}
}

int nic_register_event(nic_event_fn_t event_call)
{
	struct net_device *netdev[MAX_REPORT_PORT_NUM];
	struct nic_dev_node *netdev_node;
	struct hns_nic_priv *priv;

	if (!event_call) {
		pr_err("register event handle is null.\n");
		return -EINVAL;
	}

	nic_event_call = event_call;

	list_for_each_entry_rcu(netdev_node, &netdev_list, node) {
		priv = netdev_priv(netdev_node->net_dev);
		INIT_WORK(&priv->nic_rst_task.work, nic_rst_task);

		if (priv->recover_state) {
			netdev[0] = netdev_node->net_dev;
			nic_event_call(1, netdev,
				       priv->last_event_type,
				       priv->last_dev_type);
		}
	}

	return 0;
}
EXPORT_SYMBOL(nic_register_event);

void nic_unregister_event(void)
{
	struct nic_dev_node *netdev_node;
	struct hns_nic_priv *priv;

	/* clear all irq and recover handler */
	list_for_each_entry_rcu(netdev_node, &netdev_list, node) {
		priv = netdev_priv(netdev_node->net_dev);
		cancel_work_sync(&priv->nic_rst_task.work);
	}

	nic_event_call = NULL;
}
EXPORT_SYMBOL(nic_unregister_event);

void nic_chip_recover_handler(int port_cnt, struct net_device **netdev)
{
	struct hns_nic_priv *priv;

	if (!port_cnt || !netdev || !netdev[0]) {
		pr_err("invalid input netdev(null) in chip recover flow.");
		return;
	}

	priv = netdev_priv(netdev[0]);
	queue_work(nic_event_work_queue, &priv->nic_rst_task.work);

	if (test_bit(HNS_DSAF_RSTING_STATE, &priv->recover_state))
		netdev_info(netdev[0], "start to dsaf recovery.\n");
	else if (test_bit(HNS_PORT_RSTING_STATE, &priv->recover_state))
		netdev_info(netdev[0], "start to port recovery.\n");
}
EXPORT_SYMBOL(nic_chip_recover_handler);

static struct hnae_callback_ops hns_enet_callback_ops = {
	.event_report = hns_enet_event_report,
};

static int hns_nic_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *ndev;
	struct hns_nic_priv *priv;
	struct device_node *node = dev->of_node;
	struct nic_dev_node *netdev_node;
	int ret;

	ndev = alloc_etherdev_mq(sizeof(struct hns_nic_priv), NIC_MAX_Q_PER_VF);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	priv = netdev_priv(ndev);
	priv->dev = dev;
	priv->netdev = ndev;
	priv->link = 0;

	/* default flow control settings. */
	priv->link_config.active_flowctrl = FLOW_CTRL_RX | FLOW_CTRL_TX;
	priv->link_config.flowctrl = FLOW_CTRL_RX | FLOW_CTRL_TX;

	spin_lock_init(&priv->lock);
	mutex_init(&priv->nic_lock);

	if (of_device_is_compatible(node, "hisilicon,hns-nic-v1"))
		priv->enet_ver = AE_VERSION_1;
	else
		priv->enet_ver = AE_VERSION_2;

	priv->ae_node = (void *)of_parse_phandle(node, "ae-handle", 0);
	if (IS_ERR_OR_NULL(priv->ae_node)) {
		ret = PTR_ERR(priv->ae_node);
		dev_err(dev, "not find ae-handle\n");
		goto out_read_handle_fai;
	}

	ret = of_property_read_u32(node, "port-id", &priv->port_id);
	if (ret)
		goto out_read_string_fail;

	hns_init_mac_addr(ndev);

	if (priv->enet_ver == AE_VERSION_2)
		hns_nic_netdev_ops.ndo_select_queue = hns_nic_select_queue;

	ndev->watchdog_timeo = HNS_NIC_TX_TIMEOUT;
	ndev->priv_flags |= IFF_UNICAST_FLT;
	ndev->netdev_ops = &hns_nic_netdev_ops;
	hns_ethtool_set_ops(ndev);

#ifdef CONFIG_DCB
	if (!AE_IS_VER1(priv->enet_ver))
		hns_dcbnl_set_ops(ndev);
#endif

	ndev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
		NETIF_F_GRO;
	ndev->vlan_features |=
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM;
	ndev->vlan_features |= NETIF_F_SG | NETIF_F_GSO | NETIF_F_GRO;

	switch (priv->enet_ver) {
	case AE_VERSION_2:
		ndev->features |= NETIF_F_TSO | NETIF_F_TSO6 |
			NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_CTAG_RX |
			NETIF_F_NTUPLE;
		ndev->hw_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GSO |
			NETIF_F_GRO | NETIF_F_TSO | NETIF_F_TSO6 |
			NETIF_F_HW_VLAN_CTAG_FILTER;
		ndev->vlan_features |= NETIF_F_TSO | NETIF_F_TSO6;
		break;
	default:
		break;
	}

	SET_NETDEV_DEV(ndev, dev);

	if (!dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64)))
		dev_dbg(dev, "set mask to 64bit\n");
	else
		dev_err(dev, "set mask to 64bit fail!\n");

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(ndev);

	setup_timer(&priv->service_timer, hns_nic_service_timer,
		    (unsigned long)priv);
	INIT_WORK(&priv->service_task, hns_nic_service_task);

	set_bit(NIC_STATE_SERVICE_INITED, &priv->state);
	clear_bit(NIC_STATE_SERVICE_SCHED, &priv->state);
	set_bit(NIC_STATE_DOWN, &priv->state);

	if (!netdev_list.prev && !netdev_list.next) {
		INIT_LIST_HEAD(&netdev_list);
		nic_event_work_queue =
			create_singlethread_workqueue("arm_nic_event");
	}

	netdev_node = kzalloc(sizeof(*netdev_node), GFP_KERNEL);
	if (!netdev_node) {
		ret = -ENOMEM;
		goto out_notify_fail;
	}
	netdev_node->net_dev = ndev;
	list_add_tail_rcu(&netdev_node->node, &netdev_list);

	if (hns_nic_try_get_ae(priv->netdev)) {
		priv->notifier_block.notifier_call = hns_nic_notifier_action;
		ret = hnae_register_notifier(&priv->notifier_block);
		if (ret) {
			dev_err(dev, "register notifier fail!\n");
			goto out_get_ae_failed;
		}
		dev_dbg(dev, "has not handle, register notifier!\n");
	}

	priv->ae_handle->dev->callback_ops = &hns_enet_callback_ops;
	return 0;

out_get_ae_failed:
	list_del_rcu(&netdev_node->node);
	kfree(netdev_node);
out_notify_fail:
	cancel_work_sync(&priv->service_task);
out_read_handle_fai:

out_read_string_fail:
	free_netdev(ndev);
	return ret;
}

static int hns_nic_dev_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct hns_nic_priv *priv = netdev_priv(ndev);
	struct nic_dev_node *netdev_node;

	list_for_each_entry_rcu(netdev_node, &netdev_list, node) {
		if (netdev_node->net_dev == ndev) {
			list_del_rcu(&netdev_node->node);
			kfree(netdev_node);
			priv->ae_handle->dev->callback_ops = NULL;
			break;
		}
	}

	if (list_empty(&netdev_list)) {
		destroy_workqueue(nic_event_work_queue);
		memset(&netdev_list, 0, sizeof(netdev_list));
	}

	if (ndev->reg_state != NETREG_UNINITIALIZED)
		unregister_netdev(ndev);

	if (priv->ring_data)
		hns_nic_uninit_ring_data(priv);
	priv->ring_data = NULL;

	if (priv->phy)
		phy_disconnect(priv->phy);
	priv->phy = NULL;

	if (!IS_ERR_OR_NULL(priv->ae_handle))
		hnae_put_handle(priv->ae_handle);
	priv->ae_handle = NULL;
	if (priv->notifier_block.notifier_call)
		hnae_unregister_notifier(&priv->notifier_block);
	priv->notifier_block.notifier_call = NULL;

	set_bit(NIC_STATE_REMOVING, &priv->state);
	cancel_work_sync(&priv->service_task);

	free_netdev(ndev);
	return 0;
}

static const struct of_device_id hns_enet_of_match[] = {
	{.compatible = "hisilicon,hns-nic-v1",},
	{.compatible = "hisilicon,hns-nic-v2",},
	{},
};

MODULE_DEVICE_TABLE(of, hns_enet_of_match);

static struct platform_driver hns_nic_dev_driver = {
	.driver = {
		.name = "hns-nic",
		.of_match_table = hns_enet_of_match,
	},
	.probe = hns_nic_dev_probe,
	.remove = hns_nic_dev_remove,
};

module_platform_driver(hns_nic_dev_driver);

MODULE_DESCRIPTION("HISILICON HNS Ethernet driver");
MODULE_AUTHOR("Hisilicon, Inc.");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hns-nic");
