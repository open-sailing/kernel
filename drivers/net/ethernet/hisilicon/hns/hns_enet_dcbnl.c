#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <net/dcbnl.h>
#include "hns_basic.h"
#include "hns_enet.h"

enum hns_dcb_pfc_type {
	HNS_PFC_DISABLED = 0,
	HNS_PFC_FULL,
	HNS_PFC_TX,
	HNS_PFC_RX
};

static int hns_dcbnl_ieee_getets(struct net_device *netdev,
				 struct ieee_ets *ets)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;
	struct ieee_ets *my_ets = priv->hns_ieee_ets;

	/* Set number of supported traffic classes */
	ets->ets_cap = h->tc_cnt;

	/* No IEEE PFC settings available */
	if (!my_ets)
		return 0;

	ets->cbs = my_ets->cbs;
	memcpy(ets->tc_tx_bw, my_ets->tc_tx_bw, sizeof(ets->tc_tx_bw));
	memcpy(ets->tc_rx_bw, my_ets->tc_rx_bw, sizeof(ets->tc_rx_bw));
	memcpy(ets->tc_tsa, my_ets->tc_tsa, sizeof(ets->tc_tsa));
	memcpy(ets->prio_tc, my_ets->prio_tc, sizeof(ets->prio_tc));

	return 0;
}

static int hns_dcbnl_ieee_setets(struct net_device *netdev,
				 struct ieee_ets *ets)
{
	return -EINVAL;
}

static int hns_dcbnl_ieee_getpfc(struct net_device *dev,
				 struct ieee_pfc *pfc)
{
	struct hns_nic_priv *priv = netdev_priv(dev);
	struct hnae_handle *h = priv->ae_handle;
	struct ieee_pfc *my_pfc = priv->hns_ieee_pfc;

	pfc->pfc_cap = h->tc_cnt;

	/* No IEEE PFC settings available */
	if (!my_pfc)
		return 0;

	pfc->pfc_en = my_pfc->pfc_en;
	pfc->mbc = my_pfc->mbc;
	pfc->delay = my_pfc->delay;

	return 0;
}

static int hns_dcbnl_ieee_setpfc(struct net_device *dev,
				 struct ieee_pfc *pfc)
{
	struct hns_nic_priv *priv = netdev_priv(dev);
	struct hnae_handle *h = priv->ae_handle;

	if (!priv->hns_ieee_pfc) {
		priv->hns_ieee_pfc = devm_kzalloc(priv->dev,
						  sizeof(struct ieee_pfc),
						  GFP_KERNEL);
		if (!priv->hns_ieee_pfc)
			return -ENOMEM;
	}

	memcpy(priv->hns_ieee_pfc, pfc, sizeof(struct ieee_pfc));

	if (h->dev->ops->config_dcb_pfc)
		h->dev->ops->config_dcb_pfc(h, priv->hns_ieee_pfc->pfc_en);

	return 0;
}

static int hns_dcbnl_ieee_delapp(struct net_device *dev,
				 struct dcb_app *app)
{
	return 0;
}

static int hns_dcbnl_ieee_setapp(struct net_device *dev,
				 struct dcb_app *app)
{
	return -EINVAL;
}

static u8 hns_dcbnl_get_state(struct net_device *netdev)
{
	/* For PV660 & Hi1610, DCB is always on. */
	return 1;
}

static u8 hns_dcbnl_set_state(struct net_device *netdev, u8 state)
{
	if (state)
		return 0;

	/* For PV660 & Hi1610, DCB can't be set off. */
	return 1;
}

static void hns_dcbnl_get_perm_hw_addr(struct net_device *netdev,
				       u8 *perm_addr)
{
	memcpy(perm_addr, netdev->perm_addr, netdev->addr_len);
}

static u8 hns_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;
	u8 state = 0;

	/* return Disable. */
	if (h->dev->ops->get_dcb_pfc_pause_state)
		h->dev->ops->get_dcb_pfc_pause_state(h, &state);

	return state;
}

static void hns_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	if (h->dev->ops->get_dcb_pfc_pause_state)
		h->dev->ops->set_dcb_pfc_pause_state(h, state);
}

static int hns_dcbnl_getapp(struct net_device *netdev, u8 idtype, u16 id)
{
	return -EINVAL;
}

static u8 hns_dcbnl_getdcbx(struct net_device *netdev)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	return priv->dcbx_cap;
}

static u8 hns_dcbnl_setdcbx(struct net_device *netdev, u8 mode)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);

	/* no support for LLD_MANAGED modes or CEE+IEEE */
	if ((mode & DCB_CAP_DCBX_LLD_MANAGED) ||
	    ((mode & DCB_CAP_DCBX_VER_IEEE) && (mode & DCB_CAP_DCBX_VER_CEE)) ||
	    !(mode & DCB_CAP_DCBX_HOST))
		return 1;

	if (mode == priv->dcbx_cap)
		return 0;

	priv->dcbx_cap = mode;

	return 0;
}

static int hns_dcbnl_setnumtcs(struct net_device *netdev, int tcid, u8 num)
{
	/* Can't set tc num. */
	return -EINVAL;
}

