#ifndef _GMAC_NET_H
#define _GMAC_NET_H

/* EXPORT SYMBOL */
typedef
void (*gmac_flow_ctrl_handler)(void);
extern void gmac_send_txpause(bool value);
extern void gmac_enable_recv(bool value);
extern void gmac_enable_xmit(bool value);
extern int gmac_enable_inloop(bool value);
extern int gmac_enable_lineloop(bool value);
extern int gmac_register_flow_ctrl_handler(gmac_flow_ctrl_handler flow_ctrl_handler);
extern int gmac_unregister_flow_ctrl_handler(void);
extern int gmac_flow_ctrl_value_set(int value);
/* PHY operation */
extern int get_bcm_phy_id(void);
extern int get_phy_link_st(void);
extern int get_phy_ctrl_st(void);
extern int set_phy_loopback(bool enable);
extern int set_phy_power_switch(bool enable);
extern int phy_rdb_read(u32 reg);
extern int phy_rdb_write(u32 reg, u32 val);
#endif
