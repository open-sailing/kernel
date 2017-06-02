/*
 * Driver for Hisilicon Last Level Cache.
 *
 * Copyright (C) 2013-2014 Hisilicon Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __HISI_LLC_H
#define __HISI_LLC_H

#ifdef CONFIG_HISI_LLC
extern void llc_inv_all(void);
extern void llc_clean_all(void);
extern void llc_flush_all(void);
extern void llc_inv_range(phys_addr_t start, size_t size);
extern void llc_clean_range(phys_addr_t start, size_t size);
extern void llc_flush_range(phys_addr_t start, size_t size);
#else
static inline void llc_flush_all(void) { }
#endif

#endif
