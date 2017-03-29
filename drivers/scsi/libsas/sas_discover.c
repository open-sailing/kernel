/*
 * Serial Attached SCSI (SAS) Discover process
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/async.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_eh.h>
#include "sas_internal.h"

#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include <scsi/sas_ata.h>
#include "../scsi_sas_internal.h"

static void sas_unregister_common_dev(struct asd_sas_port *port,
				struct domain_device *dev);
static void sas_unregister_fail_dev(struct asd_sas_port *port,
				struct domain_device *dev);

/* ---------- Basic task processing for discovery purposes ---------- */

void sas_init_dev(struct domain_device *dev)
{
	switch (dev->dev_type) {
	case SAS_END_DEVICE:
		INIT_LIST_HEAD(&dev->ssp_dev.eh_list_node);
		break;
	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
		INIT_LIST_HEAD(&dev->ex_dev.children);
		mutex_init(&dev->ex_dev.cmd_mutex);
		break;
	default:
		break;
	}
}

/* ---------- Domain device discovery ---------- */

/**
 * sas_get_port_device -- Discover devices which caused port creation
 * @port: pointer to struct sas_port of interest
 *
 * Devices directly attached to a HA port, have no parent.  This is
 * how we know they are (domain) "root" devices.  All other devices
 * do, and should have their "parent" pointer set appropriately as
 * soon as a child device is discovered.
 */
static int sas_get_port_device(struct asd_sas_port *port)
{
	struct asd_sas_phy *phy;
	struct sas_rphy *rphy;
	struct domain_device *dev;
	int rc = -ENODEV;

	dev = sas_alloc_device();
	if (!dev)
		return -ENOMEM;

	spin_lock_irq(&port->phy_list_lock);
	if (list_empty(&port->phy_list)) {
		spin_unlock_irq(&port->phy_list_lock);
		sas_put_device(dev);
		return -ENODEV;
	}
	phy = container_of(port->phy_list.next, struct asd_sas_phy, port_phy_el);
	spin_lock(&phy->frame_rcvd_lock);
	memcpy(dev->frame_rcvd, phy->frame_rcvd, min(sizeof(dev->frame_rcvd),
					     (size_t)phy->frame_rcvd_size));
	spin_unlock(&phy->frame_rcvd_lock);
	spin_unlock_irq(&port->phy_list_lock);

	if (dev->frame_rcvd[0] == 0x34 && port->oob_mode == SATA_OOB_MODE) {
		struct dev_to_host_fis *fis =
			(struct dev_to_host_fis *) dev->frame_rcvd;
		if (fis->interrupt_reason == 1 && fis->lbal == 1 &&
		    fis->byte_count_low==0x69 && fis->byte_count_high == 0x96
		    && (fis->device & ~0x10) == 0)
			dev->dev_type = SAS_SATA_PM;
		else
			dev->dev_type = SAS_SATA_DEV;
		dev->tproto = SAS_PROTOCOL_SATA;
	} else {
		struct sas_identify_frame *id =
			(struct sas_identify_frame *) dev->frame_rcvd;
		dev->dev_type = id->dev_type;
		dev->iproto = id->initiator_bits;
		dev->tproto = id->target_bits;
	}

	sas_init_dev(dev);

	dev->port = port;
	switch (dev->dev_type) {
	case SAS_SATA_DEV:
		rc = sas_ata_init(dev);
		if (rc) {
			rphy = NULL;
			break;
		}
		/* fall through */
	case SAS_END_DEVICE:
		rphy = sas_end_device_alloc(port->port);
		break;
	case SAS_EDGE_EXPANDER_DEVICE:
		rphy = sas_expander_alloc(port->port,
					  SAS_EDGE_EXPANDER_DEVICE);
		break;
	case SAS_FANOUT_EXPANDER_DEVICE:
		rphy = sas_expander_alloc(port->port,
					  SAS_FANOUT_EXPANDER_DEVICE);
		break;
	default:
		printk("ERROR: Unidentified device type %d\n", dev->dev_type);
		rphy = NULL;
		break;
	}

	if (!rphy) {
		sas_put_device(dev);
		return rc;
	}

	rphy->identify.phy_identifier = phy->phy->identify.phy_identifier;
	memcpy(dev->sas_addr, port->attached_sas_addr, SAS_ADDR_SIZE);
	sas_fill_in_rphy(dev, rphy);
	sas_hash_addr(dev->hashed_sas_addr, dev->sas_addr);
	port->port_dev = dev;
	dev->linkrate = port->linkrate;
	dev->min_linkrate = port->linkrate;
	dev->max_linkrate = port->linkrate;
	dev->pathways = port->num_phys;
	memset(port->disc.fanout_sas_addr, 0, SAS_ADDR_SIZE);
	memset(port->disc.eeds_a, 0, SAS_ADDR_SIZE);
	memset(port->disc.eeds_b, 0, SAS_ADDR_SIZE);
	port->disc.max_level = 0;
	sas_device_set_phy(dev, port->port);

	dev->rphy = rphy;
	get_device(&dev->rphy->dev);

	if (dev_is_sata(dev) || dev->dev_type == SAS_END_DEVICE)
		list_add_tail(&dev->disco_list_node, &port->disco_list);
	else
		list_add_tail(&dev->dev_list_node, &port->expander_list);

	spin_lock_irq(&port->phy_list_lock);
	list_for_each_entry(phy, &port->phy_list, port_phy_el)
		sas_phy_set_target(phy, dev);
	spin_unlock_irq(&port->phy_list_lock);

	return 0;
}