static int hns_dcbnl_getnumtcs(struct net_device *netdev, int tcid, u8 *num)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;

	*num = h->tc_cnt;

	return 0;
}

static u8 hns_dcbnl_getcap(struct net_device *netdev, int capid, u8 *cap)
{
	switch (capid) {
	case DCB_CAP_ATTR_PG:
		*cap = false;
		break;
	case DCB_CAP_ATTR_PFC:
		*cap = true;
		break;
	case DCB_CAP_ATTR_UP2TC:
		*cap = false;
		break;
	case DCB_CAP_ATTR_PG_TCS:
		*cap = 0x80;
		break;
	case DCB_CAP_ATTR_PFC_TCS:
		*cap = 0x80;
		break;
	case DCB_CAP_ATTR_GSP:
		*cap = true;
		break;
	case DCB_CAP_ATTR_BCN:
		*cap = false;
		break;
	case DCB_CAP_ATTR_DCBX:
		break;
	default:
		*cap = false;
		break;
	}

	return 0;
}

static u8 hns_dcbnl_set_all(struct net_device *netdev)
{
	return 0;
}

static void hns_dcbnl_get_pfc_cfg(struct net_device *netdev, int priority,
				  u8 *setting)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;
	u8 prio_tc[IEEE_8021QAZ_MAX_TCS];
	u32 tc;

	*setting = HNS_PFC_DISABLED;
	/* if possible update UP2TC mappings from HW */
	if (!h->dev->ops->get_dcb_up2tc || !h->dev->ops->get_dcb_tc_cfg)
		return;

	h->dev->ops->get_dcb_up2tc(h, prio_tc);
	tc = prio_tc[priority];
	if (tc >= IEEE_8021QAZ_MAX_TCS)
		return;

	h->dev->ops->get_dcb_tc_cfg(h, tc, setting);
}

static void hns_dcbnl_set_pfc_cfg(struct net_device *netdev, int priority,
				  u8 setting)
{
	struct hns_nic_priv *priv = netdev_priv(netdev);
	struct hnae_handle *h = priv->ae_handle;
	u8 prio_tc[IEEE_8021QAZ_MAX_TCS];
	u32 tc;
	u8 old_pfc;

	/* if possible update UP2TC mappings from HW */
	if (!h->dev->ops->get_dcb_up2tc || !h->dev->ops->get_dcb_tc_cfg ||
	    !h->dev->ops->get_dcb_tc_cfg)
		return;

	/* now only can open 2 pfcup because the chip onlu support 2tc */
	if (priority >= h->tc_cnt && setting)
		return;

	h->dev->ops->get_dcb_up2tc(h, prio_tc);
	tc = prio_tc[priority];

	if (tc >= IEEE_8021QAZ_MAX_TCS)
		return;

	old_pfc = h->pfc_en;
	h->pfc_en &= ~(1U << tc);
	h->pfc_en |= setting << tc;

	if (!old_pfc == !h->pfc_en)
		h->dev->ops->set_dcb_tc_cfg(h, tc, setting);
	else
		hns_nic_net_reinit(netdev);
}

static struct dcbnl_rtnl_ops hns_dcbnl_ops = {
	.ieee_getets	= hns_dcbnl_ieee_getets,
	.ieee_setets	= hns_dcbnl_ieee_setets,
	.ieee_getpfc	= hns_dcbnl_ieee_getpfc,
	.ieee_setpfc	= hns_dcbnl_ieee_setpfc,
	.ieee_setapp	= hns_dcbnl_ieee_setapp,
	.ieee_delapp	= hns_dcbnl_ieee_delapp,

	.getstate	= hns_dcbnl_get_state,
	.setstate	= hns_dcbnl_set_state,
	.getpermhwaddr	= hns_dcbnl_get_perm_hw_addr,
	.setpgtccfgtx	= NULL,
	.setpgbwgcfgtx	= NULL,
	.setpgtccfgrx	= NULL,
	.setpgbwgcfgrx	= NULL,
	.getpgtccfgtx	= NULL,
	.getpgbwgcfgtx	= NULL,
	.getpgtccfgrx	= NULL,
	.getpgbwgcfgrx	= NULL,
	.setpfccfg	= hns_dcbnl_set_pfc_cfg,
	.getpfccfg	= hns_dcbnl_get_pfc_cfg,
	.setall		= hns_dcbnl_set_all,
	.getcap		= hns_dcbnl_getcap,
	.getnumtcs	= hns_dcbnl_getnumtcs,
	.setnumtcs	= hns_dcbnl_setnumtcs,
	.getpfcstate	= hns_dcbnl_getpfcstate,
	.setpfcstate	= hns_dcbnl_setpfcstate,
	.getapp		= hns_dcbnl_getapp,
	/* DCBX configuration */
	.getdcbx	= hns_dcbnl_getdcbx,
	.setdcbx	= hns_dcbnl_setdcbx,
};

void hns_dcbnl_set_ops(struct net_device *ndev)
{
	struct hns_nic_priv *priv = netdev_priv(ndev);

	priv->dcbx_cap = DCB_CAP_DCBX_VER_CEE | DCB_CAP_DCBX_HOST;
	ndev->dcbnl_ops = &hns_dcbnl_ops;
}
