/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_XGMAC_H
#define _HNS_XGMAC_H

#define ETH_XGMAC_DUMP_NUM		(214)

#define XGE_TX_LF_RF_ENABLE		(0x2)
#define XGE_TX_LF_RF_DISABLE		(0xa)

#define XGE_INT_EN_MASK ((1 << XGE_MIB_ECCERR_MUL_INT) |\
			 (1 << XGE_FEC_ECCERR_MUL_INT))
#define XGE_INT_DISABLE (0U)
#endif