/* ---------- Discover and Revalidate ---------- */

int sas_notify_lldd_dev_found(struct domain_device *dev)
{
	int res = 0;
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct Scsi_Host *shost = sas_ha->core.shost;
	struct sas_internal *i = to_sas_internal(shost->transportt);

	if (!i->dft->lldd_dev_found)
		return 0;

	res = i->dft->lldd_dev_found(dev);
	if (res) {
		printk("sas: driver on pcidev %s cannot handle "
		       "device %llx, error:%d\n",
		       dev_name(sas_ha->dev),
		       SAS_ADDR(dev->sas_addr), res);
	}
	set_bit(SAS_DEV_FOUND, &dev->state);
	kref_get(&dev->kref);
	return res;
}

void sas_notify_lldd_before_dev_gone(struct domain_device *dev)
{
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct Scsi_Host *shost = sas_ha->core.shost;
	struct sas_internal *i = to_sas_internal(shost->transportt);

	if (i->dft->lldd_before_dev_gone)
		i->dft->lldd_before_dev_gone(dev);
}

void sas_notify_lldd_dev_gone(struct domain_device *dev)
{
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct Scsi_Host *shost = sas_ha->core.shost;
	struct sas_internal *i = to_sas_internal(shost->transportt);

	if (!i->dft->lldd_dev_gone)
		return;

	if (test_and_clear_bit(SAS_DEV_FOUND, &dev->state)) {
		i->dft->lldd_dev_gone(dev);
		sas_put_device(dev);
	}
}

static void sas_add_device(struct work_struct *work)
{
	int err;
	struct sas_topo_event *ev = to_sas_topo_event(work);
	struct domain_device *dev = ev->device;
	struct asd_sas_port *port = dev->port;


	/* if device is not on disco_list, BUG! */
	BUG_ON(list_empty(&dev->disco_list_node));

	/* try to add a device that has gone */
	if (test_bit(SAS_DEV_DESTROY, &dev->state))
		goto out;

	mutex_lock(&port->ha->disco_mutex);
	spin_lock_irq(&port->dev_list_lock);
	list_add_tail(&dev->dev_list_node, &port->dev_list);
	spin_unlock_irq(&port->dev_list_lock);
	mutex_unlock(&port->ha->disco_mutex);

	sas_probe_sata_device(dev);

	if (!test_bit(SAS_DEV_PROBE_FAIL, &dev->state)) {
		err = sas_rphy_add(dev->rphy);

		if (err)
			sas_fail_probe(dev, __func__, err);
	}

out:
	/* race with discovery */
	mutex_lock(&port->ha->disco_mutex);
	list_del_init(&dev->disco_list_node);
	mutex_unlock(&port->ha->disco_mutex);

	kfree(ev);
}

static void sas_del_device(struct work_struct *work)
{
	struct sas_topo_event *ev = to_sas_topo_event(work);
	struct domain_device *dev = ev->device;
	struct asd_sas_port *port = dev->port;

	struct sas_port *sas_port = dev_to_sas_port(dev->rphy->dev.parent);

	if (dev->dev_type == SAS_EDGE_EXPANDER_DEVICE
			|| dev->dev_type == SAS_FANOUT_EXPANDER_DEVICE)
		sas_del_parent_port(dev);

	/* expander can not come to this branch */
	if (list_empty(&dev->dev_list_node)) {
		sas_rphy_free(dev->rphy);
		sas_unregister_fail_dev(port, dev);
		goto out;
	}

	if (test_and_clear_bit(SAS_DEV_PROBE_FAIL, &dev->state)) {
		/* this rphy never saw sas_rphy_add */
		sas_rphy_free(dev->rphy);
		sas_unregister_common_dev(port, dev);

		goto out;
	}

	sas_notify_lldd_before_dev_gone(dev);
	sas_remove_children(&dev->rphy->dev);
	sas_rphy_delete(dev->rphy);
	sas_unregister_common_dev(port, dev);

out:
	if (!sas_port->num_phys)
		sas_port_delete(sas_port);

	kfree(ev);
}

static void sas_suspend_devices(struct work_struct *work)
{
	struct asd_sas_phy *phy;
	struct domain_device *dev;
	struct sas_discovery_event *ev = to_sas_discovery_event(work);
	struct asd_sas_port *port = ev->port;
	struct Scsi_Host *shost = port->ha->core.shost;
	struct sas_internal *si = to_sas_internal(shost->transportt);

	clear_bit(DISCE_SUSPEND, &port->disc.pending);

	sas_suspend_sata(port);

	/* lldd is free to forget the domain_device across the
	 * suspension, we force the issue here to keep the reference
	 * counts aligned
	 */
	list_for_each_entry(dev, &port->dev_list, dev_list_node)
		sas_notify_lldd_dev_gone(dev);
	list_for_each_entry(dev, &port->expander_list, dev_list_node)
		sas_notify_lldd_dev_gone(dev);

	/* we are suspending, so we know events are disabled and
	 * phy_list is not being mutated
	 */
	list_for_each_entry(phy, &port->phy_list, port_phy_el) {
		if (si->dft->lldd_port_formed)
			si->dft->lldd_port_deformed(phy);
		phy->suspended = 1;
		port->suspended = 1;
	}
}

static void sas_resume_devices(struct work_struct *work)
{
	struct sas_discovery_event *ev = to_sas_discovery_event(work);
	struct asd_sas_port *port = ev->port;

	clear_bit(DISCE_RESUME, &port->disc.pending);

	sas_resume_sata(port);
}

const work_func_t sas_topo_event_fns[SAS_DEVICE_NUM_EVENTS] = {
		[SAS_DEVICE_ADD] = sas_add_device,
		[SAS_DEVICE_DEL] = sas_del_device,
	};

int sas_notify_device_event(struct domain_device *dev, enum sas_device_event ev)
{
	struct sas_topo_event *topo_chg_evt;

	BUG_ON(ev >= SAS_DEVICE_NUM_EVENTS);

	topo_chg_evt = kmalloc(sizeof(*topo_chg_evt), GFP_KERNEL);
	if (!topo_chg_evt)
		return 0;

	INIT_WORK(&topo_chg_evt->work, sas_topo_event_fns[ev]);
	topo_chg_evt->device = dev;
	topo_chg_evt->event = ev;

	return queue_work(topo_wq, &topo_chg_evt->work);
}

/**
 * sas_discover_end_dev -- discover an end device (SSP, etc)
 * @end: pointer to domain device of interest
 *
 * See comment in sas_discover_sata().
 */
int sas_discover_end_dev(struct domain_device *dev)
{
	int res;

	res = sas_notify_lldd_dev_found(dev);
	if (res)
		return res;
	sas_notify_device_event(dev, SAS_DEVICE_ADD);

	return 0;
}

/* ---------- Device registration and unregistration ---------- */

void sas_free_device(struct kref *kref)
{
	struct domain_device *dev = container_of(kref, typeof(*dev), kref);

	put_device(&dev->rphy->dev);
	dev->rphy = NULL;

	if (dev->parent)
		sas_put_device(dev->parent);

	sas_port_put_phy(dev->phy);
	dev->phy = NULL;

	/* remove the phys and ports, everything else should be gone */
	if (dev->dev_type == SAS_EDGE_EXPANDER_DEVICE || dev->dev_type == SAS_FANOUT_EXPANDER_DEVICE)
		kfree(dev->ex_dev.ex_phy);

	if (dev_is_sata(dev) && dev->sata_dev.ap) {
		ata_sas_port_destroy(dev->sata_dev.ap);
		dev->sata_dev.ap = NULL;
	}

	kfree(dev);
}

static void sas_unregister_fail_dev(struct asd_sas_port *port,
				struct domain_device *dev)
{
	sas_notify_lldd_dev_gone(dev);

	/* race with discovery */
	mutex_lock(&port->ha->disco_mutex);
	if (!dev->parent)
		dev->port->port_dev = NULL;
	else
		list_del_init(&dev->siblings);

	mutex_unlock(&port->ha->disco_mutex);

	sas_put_device(dev);
}

static void sas_unregister_common_dev(struct asd_sas_port *port, struct domain_device *dev)
{
	struct sas_ha_struct *ha = port->ha;

	sas_notify_lldd_dev_gone(dev);

	/* race with discovery */
	mutex_lock(&port->ha->disco_mutex);
	if (!dev->parent)
		dev->port->port_dev = NULL;
	else
		list_del_init(&dev->siblings);

	if (dev->dev_type == SAS_EDGE_EXPANDER_DEVICE
		|| dev->dev_type == SAS_FANOUT_EXPANDER_DEVICE) {
		list_del_init(&dev->dev_list_node);
	} else {
		spin_lock_irq(&port->dev_list_lock);
		list_del_init(&dev->dev_list_node);
		if (dev_is_sata(dev))
			sas_ata_end_eh(dev->sata_dev.ap);
		spin_unlock_irq(&port->dev_list_lock);
	}
	mutex_unlock(&port->ha->disco_mutex);

	spin_lock_irq(&ha->lock);
	if (dev->dev_type == SAS_END_DEVICE &&
	    !list_empty(&dev->ssp_dev.eh_list_node)) {
		list_del_init(&dev->ssp_dev.eh_list_node);
		ha->eh_active--;
	}
	spin_unlock_irq(&ha->lock);

	sas_put_device(dev);
}

void sas_unregister_dev(struct asd_sas_port *port, struct domain_device *dev)
{
	if (!test_and_set_bit(SAS_DEV_DESTROY, &dev->state))
		sas_notify_device_event(dev, SAS_DEVICE_DEL);
}

void sas_unregister_domain_devices(struct asd_sas_port *port, int gone)
{
	struct domain_device *dev, *n;

	/* race with device add or device delete */
	mutex_lock(&port->ha->disco_mutex);

	list_for_each_entry_safe(dev, n, &port->disco_list, disco_list_node)
		sas_unregister_dev(port, dev);

	list_for_each_entry_safe_reverse(dev, n, &port->dev_list, dev_list_node) {
		if (gone)
			set_bit(SAS_DEV_GONE, &dev->state);
		sas_unregister_dev(port, dev);
	}

	list_for_each_entry_safe_reverse(dev, n,
				&port->expander_list, dev_list_node) {
		if (gone)
			set_bit(SAS_DEV_GONE, &dev->state);
		sas_unregister_dev(port, dev);
	}
	mutex_unlock(&port->ha->disco_mutex);

	port->port->rphy = NULL;

}

void sas_device_set_phy(struct domain_device *dev, struct sas_port *port)
{
	struct sas_ha_struct *ha;
	struct sas_phy *new_phy;

	if (!dev)
		return;

	ha = dev->port->ha;
	new_phy = sas_port_get_phy(port);

	/* pin and record last seen phy */
	spin_lock_irq(&ha->phy_port_lock);
	if (new_phy) {
		sas_port_put_phy(dev->phy);
		dev->phy = new_phy;
	}
	spin_unlock_irq(&ha->phy_port_lock);
}

/* ---------- Discovery and Revalidation ---------- */

#define SAS_MAX_WAIT_RESCOURCE_CLEAR_TIME (3 * 60)

/**
 * sas_discover_domain -- discover the domain
 * @port: port to the domain of interest
 *
 * NOTE: this process _must_ quit (return) as soon as any connection
 * errors are encountered.  Connection recovery is done elsewhere.
 * Discover process only interrogates devices in order to discover the
 * domain.
 */

void sas_discover_domain(struct asd_sas_port *port)
{
	struct domain_device *dev;
	int error = 0;

	if (port->port_dev) {
		int cnt = 0;

		while (1) {
			msleep(100);

			mutex_lock(&port->ha->disco_mutex);
			if (list_empty(&port->dev_list)
				&& list_empty(&port->expander_list)
				&& list_empty(&port->disco_list)) {
				mutex_unlock(&port->ha->disco_mutex);
				break;
			}
			mutex_unlock(&port->ha->disco_mutex);

			cnt++;
			if (cnt > SAS_MAX_WAIT_RESCOURCE_CLEAR_TIME * 10) {
				SAS_DPRINTK(
				"Timeout for wait port %d clear, pid:%d\n",
					port->id,
					task_pid_nr(current));
				cnt = 0;
				return;
			}
		}
	}

	mutex_lock(&port->ha->disco_mutex);
	error = sas_get_port_device(port);
	if (error)
		goto out;

	dev = port->port_dev;

	SAS_DPRINTK("DOING DISCOVERY on port %d, pid:%d\n", port->id,
		    task_pid_nr(current));

	switch (dev->dev_type) {
	case SAS_END_DEVICE:
		error = sas_discover_end_dev(dev);
		break;
	case SAS_EDGE_EXPANDER_DEVICE:
	case SAS_FANOUT_EXPANDER_DEVICE:
		error = sas_discover_root_expander(dev);
		break;
	case SAS_SATA_DEV:
	case SAS_SATA_PM:
#ifdef CONFIG_SCSI_SAS_ATA
		error = sas_discover_sata(dev);
		break;
#else
		SAS_DPRINTK("ATA device seen but CONFIG_SCSI_SAS_ATA=N so cannot attach\n");
		/* Fall through */
#endif
	default:
		error = -ENXIO;
		SAS_DPRINTK("unhandled device %d\n", dev->dev_type);
		break;
	}

	if (error) {
		sas_rphy_free(dev->rphy);
		list_del_init(&dev->disco_list_node);
		list_del_init(&dev->dev_list_node);

		sas_put_device(dev);
		port->port_dev = NULL;
	}

out:
	mutex_unlock(&port->ha->disco_mutex);
	SAS_DPRINTK("DONE DISCOVERY on port %d, pid:%d, result:%d\n", port->id,
		    task_pid_nr(current), error);
}

static void sas_revalidate_domain(struct work_struct *work)
{
	int res = 0;
	struct sas_discovery_event *ev = to_sas_discovery_event(work);
	struct asd_sas_port *port = ev->port;
	struct sas_ha_struct *ha = port->ha;
	struct domain_device *ddev = port->port_dev;

	/* prevent revalidation from finding sata links in recovery */
	mutex_lock(&ha->disco_mutex);
	if (test_bit(SAS_HA_ATA_EH_ACTIVE, &ha->state)) {
		SAS_DPRINTK("REVALIDATION DEFERRED on port %d, pid:%d\n",
			    port->id, task_pid_nr(current));
		goto out;
	}

	clear_bit(DISCE_REVALIDATE_DOMAIN, &port->disc.pending);

	SAS_DPRINTK("REVALIDATING DOMAIN on port %d, pid:%d\n", port->id,
		    task_pid_nr(current));

	if (ddev && (ddev->dev_type == SAS_FANOUT_EXPANDER_DEVICE ||
		     ddev->dev_type == SAS_EDGE_EXPANDER_DEVICE))
		res = sas_ex_revalidate_domain(ddev);

	SAS_DPRINTK("done REVALIDATING DOMAIN on port %d, pid:%d, res 0x%x\n",
		    port->id, task_pid_nr(current), res);
 out:
	mutex_unlock(&ha->disco_mutex);
}

/* ---------- Events ---------- */

static void sas_chain_work(struct sas_ha_struct *ha, struct sas_work *sw)
{
	/* chained work is not subject to SA_HA_DRAINING or
	 * SAS_HA_REGISTERED, because it is either submitted in the
	 * workqueue, or known to be submitted from a context that is
	 * not racing against draining
	 */
	scsi_queue_work(ha->core.shost, &sw->work);
}

static void sas_chain_event(struct sas_work *sw,
			    struct sas_ha_struct *ha)
{
		unsigned long flags;

		spin_lock_irqsave(&ha->lock, flags);
		sas_chain_work(ha, sw);
		spin_unlock_irqrestore(&ha->lock, flags);
}

const work_func_t sas_disc_event_fns[DISC_NUM_EVENTS] = {
		[DISCE_REVALIDATE_DOMAIN] = sas_revalidate_domain,
		[DISCE_SUSPEND] = sas_suspend_devices,
		[DISCE_RESUME] = sas_resume_devices,
	};


static void sas_discover_event_handler(struct work_struct *work)
{
	struct sas_discovery_event *ev = to_sas_discovery_event(work);
	enum discover_event evt = ev->evt;

	BUG_ON(evt >= DISC_NUM_EVENTS);

	sas_disc_event_fns[evt](work);

	kfree(ev);
}

int sas_discover_event(struct asd_sas_port *port, enum discover_event ev)
{
	struct sas_discovery_event *disc_ev;

	if (!port)
		return 0;

	BUG_ON(ev >= DISC_NUM_EVENTS);

	disc_ev = kmalloc(sizeof(*disc_ev), GFP_KERNEL);
	if (!disc_ev)
		return 0;


	INIT_SAS_WORK(&disc_ev->work, sas_discover_event_handler);
	disc_ev->port = port;
	disc_ev->evt = ev;

	sas_chain_event(&disc_ev->work, port->ha);

	return 0;
}
