/*
 * Linux Driver for Mylex DAC960/AcceleRAID/eXtremeRAID PCI RAID Controllers
 *
 * This driver supports the newer, SCSI-based firmware interface only.
 *
 * Copyright 2017 Hannes Reinecke, SUSE Linux GmbH <hare@suse.com>
 *
 * Based on the original DAC960 driver, which has
 * Copyright 1998-2001 by Leonard N. Zubkoff <lnz@dandelion.com>
 * Portions Copyright 2002 by Mylex (An IBM Business Unit)
 *
 * This program is free software; you may redistribute and/or modify it under
 * the terms of the GNU General Public License Version 2 as published by the
 *  Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for complete details.
 */


#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/raid_class.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include "myrs.h"

static struct raid_template *myrs_raid_template;

static struct myrs_devstate_name_entry {
	myrs_devstate state;
	char *name;
} myrs_devstate_name_list[] = {
	{ DAC960_V2_Device_Unconfigured, "Unconfigured" },
	{ DAC960_V2_Device_Online, "Online" },
	{ DAC960_V2_Device_Rebuild, "Rebuild" },
	{ DAC960_V2_Device_Missing, "Missing" },
	{ DAC960_V2_Device_SuspectedCritical, "SuspectedCritical" },
	{ DAC960_V2_Device_Offline, "Offline" },
	{ DAC960_V2_Device_Critical, "Critical" },
	{ DAC960_V2_Device_SuspectedDead, "SuspectedDead" },
	{ DAC960_V2_Device_CommandedOffline, "CommandedOffline" },
	{ DAC960_V2_Device_Standby, "Standby" },
	{ DAC960_V2_Device_InvalidState, NULL },
};

static char *myrs_devstate_name(myrs_devstate state)
{
	struct myrs_devstate_name_entry *entry = myrs_devstate_name_list;

	while (entry && entry->name) {
		if (entry->state == state)
			return entry->name;
		entry++;
	}
	return NULL;
}

static struct myrs_raid_level_name_entry {
	myrs_raid_level level;
	char *name;
} myrs_raid_level_name_list[] = {
	{ DAC960_V2_RAID_Level0, "RAID0" },
	{ DAC960_V2_RAID_Level1, "RAID1" },
	{ DAC960_V2_RAID_Level3, "RAID3 right asymmetric parity" },
	{ DAC960_V2_RAID_Level5, "RAID5 right asymmetric parity" },
	{ DAC960_V2_RAID_Level6, "RAID6" },
	{ DAC960_V2_RAID_JBOD, "JBOD" },
	{ DAC960_V2_RAID_NewSpan, "New Mylex SPAN" },
	{ DAC960_V2_RAID_Level3F, "RAID3 fixed parity" },
	{ DAC960_V2_RAID_Level3L, "RAID3 left symmetric parity" },
	{ DAC960_V2_RAID_Span, "Mylex SPAN" },
	{ DAC960_V2_RAID_Level5L, "RAID5 left symmetric parity" },
	{ DAC960_V2_RAID_LevelE, "RAIDE (concatenation)" },
	{ DAC960_V2_RAID_Physical, "Physical device" },
	{ 0xff, NULL }
};

static char *myrs_raid_level_name(myrs_raid_level level)
{
	struct myrs_raid_level_name_entry *entry = myrs_raid_level_name_list;

	while (entry && entry->name) {
		if (entry->level == level)
			return entry->name;
		entry++;
	}
	return NULL;
}

/*
  myrs_reset_cmd clears critical fields of Command for DAC960 V2
  Firmware Controllers.
*/

static inline void myrs_reset_cmd(myrs_cmdblk *cmd_blk)
{
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;

	memset(mbox, 0, sizeof(myrs_cmd_mbox));
	cmd_blk->status = 0;
}


/*
 * myrs_qcmd queues Command for DAC960 V2 Series Controllers.
 */
static void myrs_qcmd(myrs_hba *cs, myrs_cmdblk *cmd_blk)
{
	void __iomem *base = cs->io_base;
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	myrs_cmd_mbox *next_mbox = cs->next_cmd_mbox;

	cs->write_cmd_mbox(next_mbox, mbox);

	if (cs->prev_cmd_mbox1->Words[0] == 0 ||
	    cs->prev_cmd_mbox2->Words[0] == 0)
		cs->get_cmd_mbox(base);

	cs->prev_cmd_mbox2 = cs->prev_cmd_mbox1;
	cs->prev_cmd_mbox1 = next_mbox;

	if (++next_mbox > cs->last_cmd_mbox)
		next_mbox = cs->first_cmd_mbox;

	cs->next_cmd_mbox = next_mbox;
}

/*
 * myrs_exec_cmd executes V2 Command and waits for completion.
 */

static void myrs_exec_cmd(myrs_hba *cs,
			  myrs_cmdblk *cmd_blk)
{
	DECLARE_COMPLETION_ONSTACK(Completion);
	unsigned long flags;

	cmd_blk->Completion = &Completion;
	spin_lock_irqsave(&cs->queue_lock, flags);
	myrs_qcmd(cs, cmd_blk);
	spin_unlock_irqrestore(&cs->queue_lock, flags);

	if (in_interrupt())
		return;
	wait_for_completion(&Completion);
}


/*
  myrs_report_progress prints an appropriate progress message for
  Logical Device Long Operations.
*/

static void
myrs_report_progress(myrs_hba *cs, unsigned short ldev_num,
		     unsigned char *msg, unsigned long blocks,
		     unsigned long size)
{
	shost_printk(KERN_INFO, cs->host,
		     "Logical Drive %d: %s in Progress: %ld%% completed\n",
		     ldev_num, msg, (100 * (blocks >> 7)) / (size >> 7));
}


/*
  myrs_get_ctlr_info executes a DAC960 V2 Firmware Controller
  Information Reading IOCTL Command and waits for completion.
*/

static unsigned char
myrs_get_ctlr_info(myrs_hba *cs)
{
	myrs_cmdblk *cmd_blk = &cs->dcmd_blk;
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	dma_addr_t ctlr_info_addr;
	myrs_sgl *sgl;
	unsigned char status;
	myrs_ctlr_info old;

	memcpy(&old, cs->ctlr_info, sizeof(myrs_ctlr_info));
	ctlr_info_addr = dma_map_single(&cs->pdev->dev, cs->ctlr_info,
					sizeof(myrs_ctlr_info),
					DMA_FROM_DEVICE);
	if (dma_mapping_error(&cs->pdev->dev, ctlr_info_addr))
		return DAC960_V2_AbnormalCompletion;

	mutex_lock(&cs->dcmd_mutex);
	myrs_reset_cmd(cmd_blk);
	mbox->ControllerInfo.id = MYRS_DCMD_TAG;
	mbox->ControllerInfo.opcode = DAC960_V2_IOCTL;
	mbox->ControllerInfo.control.DataTransferControllerToHost = true;
	mbox->ControllerInfo.control.NoAutoRequestSense = true;
	mbox->ControllerInfo.dma_size = sizeof(myrs_ctlr_info);
	mbox->ControllerInfo.ctlr_num = 0;
	mbox->ControllerInfo.ioctl_opcode = DAC960_V2_GetControllerInfo;
	sgl = &mbox->ControllerInfo.dma_addr;
	sgl->sge[0].sge_addr = ctlr_info_addr;
	sgl->sge[0].sge_count = mbox->ControllerInfo.dma_size;
	dev_dbg(&cs->host->shost_gendev, "Sending GetControllerInfo\n");
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	dma_unmap_single(&cs->pdev->dev, ctlr_info_addr,
			 sizeof(myrs_ctlr_info), DMA_FROM_DEVICE);
	if (status == DAC960_V2_NormalCompletion) {
		if (cs->ctlr_info->bg_init_active +
		    cs->ctlr_info->ldev_init_active +
		    cs->ctlr_info->pdev_init_active +
		    cs->ctlr_info->cc_active +
		    cs->ctlr_info->rbld_active +
		    cs->ctlr_info->exp_active != 0)
			cs->needs_update = true;
		if (cs->ctlr_info->ldev_present != old.ldev_present ||
		    cs->ctlr_info->ldev_critical != old.ldev_critical ||
		    cs->ctlr_info->ldev_offline != old.ldev_offline)
			shost_printk(KERN_INFO, cs->host,
				     "Logical drive count changes (%d/%d/%d)\n",
				     cs->ctlr_info->ldev_critical,
				     cs->ctlr_info->ldev_offline,
				     cs->ctlr_info->ldev_present);
	}

	return status;
}


/*
  myrs_get_ldev_info executes a DAC960 V2 Firmware Controller Logical
  Device Information Reading IOCTL Command and waits for completion.
*/

static unsigned char
myrs_get_ldev_info(myrs_hba *cs, unsigned short ldev_num,
		   myrs_ldev_info *ldev_info)
{
	myrs_cmdblk *cmd_blk = &cs->dcmd_blk;
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	dma_addr_t ldev_info_addr;
	myrs_ldev_info ldev_info_orig;
	myrs_sgl *sgl;
	unsigned char status;

	memcpy(&ldev_info_orig, ldev_info, sizeof(myrs_ldev_info));
	ldev_info_addr = dma_map_single(&cs->pdev->dev, ldev_info,
					sizeof(myrs_ldev_info),
					DMA_FROM_DEVICE);
	if (dma_mapping_error(&cs->pdev->dev, ldev_info_addr))
		return DAC960_V2_AbnormalCompletion;

	mutex_lock(&cs->dcmd_mutex);
	myrs_reset_cmd(cmd_blk);
	mbox->LogicalDeviceInfo.id = MYRS_DCMD_TAG;
	mbox->LogicalDeviceInfo.opcode = DAC960_V2_IOCTL;
	mbox->LogicalDeviceInfo.control.DataTransferControllerToHost = true;
	mbox->LogicalDeviceInfo.control.NoAutoRequestSense = true;
	mbox->LogicalDeviceInfo.dma_size = sizeof(myrs_ldev_info);
	mbox->LogicalDeviceInfo.ldev.ldev_num = ldev_num;
	mbox->LogicalDeviceInfo.ioctl_opcode =
		DAC960_V2_GetLogicalDeviceInfoValid;
	sgl = &mbox->LogicalDeviceInfo.dma_addr;
	sgl->sge[0].sge_addr = ldev_info_addr;
	sgl->sge[0].sge_count = mbox->LogicalDeviceInfo.dma_size;
	dev_dbg(&cs->host->shost_gendev,
		"Sending GetLogicalDeviceInfoValid for ldev %d\n", ldev_num);
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	dma_unmap_single(&cs->pdev->dev, ldev_info_addr,
			 sizeof(myrs_ldev_info), DMA_FROM_DEVICE);
	if (status == DAC960_V2_NormalCompletion) {
		unsigned short ldev_num = ldev_info->ldev_num;
		myrs_ldev_info *new = ldev_info;
		myrs_ldev_info *old = &ldev_info_orig;
		unsigned long ldev_size = new->cfg_devsize;

		if (new->State != old->State) {
			const char *name;

			name = myrs_devstate_name(new->State);
			shost_printk(KERN_INFO, cs->host,
				     "Logical Drive %d is now %s\n",
				     ldev_num, name ? name : "Invalid");
		}
		if ((new->SoftErrors != old->SoftErrors) ||
		    (new->CommandsFailed != old->CommandsFailed) ||
		    (new->DeferredWriteErrors !=
		     old->DeferredWriteErrors))
			shost_printk(KERN_INFO, cs->host,
				     "Logical Drive %d Errors: "
				     "Soft = %d, Failed = %d, Deferred Write = %d\n",
				     ldev_num,
				     new->SoftErrors,
				     new->CommandsFailed,
				     new->DeferredWriteErrors);
		if (new->bg_init_active)
			myrs_report_progress(cs, ldev_num,
					     "Background Initialization",
					     new->bg_init_lba, ldev_size);
		else if (new->fg_init_active)
			myrs_report_progress(cs, ldev_num,
					     "Foreground Initialization",
					     new->fg_init_lba, ldev_size);
		else if (new->migration_active)
			myrs_report_progress(cs, ldev_num,
					     "Data Migration",
					     new->migration_lba, ldev_size);
		else if (new->patrol_active)
			myrs_report_progress(cs, ldev_num,
					     "Patrol Operation",
					     new->patrol_lba, ldev_size);
		if (old->bg_init_active && !new->bg_init_active)
			shost_printk(KERN_INFO, cs->host,
				     "Logical Drive %d: "
				     "Background Initialization %s\n",
				     ldev_num,
				     (new->ldev_control.ldev_init_done ?
				      "Completed" : "Failed"));
	}
	return status;
}


/*
  myrs_get_pdev_info executes a DAC960 V2 Firmware Controller "Read
  Physical Device Information" IOCTL Command and waits for completion.
*/

static unsigned char
myrs_get_pdev_info(myrs_hba *cs, unsigned char channel,
		   unsigned char target, unsigned char lun,
		   myrs_pdev_info *pdev_info)
{
	myrs_cmdblk *cmd_blk = &cs->dcmd_blk;
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	dma_addr_t pdev_info_addr;
	myrs_sgl *sgl;
	unsigned char status;

	pdev_info_addr = dma_map_single(&cs->pdev->dev, pdev_info,
					sizeof(myrs_pdev_info),
					DMA_FROM_DEVICE);
	if (dma_mapping_error(&cs->pdev->dev, pdev_info_addr))
		return DAC960_V2_AbnormalCompletion;

	mutex_lock(&cs->dcmd_mutex);
	myrs_reset_cmd(cmd_blk);
	mbox->PhysicalDeviceInfo.opcode = DAC960_V2_IOCTL;
	mbox->PhysicalDeviceInfo.id = MYRS_DCMD_TAG;
	mbox->PhysicalDeviceInfo.control.DataTransferControllerToHost = true;
	mbox->PhysicalDeviceInfo.control.NoAutoRequestSense = true;
	mbox->PhysicalDeviceInfo.dma_size = sizeof(myrs_pdev_info);
	mbox->PhysicalDeviceInfo.pdev.LogicalUnit = lun;
	mbox->PhysicalDeviceInfo.pdev.TargetID = target;
	mbox->PhysicalDeviceInfo.pdev.Channel = channel;
	mbox->PhysicalDeviceInfo.ioctl_opcode =
		DAC960_V2_GetPhysicalDeviceInfoValid;
	sgl = &mbox->PhysicalDeviceInfo.dma_addr;
	sgl->sge[0].sge_addr = pdev_info_addr;
	sgl->sge[0].sge_count = mbox->PhysicalDeviceInfo.dma_size;
	dev_dbg(&cs->host->shost_gendev,
		"Sending GetPhysicalDeviceInfoValid for pdev %d:%d:%d\n",
		channel, target, lun);
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	dma_unmap_single(&cs->pdev->dev, pdev_info_addr,
			 sizeof(myrs_pdev_info), DMA_FROM_DEVICE);
	return status;
}

/*
  myrs_dev_op executes a DAC960 V2 Firmware Controller Device
  Operation IOCTL Command and waits for completion.
*/

static unsigned char
myrs_dev_op(myrs_hba *cs, myrs_ioctl_opcode opcode, myrs_opdev opdev)
{
	myrs_cmdblk *cmd_blk = &cs->dcmd_blk;
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	unsigned char status;

	mutex_lock(&cs->dcmd_mutex);
	myrs_reset_cmd(cmd_blk);
	mbox->DeviceOperation.opcode = DAC960_V2_IOCTL;
	mbox->DeviceOperation.id = MYRS_DCMD_TAG;
	mbox->DeviceOperation.control.DataTransferControllerToHost = true;
	mbox->DeviceOperation.control.NoAutoRequestSense = true;
	mbox->DeviceOperation.ioctl_opcode = opcode;
	mbox->DeviceOperation.opdev = opdev;
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	return status;
}


/*
  myrs_translate_pdev translates a Physical Device Channel and
  TargetID into a Logical Device.
*/

static unsigned char
myrs_translate_pdev(myrs_hba *cs, unsigned char channel,
		    unsigned char target, unsigned char lun,
		    myrs_devmap *devmap)
{
	struct pci_dev *pdev = cs->pdev;
	dma_addr_t devmap_addr;
	myrs_cmdblk *cmd_blk;
	myrs_cmd_mbox *mbox;
	myrs_sgl *sgl;
	unsigned char status;

	memset(devmap, 0x0, sizeof(myrs_devmap));
	devmap_addr = dma_map_single(&pdev->dev, devmap,
				     sizeof(myrs_devmap), DMA_FROM_DEVICE);
	if (dma_mapping_error(&pdev->dev, devmap_addr))
		return DAC960_V2_AbnormalCompletion;

	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	mbox = &cmd_blk->mbox;
	mbox->PhysicalDeviceInfo.opcode = DAC960_V2_IOCTL;
	mbox->PhysicalDeviceInfo.control.DataTransferControllerToHost = true;
	mbox->PhysicalDeviceInfo.control.NoAutoRequestSense = true;
	mbox->PhysicalDeviceInfo.dma_size = sizeof(myrs_devmap);
	mbox->PhysicalDeviceInfo.pdev.TargetID = target;
	mbox->PhysicalDeviceInfo.pdev.Channel = channel;
	mbox->PhysicalDeviceInfo.pdev.LogicalUnit = lun;
	mbox->PhysicalDeviceInfo.ioctl_opcode =
		DAC960_V2_TranslatePhysicalToLogicalDevice;
	sgl = &mbox->PhysicalDeviceInfo.dma_addr;
	sgl->sge[0].sge_addr = devmap_addr;
	sgl->sge[0].sge_addr = mbox->PhysicalDeviceInfo.dma_size;

	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	dma_unmap_single(&pdev->dev, devmap_addr,
			 sizeof(myrs_devmap), DMA_FROM_DEVICE);
	return status;
}


/*
  myrs_get_event queues a Get Event Command
  to DAC960 V2 Firmware Controllers.
*/

static unsigned char
myrs_get_event(myrs_hba *cs, unsigned short event_num,
	       myrs_event *event_buf)
{
	struct pci_dev *pdev = cs->pdev;
	dma_addr_t event_addr;
	myrs_cmdblk *cmd_blk = &cs->mcmd_blk;
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	myrs_sgl *sgl;
	unsigned char status;

	event_addr = dma_map_single(&pdev->dev, event_buf,
				    sizeof(myrs_event), DMA_FROM_DEVICE);
	if (dma_mapping_error(&pdev->dev, event_addr))
		return DAC960_V2_AbnormalCompletion;

	mbox->GetEvent.opcode = DAC960_V2_IOCTL;
	mbox->GetEvent.dma_size = sizeof(myrs_event);
	mbox->GetEvent.evnum_upper = event_num >> 16;
	mbox->GetEvent.ctlr_num = 0;
	mbox->GetEvent.ioctl_opcode = DAC960_V2_GetEvent;
	mbox->GetEvent.evnum_lower = event_num & 0xFFFF;
	sgl = &mbox->GetEvent.dma_addr;
	sgl->sge[0].sge_addr = event_addr;
	sgl->sge[0].sge_count = mbox->GetEvent.dma_size;
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	dma_unmap_single(&pdev->dev, event_addr,
			 sizeof(myrs_event), DMA_FROM_DEVICE);

	return status;
}


/*
  myrs_get_fwstatus queues a Get Health Status Command
  to DAC960 V2 Firmware Controllers.
*/

static unsigned char myrs_get_fwstatus(myrs_hba *cs)
{
	myrs_cmdblk *cmd_blk = &cs->mcmd_blk;
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	myrs_sgl *sgl;
	unsigned char status = cmd_blk->status;

	myrs_reset_cmd(cmd_blk);
	mbox->Common.opcode = DAC960_V2_IOCTL;
	mbox->Common.id = MYRS_MCMD_TAG;
	mbox->Common.control.DataTransferControllerToHost = true;
	mbox->Common.control.NoAutoRequestSense = true;
	mbox->Common.dma_size = sizeof(myrs_fwstat);
	mbox->Common.ioctl_opcode = DAC960_V2_GetHealthStatus;
	sgl = &mbox->Common.dma_addr;
	sgl->sge[0].sge_addr = cs->fwstat_addr;
	sgl->sge[0].sge_count = mbox->ControllerInfo.dma_size;
	dev_dbg(&cs->host->shost_gendev, "Sending GetHealthStatus\n");
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;

	return status;
}

/*
  myrs_enable_mmio_mbox enables the Memory Mailbox Interface
  for DAC960 V2 Firmware Controllers.

  Aggregate the space needed for the controller's memory mailbox and
  the other data structures that will be targets of dma transfers with
  the controller.  Allocate a dma-mapped region of memory to hold these
  structures.  Then, save CPU pointers and dma_addr_t values to reference
  the structures that are contained in that region.
*/

static bool myrs_enable_mmio_mbox(myrs_hba *cs, enable_mbox_t enable_mbox_fn)
{
	void __iomem *base = cs->io_base;
	struct pci_dev *pdev = cs->pdev;

	myrs_cmd_mbox *cmd_mbox;
	myrs_stat_mbox *stat_mbox;

	myrs_cmd_mbox *mbox;
	dma_addr_t mbox_addr;
	unsigned char status = DAC960_V2_AbnormalCompletion;

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64)))
		if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
			dev_err(&pdev->dev, "DMA mask out of range\n");
			return false;
		}

	/* This is a temporary dma mapping, used only in the scope of this function */
	mbox = dma_alloc_coherent(&pdev->dev, sizeof(myrs_cmd_mbox),
				  &mbox_addr, GFP_KERNEL);
	if (dma_mapping_error(&pdev->dev, mbox_addr))
		return false;

	/* These are the base addresses for the command memory mailbox array */
	cs->cmd_mbox_size = MYRS_MAX_CMD_MBOX * sizeof(myrs_cmd_mbox);
	cmd_mbox = dma_alloc_coherent(&pdev->dev, cs->cmd_mbox_size,
				      &cs->cmd_mbox_addr, GFP_KERNEL);
	if (dma_mapping_error(&pdev->dev, cs->cmd_mbox_addr)) {
		dev_err(&pdev->dev, "Failed to map command mailbox\n");
		goto out_free;
	}
	cs->first_cmd_mbox = cmd_mbox;
	cmd_mbox += MYRS_MAX_CMD_MBOX - 1;
	cs->last_cmd_mbox = cmd_mbox;
	cs->next_cmd_mbox = cs->first_cmd_mbox;
	cs->prev_cmd_mbox1 = cs->last_cmd_mbox;
	cs->prev_cmd_mbox2 = cs->last_cmd_mbox - 1;

	/* These are the base addresses for the status memory mailbox array */
	cs->stat_mbox_size = MYRS_MAX_STAT_MBOX * sizeof(myrs_stat_mbox);
	stat_mbox = dma_alloc_coherent(&pdev->dev, cs->stat_mbox_size,
				       &cs->stat_mbox_addr, GFP_KERNEL);
	if (dma_mapping_error(&pdev->dev, cs->stat_mbox_addr)) {
		dev_err(&pdev->dev, "Failed to map status mailbox\n");
		goto out_free;
	}

	cs->first_stat_mbox = stat_mbox;
	stat_mbox += MYRS_MAX_STAT_MBOX - 1;
	cs->last_stat_mbox = stat_mbox;
	cs->next_stat_mbox = cs->first_stat_mbox;

	cs->fwstat_buf = dma_alloc_coherent(&pdev->dev, sizeof(myrs_fwstat),
					    &cs->fwstat_addr, GFP_KERNEL);
	if (dma_mapping_error(&pdev->dev, cs->fwstat_addr)) {
		dev_err(&pdev->dev, "Failed to map firmware health buffer\n");
		cs->fwstat_buf = NULL;
		goto out_free;
	}
	cs->ctlr_info = kzalloc(sizeof(myrs_ctlr_info), GFP_KERNEL | GFP_DMA);
	if (!cs->ctlr_info) {
		dev_err(&pdev->dev, "Failed to allocate controller info\n");
		goto out_free;
	}

	cs->event_buf = kzalloc(sizeof(myrs_event), GFP_KERNEL | GFP_DMA);
	if (!cs->event_buf) {
		dev_err(&pdev->dev, "Failed to allocate event buffer\n");
		goto out_free;
	}

	/*
	  Enable the Memory Mailbox Interface.
	*/
	memset(mbox, 0, sizeof(myrs_cmd_mbox));
	mbox->SetMemoryMailbox.id = 1;
	mbox->SetMemoryMailbox.opcode = DAC960_V2_IOCTL;
	mbox->SetMemoryMailbox.control.NoAutoRequestSense = true;
	mbox->SetMemoryMailbox.FirstCommandMailboxSizeKB =
		(MYRS_MAX_CMD_MBOX * sizeof(myrs_cmd_mbox)) >> 10;
	mbox->SetMemoryMailbox.FirstStatusMailboxSizeKB =
		(MYRS_MAX_STAT_MBOX * sizeof(myrs_stat_mbox)) >> 10;
	mbox->SetMemoryMailbox.SecondCommandMailboxSizeKB = 0;
	mbox->SetMemoryMailbox.SecondStatusMailboxSizeKB = 0;
	mbox->SetMemoryMailbox.sense_len = 0;
	mbox->SetMemoryMailbox.ioctl_opcode = DAC960_V2_SetMemoryMailbox;
	mbox->SetMemoryMailbox.HealthStatusBufferSizeKB = 1;
	mbox->SetMemoryMailbox.HealthStatusBufferBusAddress =
		cs->fwstat_addr;
	mbox->SetMemoryMailbox.FirstCommandMailboxBusAddress =
		cs->cmd_mbox_addr;
	mbox->SetMemoryMailbox.FirstStatusMailboxBusAddress =
		cs->stat_mbox_addr;
	status = enable_mbox_fn(base, mbox_addr);

out_free:
	dma_free_coherent(&pdev->dev, sizeof(myrs_cmd_mbox),
			  mbox, mbox_addr);
	if (status != DAC960_V2_NormalCompletion)
		dev_err(&pdev->dev, "Failed to enable mailbox, status %X\n",
			status);
	return (status == DAC960_V2_NormalCompletion);
}


/*
  myrs_get_config reads the Configuration Information
  from DAC960 V2 Firmware Controllers and initializes the Controller structure.
*/

int myrs_get_config(myrs_hba *cs)
{
	myrs_ctlr_info *info = cs->ctlr_info;
	struct Scsi_Host *shost = cs->host;
	unsigned char status;
	unsigned char ModelName[20];
	unsigned char fw_version[12];
	int i, ModelNameLength;

	/* Get data into dma-able area, then copy into permanent location */
	mutex_lock(&cs->cinfo_mutex);
	status = myrs_get_ctlr_info(cs);
	mutex_unlock(&cs->cinfo_mutex);
	if (status != DAC960_V2_NormalCompletion) {
		shost_printk(KERN_ERR, shost,
			     "Failed to get controller information\n");
		return -ENODEV;
	}

	/*
	  Initialize the Controller Model Name and Full Model Name fields.
	*/
	ModelNameLength = sizeof(info->ControllerName);
	if (ModelNameLength > sizeof(ModelName)-1)
		ModelNameLength = sizeof(ModelName)-1;
	memcpy(ModelName, info->ControllerName, ModelNameLength);
	ModelNameLength--;
	while (ModelName[ModelNameLength] == ' ' ||
	       ModelName[ModelNameLength] == '\0')
		ModelNameLength--;
	ModelName[++ModelNameLength] = '\0';
	strcpy(cs->model_name, "DAC960 ");
	strcat(cs->model_name, ModelName);
	/*
	  Initialize the Controller Firmware Version field.
	*/
	sprintf(fw_version, "%d.%02d-%02d",
		info->FirmwareMajorVersion,
		info->FirmwareMinorVersion,
		info->FirmwareTurnNumber);
	if (info->FirmwareMajorVersion == 6 &&
	    info->FirmwareMinorVersion == 0 &&
	    info->FirmwareTurnNumber < 1) {
		shost_printk(KERN_WARNING, shost,
			"FIRMWARE VERSION %s DOES NOT PROVIDE THE CONTROLLER\n"
			"STATUS MONITORING FUNCTIONALITY NEEDED BY THIS DRIVER.\n"
			"PLEASE UPGRADE TO VERSION 6.00-01 OR ABOVE.\n",
			fw_version);
		return -ENODEV;
	}
	/*
	  Initialize the Controller Channels and Targets.
	*/
	shost->max_channel = info->physchan_present + info->virtchan_present;
	shost->max_id = info->max_targets[0];
	for (i = 1; i < 16; i++) {
		if (!info->max_targets[i])
			continue;
		if (shost->max_id < info->max_targets[i])
			shost->max_id = info->max_targets[i];
	}

	/*
	 * Initialize the Controller Queue Depth, Driver Queue Depth,
	 * Logical Drive Count, Maximum Blocks per Command, Controller
	 * Scatter/Gather Limit, and Driver Scatter/Gather Limit.
	 * The Driver Queue Depth must be at most three less than
	 * the Controller Queue Depth; tag '1' is reserved for
	 * direct commands, and tag '2' for monitoring commands.
	 */
	shost->can_queue = info->max_tcq - 3;
	if (shost->can_queue > MYRS_MAX_CMD_MBOX - 3)
		shost->can_queue = MYRS_MAX_CMD_MBOX - 3;
	shost->max_sectors = info->max_transfer_size;
	shost->sg_tablesize = info->max_sge;
	if (shost->sg_tablesize > MYRS_SG_LIMIT)
		shost->sg_tablesize = MYRS_SG_LIMIT;

	shost_printk(KERN_INFO, shost,
		"Configuring %s PCI RAID Controller\n", ModelName);
	shost_printk(KERN_INFO, shost,
		"  Firmware Version: %s, Channels: %d, Memory Size: %dMB\n",
		fw_version, info->physchan_present, info->MemorySizeMB);

	shost_printk(KERN_INFO, shost,
		     "  Controller Queue Depth: %d,"
		     " Maximum Blocks per Command: %d\n",
		     shost->can_queue, shost->max_sectors);

	shost_printk(KERN_INFO, shost,
		     "  Driver Queue Depth: %d,"
		     " Scatter/Gather Limit: %d of %d Segments\n",
		     shost->can_queue, shost->sg_tablesize, MYRS_SG_LIMIT);
	for (i = 0; i < info->physchan_max; i++) {
		if (!info->max_targets[i])
			continue;
		shost_printk(KERN_INFO, shost,
			     "  Device Channel %d: max %d devices\n",
			     i, info->max_targets[i]);
	}
	shost_printk(KERN_INFO, shost,
		     "  Physical: %d/%d channels, %d disks, %d devices\n",
		     info->physchan_present, info->physchan_max,
		     info->pdisk_present, info->pdev_present);

	shost_printk(KERN_INFO, shost,
		     "  Logical: %d/%d channels, %d disks\n",
		     info->virtchan_present, info->virtchan_max,
		     info->ldev_present);
	return 0;
}

/*
  myrs_log_event prints an appropriate message when a Controller Event
  occurs.
*/

static struct {
	int ev_code;
	unsigned char *ev_msg;
} myrs_ev_list[] =
{ /* Physical Device Events (0x0000 - 0x007F) */
	{ 0x0001, "P Online" },
	{ 0x0002, "P Standby" },
	{ 0x0005, "P Automatic Rebuild Started" },
	{ 0x0006, "P Manual Rebuild Started" },
	{ 0x0007, "P Rebuild Completed" },
	{ 0x0008, "P Rebuild Cancelled" },
	{ 0x0009, "P Rebuild Failed for Unknown Reasons" },
	{ 0x000A, "P Rebuild Failed due to New Physical Device" },
	{ 0x000B, "P Rebuild Failed due to Logical Drive Failure" },
	{ 0x000C, "S Offline" },
	{ 0x000D, "P Found" },
	{ 0x000E, "P Removed" },
	{ 0x000F, "P Unconfigured" },
	{ 0x0010, "P Expand Capacity Started" },
	{ 0x0011, "P Expand Capacity Completed" },
	{ 0x0012, "P Expand Capacity Failed" },
	{ 0x0013, "P Command Timed Out" },
	{ 0x0014, "P Command Aborted" },
	{ 0x0015, "P Command Retried" },
	{ 0x0016, "P Parity Error" },
	{ 0x0017, "P Soft Error" },
	{ 0x0018, "P Miscellaneous Error" },
	{ 0x0019, "P Reset" },
	{ 0x001A, "P Active Spare Found" },
	{ 0x001B, "P Warm Spare Found" },
	{ 0x001C, "S Sense Data Received" },
	{ 0x001D, "P Initialization Started" },
	{ 0x001E, "P Initialization Completed" },
	{ 0x001F, "P Initialization Failed" },
	{ 0x0020, "P Initialization Cancelled" },
	{ 0x0021, "P Failed because Write Recovery Failed" },
	{ 0x0022, "P Failed because SCSI Bus Reset Failed" },
	{ 0x0023, "P Failed because of Double Check Condition" },
	{ 0x0024, "P Failed because Device Cannot Be Accessed" },
	{ 0x0025, "P Failed because of Gross Error on SCSI Processor" },
	{ 0x0026, "P Failed because of Bad Tag from Device" },
	{ 0x0027, "P Failed because of Command Timeout" },
	{ 0x0028, "P Failed because of System Reset" },
	{ 0x0029, "P Failed because of Busy Status or Parity Error" },
	{ 0x002A, "P Failed because Host Set Device to Failed State" },
	{ 0x002B, "P Failed because of Selection Timeout" },
	{ 0x002C, "P Failed because of SCSI Bus Phase Error" },
	{ 0x002D, "P Failed because Device Returned Unknown Status" },
	{ 0x002E, "P Failed because Device Not Ready" },
	{ 0x002F, "P Failed because Device Not Found at Startup" },
	{ 0x0030, "P Failed because COD Write Operation Failed" },
	{ 0x0031, "P Failed because BDT Write Operation Failed" },
	{ 0x0039, "P Missing at Startup" },
	{ 0x003A, "P Start Rebuild Failed due to Physical Drive Too Small" },
	{ 0x003C, "P Temporarily Offline Device Automatically Made Online" },
	{ 0x003D, "P Standby Rebuild Started" },
	/* Logical Device Events (0x0080 - 0x00FF) */
	{ 0x0080, "M Consistency Check Started" },
	{ 0x0081, "M Consistency Check Completed" },
	{ 0x0082, "M Consistency Check Cancelled" },
	{ 0x0083, "M Consistency Check Completed With Errors" },
	{ 0x0084, "M Consistency Check Failed due to Logical Drive Failure" },
	{ 0x0085, "M Consistency Check Failed due to Physical Device Failure" },
	{ 0x0086, "L Offline" },
	{ 0x0087, "L Critical" },
	{ 0x0088, "L Online" },
	{ 0x0089, "M Automatic Rebuild Started" },
	{ 0x008A, "M Manual Rebuild Started" },
	{ 0x008B, "M Rebuild Completed" },
	{ 0x008C, "M Rebuild Cancelled" },
	{ 0x008D, "M Rebuild Failed for Unknown Reasons" },
	{ 0x008E, "M Rebuild Failed due to New Physical Device" },
	{ 0x008F, "M Rebuild Failed due to Logical Drive Failure" },
	{ 0x0090, "M Initialization Started" },
	{ 0x0091, "M Initialization Completed" },
	{ 0x0092, "M Initialization Cancelled" },
	{ 0x0093, "M Initialization Failed" },
	{ 0x0094, "L Found" },
	{ 0x0095, "L Deleted" },
	{ 0x0096, "M Expand Capacity Started" },
	{ 0x0097, "M Expand Capacity Completed" },
	{ 0x0098, "M Expand Capacity Failed" },
	{ 0x0099, "L Bad Block Found" },
	{ 0x009A, "L Size Changed" },
	{ 0x009B, "L Type Changed" },
	{ 0x009C, "L Bad Data Block Found" },
	{ 0x009E, "L Read of Data Block in BDT" },
	{ 0x009F, "L Write Back Data for Disk Block Lost" },
	{ 0x00A0, "L Temporarily Offline RAID-5/3 Drive Made Online" },
	{ 0x00A1, "L Temporarily Offline RAID-6/1/0/7 Drive Made Online" },
	{ 0x00A2, "L Standby Rebuild Started" },
	/* Fault Management Events (0x0100 - 0x017F) */
	{ 0x0140, "E Fan %d Failed" },
	{ 0x0141, "E Fan %d OK" },
	{ 0x0142, "E Fan %d Not Present" },
	{ 0x0143, "E Power Supply %d Failed" },
	{ 0x0144, "E Power Supply %d OK" },
	{ 0x0145, "E Power Supply %d Not Present" },
	{ 0x0146, "E Temperature Sensor %d Temperature Exceeds Safe Limit" },
	{ 0x0147, "E Temperature Sensor %d Temperature Exceeds Working Limit" },
	{ 0x0148, "E Temperature Sensor %d Temperature Normal" },
	{ 0x0149, "E Temperature Sensor %d Not Present" },
	{ 0x014A, "E Enclosure Management Unit %d Access Critical" },
	{ 0x014B, "E Enclosure Management Unit %d Access OK" },
	{ 0x014C, "E Enclosure Management Unit %d Access Offline" },
	/* Controller Events (0x0180 - 0x01FF) */
	{ 0x0181, "C Cache Write Back Error" },
	{ 0x0188, "C Battery Backup Unit Found" },
	{ 0x0189, "C Battery Backup Unit Charge Level Low" },
	{ 0x018A, "C Battery Backup Unit Charge Level OK" },
	{ 0x0193, "C Installation Aborted" },
	{ 0x0195, "C Battery Backup Unit Physically Removed" },
	{ 0x0196, "C Memory Error During Warm Boot" },
	{ 0x019E, "C Memory Soft ECC Error Corrected" },
	{ 0x019F, "C Memory Hard ECC Error Corrected" },
	{ 0x01A2, "C Battery Backup Unit Failed" },
	{ 0x01AB, "C Mirror Race Recovery Failed" },
	{ 0x01AC, "C Mirror Race on Critical Drive" },
	/* Controller Internal Processor Events */
	{ 0x0380, "C Internal Controller Hung" },
	{ 0x0381, "C Internal Controller Firmware Breakpoint" },
	{ 0x0390, "C Internal Controller i960 Processor Specific Error" },
	{ 0x03A0, "C Internal Controller StrongARM Processor Specific Error" },
	{ 0, "" }
};

static void myrs_log_event(myrs_hba *cs, myrs_event *ev)
{
	unsigned char msg_buf[MYRS_LINE_BUFFER_SIZE];
	int ev_idx = 0, ev_code;
	unsigned char ev_type, *ev_msg;
	struct Scsi_Host *shost = cs->host;
	struct scsi_device *sdev;
	struct scsi_sense_hdr sshdr;
	unsigned char *sense_info;
	unsigned char *cmd_specific;

	if (ev->ev_code == 0x1C) {
		if (!scsi_normalize_sense(ev->sense_data, 40, &sshdr))
			memset(&sshdr, 0x0, sizeof(sshdr));
		else {
			sense_info = &ev->sense_data[3];
			cmd_specific = &ev->sense_data[7];
		}
	}
	if (sshdr.sense_key == VENDOR_SPECIFIC &&
	    (sshdr.asc == 0x80 || sshdr.asc == 0x81))
		ev->ev_code = ((sshdr.asc - 0x80) << 8 || sshdr.ascq);
	while (true) {
		ev_code = myrs_ev_list[ev_idx].ev_code;
		if (ev_code == ev->ev_code || ev_code == 0)
			break;
		ev_idx++;
	}
	ev_type = myrs_ev_list[ev_idx].ev_msg[0];
	ev_msg = &myrs_ev_list[ev_idx].ev_msg[2];
	if (ev_code == 0) {
		shost_printk(KERN_WARNING, shost,
			     "Unknown Controller Event Code %04X\n",
			     ev->ev_code);
		return;
	}
	switch (ev_type) {
	case 'P':
		sdev = scsi_device_lookup(shost, ev->channel,
					  ev->target, 0);
		sdev_printk(KERN_INFO, sdev, "event %d: Physical Device %s\n",
			    ev->ev_seq, ev_msg);
		if (sdev && sdev->hostdata &&
		    sdev->channel < cs->ctlr_info->physchan_present) {
			myrs_pdev_info *pdev_info = sdev->hostdata;
			switch (ev->ev_code) {
			case 0x0001:
			case 0x0007:
				pdev_info->State = DAC960_V2_Device_Online;
				break;
			case 0x0002:
				pdev_info->State = DAC960_V2_Device_Standby;
				break;
			case 0x000C:
				pdev_info->State = DAC960_V2_Device_Offline;
				break;
			case 0x000E:
				pdev_info->State = DAC960_V2_Device_Missing;
				break;
			case 0x000F:
				pdev_info->State =
					DAC960_V2_Device_Unconfigured;
				break;
			}
		}
		break;
	case 'L':
		shost_printk(KERN_INFO, shost,
			     "event %d: Logical Drive %d %s\n",
			     ev->ev_seq, ev->lun, ev_msg);
		cs->needs_update = true;
		break;
	case 'M':
		shost_printk(KERN_INFO, shost,
			     "event %d: Logical Drive %d %s\n",
			     ev->ev_seq, ev->lun, ev_msg);
		cs->needs_update = true;
		break;
	case 'S':
		if (sshdr.sense_key == NO_SENSE ||
		    (sshdr.sense_key == NOT_READY &&
		     sshdr.asc == 0x04 && (sshdr.ascq == 0x01 ||
					    sshdr.ascq == 0x02)))
			break;
		shost_printk(KERN_INFO, shost,
			     "event %d: Physical Device %d:%d %s\n",
			     ev->ev_seq, ev->channel, ev->target, ev_msg);
		shost_printk(KERN_INFO, shost,
			     "Physical Device %d:%d Request Sense: "
			     "Sense Key = %X, ASC = %02X, ASCQ = %02X\n",
			     ev->channel, ev->target,
			     sshdr.sense_key, sshdr.asc, sshdr.ascq);
		shost_printk(KERN_INFO, shost,
			     "Physical Device %d:%d Request Sense: "
			     "Information = %02X%02X%02X%02X "
			     "%02X%02X%02X%02X\n",
			     ev->channel, ev->target,
			     sense_info[0], sense_info[1],
			     sense_info[2], sense_info[3],
			     cmd_specific[0], cmd_specific[1],
			     cmd_specific[2], cmd_specific[3]);
		break;
	case 'E':
		if (cs->disable_enc_msg)
			break;
		sprintf(msg_buf, ev_msg, ev->lun);
		shost_printk(KERN_INFO, shost, "event %d: Enclosure %d %s\n",
			     ev->ev_seq, ev->target, msg_buf);
		break;
	case 'C':
		shost_printk(KERN_INFO, shost, "event %d: Controller %s\n",
			     ev->ev_seq, ev_msg);
		break;
	default:
		shost_printk(KERN_INFO, shost,
			     "event %d: Unknown Event Code %04X\n",
			     ev->ev_seq, ev->ev_code);
		break;
	}
}

/*
 * SCSI sysfs interface functions
 */
static ssize_t myrs_show_dev_state(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	int ret;

	if (!sdev->hostdata)
		return snprintf(buf, 16, "Unknown\n");

	if (sdev->channel >= cs->ctlr_info->physchan_present) {
		myrs_ldev_info *ldev_info = sdev->hostdata;
		const char *name;

		name = myrs_devstate_name(ldev_info->State);
		if (name)
			ret = snprintf(buf, 32, "%s\n", name);
		else
			ret = snprintf(buf, 32, "Invalid (%02X)\n",
				       ldev_info->State);
	} else {
		myrs_pdev_info *pdev_info;
		const char *name;

		pdev_info = sdev->hostdata;
		name = myrs_devstate_name(pdev_info->State);
		if (name)
			ret = snprintf(buf, 32, "%s\n", name);
		else
			ret = snprintf(buf, 32, "Invalid (%02X)\n",
				       pdev_info->State);
	}
	return ret;
}

static ssize_t myrs_store_dev_state(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	myrs_cmdblk *cmd_blk;
	myrs_cmd_mbox *mbox;
	myrs_devstate new_state;
	unsigned short ldev_num;
	unsigned char status;

	if (!strncmp(buf, "offline", 7) ||
	    !strncmp(buf, "kill", 4))
		new_state = DAC960_V2_Device_Offline;
	else if (!strncmp(buf, "online", 6))
		new_state = DAC960_V2_Device_Online;
	else if (!strncmp(buf, "standby", 7))
		new_state = DAC960_V2_Device_Standby;
	else
		return -EINVAL;

	if (sdev->channel < cs->ctlr_info->physchan_present) {
		myrs_pdev_info *pdev_info = sdev->hostdata;
		myrs_devmap *pdev_devmap = (myrs_devmap *)&pdev_info->rsvd13;

		if (pdev_info->State == new_state) {
			sdev_printk(KERN_INFO, sdev,
				    "Device already in %s\n",
				    myrs_devstate_name(new_state));
			return count;
		}
		status = myrs_translate_pdev(cs, sdev->channel, sdev->id,
					     sdev->lun, pdev_devmap);
		if (status != DAC960_V2_NormalCompletion)
			return -ENXIO;
		ldev_num = pdev_devmap->ldev_num;
	} else {
		myrs_ldev_info *ldev_info = sdev->hostdata;

		if (ldev_info->State == new_state) {
			sdev_printk(KERN_INFO, sdev,
				    "Device already in %s\n",
				    myrs_devstate_name(new_state));
			return count;
		}
		ldev_num = ldev_info->ldev_num;
	}
	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	myrs_reset_cmd(cmd_blk);
	mbox = &cmd_blk->mbox;
	mbox->Common.opcode = DAC960_V2_IOCTL;
	mbox->Common.id = MYRS_DCMD_TAG;
	mbox->Common.control.DataTransferControllerToHost = true;
	mbox->Common.control.NoAutoRequestSense = true;
	mbox->SetDeviceState.ioctl_opcode = DAC960_V2_SetDeviceState;
	mbox->SetDeviceState.state = new_state;
	mbox->SetDeviceState.ldev.ldev_num = ldev_num;
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	if (status == DAC960_V2_NormalCompletion) {
		if (sdev->channel < cs->ctlr_info->physchan_present) {
			myrs_pdev_info *pdev_info = sdev->hostdata;

			pdev_info->State = new_state;
		} else {
			myrs_ldev_info *ldev_info = sdev->hostdata;

			ldev_info->State = new_state;
		}
		sdev_printk(KERN_INFO, sdev,
			    "Set device state to %s\n",
			    myrs_devstate_name(new_state));
		return count;
	}
	sdev_printk(KERN_INFO, sdev,
		    "Failed to set device state to %s, status 0x%02x\n",
		    myrs_devstate_name(new_state), status);
	return -EINVAL;
}

static DEVICE_ATTR(raid_state, S_IRUGO | S_IWUSR, myrs_show_dev_state,
		   myrs_store_dev_state);

static ssize_t myrs_show_dev_level(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	const char *name = NULL;

	if (!sdev->hostdata)
		return snprintf(buf, 16, "Unknown\n");

	if (sdev->channel >= cs->ctlr_info->physchan_present) {
		myrs_ldev_info *ldev_info;

		ldev_info = sdev->hostdata;
		name = myrs_raid_level_name(ldev_info->RAIDLevel);
		if (!name)
			return snprintf(buf, 32, "Invalid (%02X)\n",
					ldev_info->State);

	} else
		name = myrs_raid_level_name(DAC960_V2_RAID_Physical);

	return snprintf(buf, 32, "%s\n", name);
}
static DEVICE_ATTR(raid_level, S_IRUGO, myrs_show_dev_level, NULL);

static ssize_t myrs_show_dev_rebuild(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	myrs_ldev_info *ldev_info;
	unsigned short ldev_num;
	unsigned char status;

	if (sdev->channel < cs->ctlr_info->physchan_present)
		return snprintf(buf, 32, "physical device - not rebuilding\n");

	ldev_info = sdev->hostdata;
	ldev_num = ldev_info->ldev_num;
	status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
	if (status != DAC960_V2_NormalCompletion) {
		sdev_printk(KERN_INFO, sdev,
			    "Failed to get device information, status 0x%02x\n",
			    status);
		return -EIO;
	}
	if (ldev_info->rbld_active) {
		return snprintf(buf, 32, "rebuilding block %zu of %zu\n",
				(size_t)ldev_info->rbld_lba,
				(size_t)ldev_info->cfg_devsize);
	} else
		return snprintf(buf, 32, "not rebuilding\n");
}

static ssize_t myrs_store_dev_rebuild(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	myrs_ldev_info *ldev_info;
	myrs_cmdblk *cmd_blk;
	myrs_cmd_mbox *mbox;
	char tmpbuf[8];
	ssize_t len;
	unsigned short ldev_num;
	unsigned char status;
	int rebuild;
	int ret = count;

	if (sdev->channel < cs->ctlr_info->physchan_present)
		return -EINVAL;

	ldev_info = sdev->hostdata;
	if (!ldev_info)
		return -ENXIO;
	ldev_num = ldev_info->ldev_num;

	len = count > sizeof(tmpbuf) - 1 ? sizeof(tmpbuf) - 1 : count;
	strncpy(tmpbuf, buf, len);
	tmpbuf[len] = '\0';
	if (sscanf(tmpbuf, "%d", &rebuild) != 1)
		return -EINVAL;

	status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
	if (status != DAC960_V2_NormalCompletion) {
		sdev_printk(KERN_INFO, sdev,
			    "Failed to get device information, status 0x%02x\n",
			    status);
		return -EIO;
	}

	if (rebuild && ldev_info->rbld_active) {
		sdev_printk(KERN_INFO, sdev,
			    "Rebuild Not Initiated; already in progress\n");
		return -EALREADY;
	}
	if (!rebuild && !ldev_info->rbld_active) {
		sdev_printk(KERN_INFO, sdev,
			    "Rebuild Not Cancelled; no rebuild in progress\n");
		return ret;
	}

	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	myrs_reset_cmd(cmd_blk);
	mbox = &cmd_blk->mbox;
	mbox->Common.opcode = DAC960_V2_IOCTL;
	mbox->Common.id = MYRS_DCMD_TAG;
	mbox->Common.control.DataTransferControllerToHost = true;
	mbox->Common.control.NoAutoRequestSense = true;
	if (rebuild) {
		mbox->LogicalDeviceInfo.ldev.ldev_num = ldev_num;
		mbox->LogicalDeviceInfo.ioctl_opcode =
			DAC960_V2_RebuildDeviceStart;
	} else {
		mbox->LogicalDeviceInfo.ldev.ldev_num = ldev_num;
		mbox->LogicalDeviceInfo.ioctl_opcode =
			DAC960_V2_RebuildDeviceStop;
	}
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	if (status) {
		sdev_printk(KERN_INFO, sdev,
			    "Rebuild Not %s, status 0x%02x\n",
			    rebuild ? "Initiated" : "Cancelled", status);
		ret = -EIO;
	} else
		sdev_printk(KERN_INFO, sdev, "Rebuild %s\n",
			    rebuild ? "Initiated" : "Cancelled");

	return ret;
}
static DEVICE_ATTR(rebuild, S_IRUGO | S_IWUSR, myrs_show_dev_rebuild,
		   myrs_store_dev_rebuild);


static ssize_t myrs_show_consistency_check(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	myrs_ldev_info *ldev_info;
	unsigned short ldev_num;
	unsigned char status;

	if (sdev->channel < cs->ctlr_info->physchan_present)
		return snprintf(buf, 32, "physical device - not checking\n");

	ldev_info = sdev->hostdata;
	if (!ldev_info)
		return -ENXIO;
	ldev_num = ldev_info->ldev_num;
	status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
	if (ldev_info->cc_active)
		return snprintf(buf, 32, "checking block %zu of %zu\n",
				(size_t)ldev_info->cc_lba,
				(size_t)ldev_info->cfg_devsize);
	else
		return snprintf(buf, 32, "not checking\n");
}

static ssize_t myrs_store_consistency_check(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	myrs_ldev_info *ldev_info;
	myrs_cmdblk *cmd_blk;
	myrs_cmd_mbox *mbox;
	char tmpbuf[8];
	ssize_t len;
	unsigned short ldev_num;
	unsigned char status;
	int check;
	int ret = count;

	if (sdev->channel < cs->ctlr_info->physchan_present)
		return -EINVAL;

	ldev_info = sdev->hostdata;
	if (!ldev_info)
		return -ENXIO;
	ldev_num = ldev_info->ldev_num;

	len = count > sizeof(tmpbuf) - 1 ? sizeof(tmpbuf) - 1 : count;
	strncpy(tmpbuf, buf, len);
	tmpbuf[len] = '\0';
	if (sscanf(tmpbuf, "%d", &check) != 1)
		return -EINVAL;

	status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
	if (status != DAC960_V2_NormalCompletion) {
		sdev_printk(KERN_INFO, sdev,
			    "Failed to get device information, status 0x%02x\n",
			    status);
		return -EIO;
	}
	if (check && ldev_info->cc_active) {
		sdev_printk(KERN_INFO, sdev,
			    "Consistency Check Not Initiated; "
			    "already in progress\n");
		return -EALREADY;
	}
	if (!check && !ldev_info->cc_active) {
		sdev_printk(KERN_INFO, sdev,
			    "Consistency Check Not Cancelled; "
			    "check not in progress\n");
		return ret;
	}

	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	myrs_reset_cmd(cmd_blk);
	mbox = &cmd_blk->mbox;
	mbox->Common.opcode = DAC960_V2_IOCTL;
	mbox->Common.id = MYRS_DCMD_TAG;
	mbox->Common.control.DataTransferControllerToHost = true;
	mbox->Common.control.NoAutoRequestSense = true;
	if (check) {
		mbox->ConsistencyCheck.ldev.ldev_num = ldev_num;
		mbox->ConsistencyCheck.ioctl_opcode =
			DAC960_V2_ConsistencyCheckStart;
		mbox->ConsistencyCheck.RestoreConsistency = true;
		mbox->ConsistencyCheck.InitializedAreaOnly = false;
	} else {
		mbox->ConsistencyCheck.ldev.ldev_num = ldev_num;
		mbox->ConsistencyCheck.ioctl_opcode =
			DAC960_V2_ConsistencyCheckStop;
	}
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	if (status != DAC960_V2_NormalCompletion) {
		sdev_printk(KERN_INFO, sdev,
			    "Consistency Check Not %s, status 0x%02x\n",
			    check ? "Initiated" : "Cancelled", status);
		ret = -EIO;
	} else
		sdev_printk(KERN_INFO, sdev, "Consistency Check %s\n",
			    check ? "Initiated" : "Cancelled");

	return ret;
}
static DEVICE_ATTR(consistency_check, S_IRUGO | S_IWUSR,
		   myrs_show_consistency_check,
		   myrs_store_consistency_check);

static struct device_attribute *myrs_sdev_attrs[] = {
	&dev_attr_consistency_check,
	&dev_attr_rebuild,
	&dev_attr_raid_state,
	&dev_attr_raid_level,
	NULL,
};

static ssize_t myrs_show_ctlr_serial(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;
	char serial[17];

	memcpy(serial, cs->ctlr_info->ControllerSerialNumber, 16);
	serial[16] = '\0';
	return snprintf(buf, 16, "%s\n", serial);
}
static DEVICE_ATTR(serial, S_IRUGO, myrs_show_ctlr_serial, NULL);

static ssize_t myrs_show_ctlr_num(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;

	return snprintf(buf, 20, "%d\n", cs->host->host_no);
}
static DEVICE_ATTR(ctlr_num, S_IRUGO, myrs_show_ctlr_num, NULL);

static struct myrs_cpu_type_tbl {
	myrs_cpu_type type;
	char *name;
} myrs_cpu_type_names[] = {
	{ DAC960_V2_ProcessorType_i960CA, "i960CA" },
	{ DAC960_V2_ProcessorType_i960RD, "i960RD" },
	{ DAC960_V2_ProcessorType_i960RN, "i960RN" },
	{ DAC960_V2_ProcessorType_i960RP, "i960RP" },
	{ DAC960_V2_ProcessorType_NorthBay, "NorthBay" },
	{ DAC960_V2_ProcessorType_StrongArm, "StrongARM" },
	{ DAC960_V2_ProcessorType_i960RM, "i960RM" },
	{ 0xff, NULL },
};

static ssize_t myrs_show_processor(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;
	struct myrs_cpu_type_tbl *tbl = myrs_cpu_type_names;
	const char *first_processor = NULL;
	const char *second_processor = NULL;
	myrs_ctlr_info *info = cs->ctlr_info;
	ssize_t ret;

	if (info->FirstProcessorCount) {
		while (tbl && tbl->name) {
			if (tbl->type == info->FirstProcessorType) {
				first_processor = tbl->name;
				break;
			}
			tbl++;
		}
	}
	if (info->SecondProcessorCount) {
		tbl = myrs_cpu_type_names;
		while (tbl && tbl->name) {
			if (tbl->type == info->SecondProcessorType) {
				second_processor = tbl->name;
				break;
			}
			tbl++;
		}
	}
	if (first_processor && second_processor)
		ret = snprintf(buf, 64, "1: %s (%s, %d cpus)\n"
			       "2: %s (%s, %d cpus)\n",
			       info->FirstProcessorName,
			       first_processor, info->FirstProcessorCount,
			       info->SecondProcessorName,
			       second_processor, info->SecondProcessorCount);
	else if (!second_processor)
		ret = snprintf(buf, 64, "1: %s (%s, %d cpus)\n2: absent\n",
			       info->FirstProcessorName,
			       first_processor, info->FirstProcessorCount );
	else if (!first_processor)
		ret = snprintf(buf, 64, "1: absent\n2: %s (%s, %d cpus)\n",
			       info->SecondProcessorName,
			       second_processor, info->SecondProcessorCount);
	else
		ret = snprintf(buf, 64, "1: absent\n2: absent\n");

	return ret;
}
static DEVICE_ATTR(processor, S_IRUGO, myrs_show_processor, NULL);

static ssize_t myrs_show_model_name(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;

	return snprintf(buf, 28, "%s\n", cs->model_name);
}
static DEVICE_ATTR(model, S_IRUGO, myrs_show_model_name, NULL);

static ssize_t myrs_show_ctlr_type(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;

	return snprintf(buf, 4, "%d\n", cs->ctlr_info->ControllerType);
}
static DEVICE_ATTR(ctlr_type, S_IRUGO, myrs_show_ctlr_type, NULL);

static ssize_t myrs_show_cache_size(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;

	return snprintf(buf, 8, "%d MB\n", cs->ctlr_info->CacheSizeMB);
}
static DEVICE_ATTR(cache_size, S_IRUGO, myrs_show_cache_size, NULL);

static ssize_t myrs_show_firmware_version(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;

	return snprintf(buf, 16, "%d.%02d-%02d\n",
			cs->ctlr_info->FirmwareMajorVersion,
			cs->ctlr_info->FirmwareMinorVersion,
			cs->ctlr_info->FirmwareTurnNumber);
}
static DEVICE_ATTR(firmware, S_IRUGO, myrs_show_firmware_version, NULL);

static ssize_t myrs_store_discovery_command(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;
	myrs_cmdblk *cmd_blk;
	myrs_cmd_mbox *mbox;
	unsigned char status;

	mutex_lock(&cs->dcmd_mutex);
	cmd_blk = &cs->dcmd_blk;
	myrs_reset_cmd(cmd_blk);
	mbox = &cmd_blk->mbox;
	mbox->Common.opcode = DAC960_V2_IOCTL;
	mbox->Common.id = MYRS_DCMD_TAG;
	mbox->Common.control.DataTransferControllerToHost = true;
	mbox->Common.control.NoAutoRequestSense = true;
	mbox->Common.ioctl_opcode = DAC960_V2_StartDiscovery;
	myrs_exec_cmd(cs, cmd_blk);
	status = cmd_blk->status;
	mutex_unlock(&cs->dcmd_mutex);
	if (status != DAC960_V2_NormalCompletion) {
		shost_printk(KERN_INFO, shost,
			     "Discovery Not Initiated, status %02X\n",
			     status);
		return -EINVAL;
	}
	shost_printk(KERN_INFO, shost, "Discovery Initiated\n");
	cs->next_evseq = 0;
	cs->needs_update = true;
	queue_delayed_work(cs->work_q, &cs->monitor_work, 1);
	flush_delayed_work(&cs->monitor_work);
	shost_printk(KERN_INFO, shost, "Discovery Completed\n");

	return count;
}
static DEVICE_ATTR(discovery, S_IWUSR, NULL, myrs_store_discovery_command);

static ssize_t myrs_store_flush_cache(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;
	unsigned char status;

	status = myrs_dev_op(cs, DAC960_V2_FlushDeviceData,
			     DAC960_V2_RAID_Controller);
	if (status == DAC960_V2_NormalCompletion) {
		shost_printk(KERN_INFO, shost, "Cache Flush Completed\n");
		return count;
	}
	shost_printk(KERN_INFO, shost,
		     "Cashe Flush failed, status 0x%02x\n", status);
	return -EIO;
}
static DEVICE_ATTR(flush_cache, S_IWUSR, NULL, myrs_store_flush_cache);

static ssize_t myrs_show_suppress_enclosure_messages(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	myrs_hba *cs = (myrs_hba *)shost->hostdata;

	return snprintf(buf, 3, "%d\n", cs->disable_enc_msg);
}

static ssize_t myrs_store_suppress_enclosure_messages(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	char tmpbuf[8];
	ssize_t len;
	int value;

	len = count > sizeof(tmpbuf) - 1 ? sizeof(tmpbuf) - 1 : count;
	strncpy(tmpbuf, buf, len);
	tmpbuf[len] = '\0';
	if (sscanf(tmpbuf, "%d", &value) != 1 || value > 2)
		return -EINVAL;

	cs->disable_enc_msg = value;
	return count;
}
static DEVICE_ATTR(disable_enclosure_messages, S_IRUGO | S_IWUSR,
		   myrs_show_suppress_enclosure_messages,
		   myrs_store_suppress_enclosure_messages);

static struct device_attribute *myrs_shost_attrs[] = {
	&dev_attr_serial,
	&dev_attr_ctlr_num,
	&dev_attr_processor,
	&dev_attr_model,
	&dev_attr_ctlr_type,
	&dev_attr_cache_size,
	&dev_attr_firmware,
	&dev_attr_discovery,
	&dev_attr_flush_cache,
	&dev_attr_disable_enclosure_messages,
	NULL,
};

/*
 * SCSI midlayer interface
 */
int myrs_host_reset(struct scsi_cmnd *scmd)
{
	struct Scsi_Host *shost = scmd->device->host;
	myrs_hba *cs = (myrs_hba *)shost->hostdata;

	cs->reset(cs->io_base);
	return SUCCESS;
}

static void
myrs_mode_sense(myrs_hba *cs, struct scsi_cmnd *scmd,
		myrs_ldev_info *ldev_info)
{
	unsigned char modes[32], *mode_pg;
	bool dbd;
	size_t mode_len;

	dbd = (scmd->cmnd[1] & 0x08) == 0x08;
	if (dbd) {
		mode_len = 24;
		mode_pg = &modes[4];
	} else {
		mode_len = 32;
		mode_pg = &modes[12];
	}
	memset(modes, 0, sizeof(modes));
	modes[0] = mode_len - 1;
	modes[2] = 0x10; /* Enable FUA */
	if (ldev_info->ldev_control.WriteCache ==
	    DAC960_V2_LogicalDeviceReadOnly)
		modes[2] |= 0x80;
	if (!dbd) {
		unsigned char *block_desc = &modes[4];
		modes[3] = 8;
		put_unaligned_be32(ldev_info->cfg_devsize, &block_desc[0]);
		put_unaligned_be32(ldev_info->DeviceBlockSizeInBytes,
				   &block_desc[5]);
	}
	mode_pg[0] = 0x08;
	mode_pg[1] = 0x12;
	if (ldev_info->ldev_control.ReadCache == DAC960_V2_ReadCacheDisabled)
		mode_pg[2] |= 0x01;
	if (ldev_info->ldev_control.WriteCache == DAC960_V2_WriteCacheEnabled ||
	    ldev_info->ldev_control.WriteCache ==
	    DAC960_V2_IntelligentWriteCacheEnabled)
		mode_pg[2] |= 0x04;
	if (ldev_info->CacheLineSize) {
		mode_pg[2] |= 0x08;
		put_unaligned_be16(1 << ldev_info->CacheLineSize, &mode_pg[14]);
	}

	scsi_sg_copy_from_buffer(scmd, modes, mode_len);
}

static int myrs_queuecommand(struct Scsi_Host *shost,
			     struct scsi_cmnd *scmd)
{
	myrs_hba *cs = (myrs_hba *)shost->hostdata;
	myrs_cmdblk *cmd_blk = scsi_cmd_priv(scmd);
	myrs_cmd_mbox *mbox = &cmd_blk->mbox;
	struct scsi_device *sdev = scmd->device;
	myrs_sgl *hw_sge;
	dma_addr_t sense_addr;
	struct scatterlist *sgl;
	unsigned long flags, timeout;
	int nsge;

	if (!scmd->device->hostdata) {
		scmd->result = (DID_NO_CONNECT << 16);
		scmd->scsi_done(scmd);
		return 0;
	}

	switch (scmd->cmnd[0]) {
	case REPORT_LUNS:
		scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
					0x20, 0x0);
		scmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;
		scmd->scsi_done(scmd);
		return 0;
	case MODE_SENSE:
		if (scmd->device->channel >= cs->ctlr_info->physchan_present) {
			myrs_ldev_info *ldev_info = sdev->hostdata;
			if ((scmd->cmnd[2] & 0x3F) != 0x3F &&
			    (scmd->cmnd[2] & 0x3F) != 0x08) {
				/* Illegal request, invalid field in CDB */
				scsi_build_sense_buffer(0, scmd->sense_buffer,
							ILLEGAL_REQUEST, 0x24, 0);
				scmd->result = (DRIVER_SENSE << 24) |
					SAM_STAT_CHECK_CONDITION;
			} else {
				myrs_mode_sense(cs, scmd, ldev_info);
				scmd->result = (DID_OK << 16);
			}
			scmd->scsi_done(scmd);
			return 0;
		}
		break;
	}

	myrs_reset_cmd(cmd_blk);
	cmd_blk->sense = dma_pool_alloc(cs->sense_pool, GFP_ATOMIC,
					&sense_addr);
	if (!cmd_blk->sense)
		return SCSI_MLQUEUE_HOST_BUSY;
	cmd_blk->sense_addr = sense_addr;

	timeout = scmd->request->timeout;
	if (scmd->cmd_len <= 10) {
		if (scmd->device->channel >= cs->ctlr_info->physchan_present) {
			myrs_ldev_info *ldev_info = sdev->hostdata;

			mbox->SCSI_10.opcode = DAC960_V2_SCSI_10;
			mbox->SCSI_10.pdev.LogicalUnit = ldev_info->LogicalUnit;
			mbox->SCSI_10.pdev.TargetID = ldev_info->TargetID;
			mbox->SCSI_10.pdev.Channel = ldev_info->Channel;
			mbox->SCSI_10.pdev.Controller = 0;
		} else {
			mbox->SCSI_10.opcode = DAC960_V2_SCSI_10_Passthru;
			mbox->SCSI_10.pdev.LogicalUnit = sdev->lun;
			mbox->SCSI_10.pdev.TargetID = sdev->id;
			mbox->SCSI_10.pdev.Channel = sdev->channel;
		}
		mbox->SCSI_10.id = scmd->request->tag + 3;
		mbox->SCSI_10.control.DataTransferControllerToHost =
			(scmd->sc_data_direction == DMA_FROM_DEVICE);
		if (scmd->request->cmd_flags & REQ_FUA)
			mbox->SCSI_10.control.ForceUnitAccess = true;
		mbox->SCSI_10.dma_size = scsi_bufflen(scmd);
		mbox->SCSI_10.sense_addr = cmd_blk->sense_addr;
		mbox->SCSI_10.sense_len = MYRS_SENSE_SIZE;
		mbox->SCSI_10.cdb_len = scmd->cmd_len;
		if (timeout > 60) {
			mbox->SCSI_10.tmo.TimeoutScale =
				DAC960_V2_TimeoutScale_Minutes;
			mbox->SCSI_10.tmo.TimeoutValue = timeout / 60;
		} else {
			mbox->SCSI_10.tmo.TimeoutScale =
				DAC960_V2_TimeoutScale_Seconds;
			mbox->SCSI_10.tmo.TimeoutValue = timeout;
		}
		memcpy(&mbox->SCSI_10.cdb, scmd->cmnd, scmd->cmd_len);
		hw_sge = &mbox->SCSI_10.dma_addr;
		cmd_blk->DCDB = NULL;
	} else {
		dma_addr_t DCDB_dma;

		cmd_blk->DCDB = dma_pool_alloc(cs->dcdb_pool, GFP_ATOMIC,
					       &DCDB_dma);
		if (!cmd_blk->DCDB) {
			dma_pool_free(cs->sense_pool, cmd_blk->sense,
				      cmd_blk->sense_addr);
			cmd_blk->sense = NULL;
			cmd_blk->sense_addr = 0;
			return SCSI_MLQUEUE_HOST_BUSY;
		}
		cmd_blk->DCDB_dma = DCDB_dma;
		if (scmd->device->channel >= cs->ctlr_info->physchan_present) {
			myrs_ldev_info *ldev_info = sdev->hostdata;

			mbox->SCSI_255.opcode = DAC960_V2_SCSI_256;
			mbox->SCSI_255.pdev.LogicalUnit =
				ldev_info->LogicalUnit;
			mbox->SCSI_255.pdev.TargetID = ldev_info->TargetID;
			mbox->SCSI_255.pdev.Channel = ldev_info->Channel;
			mbox->SCSI_255.pdev.Controller = 0;
		} else {
			mbox->SCSI_255.opcode =
				DAC960_V2_SCSI_255_Passthru;
			mbox->SCSI_255.pdev.LogicalUnit = sdev->lun;
			mbox->SCSI_255.pdev.TargetID = sdev->id;
			mbox->SCSI_255.pdev.Channel = sdev->channel;
		}
		mbox->SCSI_255.id = scmd->request->tag + 3;
		mbox->SCSI_255.control.DataTransferControllerToHost =
			(scmd->sc_data_direction == DMA_FROM_DEVICE);
		if (scmd->request->cmd_flags & REQ_FUA)
			mbox->SCSI_255.control.ForceUnitAccess = true;
		mbox->SCSI_255.dma_size = scsi_bufflen(scmd);
		mbox->SCSI_255.sense_addr = cmd_blk->sense_addr;
		mbox->SCSI_255.sense_len = MYRS_SENSE_SIZE;
		mbox->SCSI_255.cdb_len = scmd->cmd_len;
		mbox->SCSI_255.cdb_addr = cmd_blk->DCDB_dma;
		if (timeout > 60) {
			mbox->SCSI_255.tmo.TimeoutScale =
				DAC960_V2_TimeoutScale_Minutes;
			mbox->SCSI_255.tmo.TimeoutValue = timeout / 60;
		} else {
			mbox->SCSI_255.tmo.TimeoutScale =
				DAC960_V2_TimeoutScale_Seconds;
			mbox->SCSI_255.tmo.TimeoutValue = timeout;
		}
		memcpy(cmd_blk->DCDB, scmd->cmnd, scmd->cmd_len);
		hw_sge = &mbox->SCSI_255.dma_addr;
	}
	if (scmd->sc_data_direction == DMA_NONE)
		goto submit;
	nsge = scsi_dma_map(scmd);
	if (nsge == 1) {
		sgl = scsi_sglist(scmd);
		hw_sge->sge[0].sge_addr = (u64)sg_dma_address(sgl);
		hw_sge->sge[0].sge_count = (u64)sg_dma_len(sgl);
	} else {
		myrs_sge *hw_sgl;
		dma_addr_t hw_sgl_addr;
		int i;

		if (nsge > 2) {
			hw_sgl = dma_pool_alloc(cs->sg_pool, GFP_ATOMIC,
						&hw_sgl_addr);
			if (WARN_ON(!hw_sgl)) {
				if (cmd_blk->DCDB) {
					dma_pool_free(cs->dcdb_pool,
						      cmd_blk->DCDB,
						      cmd_blk->DCDB_dma);
					cmd_blk->DCDB = NULL;
					cmd_blk->DCDB_dma = 0;
				}
				dma_pool_free(cs->sense_pool,
					      cmd_blk->sense,
					      cmd_blk->sense_addr);
				cmd_blk->sense = NULL;
				cmd_blk->sense_addr = 0;
				return SCSI_MLQUEUE_HOST_BUSY;
			}
			cmd_blk->sgl = hw_sgl;
			cmd_blk->sgl_addr = hw_sgl_addr;
			if (scmd->cmd_len <= 10)
				mbox->SCSI_10.control
					.AdditionalScatterGatherListMemory = true;
			else
				mbox->SCSI_255.control
					.AdditionalScatterGatherListMemory = true;
			hw_sge->ext.sge0_len = nsge;
			hw_sge->ext.sge0_addr = cmd_blk->sgl_addr;
		} else
			hw_sgl = hw_sge->sge;

		scsi_for_each_sg(scmd, sgl, nsge, i) {
			if (WARN_ON(!hw_sgl)) {
				scsi_dma_unmap(scmd);
				scmd->result = (DID_ERROR << 16);
				scmd->scsi_done(scmd);
				return 0;
			}
			hw_sgl->sge_addr = (u64)sg_dma_address(sgl);
			hw_sgl->sge_count = (u64)sg_dma_len(sgl);
			hw_sgl++;
		}
	}
submit:
	spin_lock_irqsave(&cs->queue_lock, flags);
	myrs_qcmd(cs, cmd_blk);
	spin_unlock_irqrestore(&cs->queue_lock, flags);

	return 0;
}

static unsigned short myrs_translate_ldev(myrs_hba *cs,
					  struct scsi_device *sdev)
{
	unsigned short ldev_num;
	unsigned int chan_offset =
		sdev->channel - cs->ctlr_info->physchan_present;

	ldev_num = sdev->id + chan_offset * sdev->host->max_id;

	return ldev_num;
}

static int myrs_slave_alloc(struct scsi_device *sdev)
{
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	unsigned char status;

	if (sdev->channel > sdev->host->max_channel)
		return 0;

	if (sdev->channel >= cs->ctlr_info->physchan_present) {
		myrs_ldev_info *ldev_info;
		unsigned short ldev_num;

		if (sdev->lun > 0)
			return -ENXIO;

		ldev_num = myrs_translate_ldev(cs, sdev);

		ldev_info = kzalloc(sizeof(*ldev_info), GFP_KERNEL|GFP_DMA);
		if (!ldev_info)
			return -ENOMEM;

		status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
		if (status != DAC960_V2_NormalCompletion) {
			sdev->hostdata = NULL;
			kfree(ldev_info);
		} else {
			enum raid_level level;

			dev_dbg(&sdev->sdev_gendev,
				"Logical device mapping %d:%d:%d -> %d\n",
				ldev_info->Channel, ldev_info->TargetID,
				ldev_info->LogicalUnit,
				ldev_info->ldev_num);

			sdev->hostdata = ldev_info;
			switch (ldev_info->RAIDLevel) {
			case DAC960_V2_RAID_Level0:
				level = RAID_LEVEL_LINEAR;
				break;
			case DAC960_V2_RAID_Level1:
				level = RAID_LEVEL_1;
				break;
			case DAC960_V2_RAID_Level3:
			case DAC960_V2_RAID_Level3F:
			case DAC960_V2_RAID_Level3L:
				level = RAID_LEVEL_3;
				break;
			case DAC960_V2_RAID_Level5:
			case DAC960_V2_RAID_Level5L:
				level = RAID_LEVEL_5;
				break;
			case DAC960_V2_RAID_Level6:
				level = RAID_LEVEL_6;
				break;
			case DAC960_V2_RAID_LevelE:
			case DAC960_V2_RAID_NewSpan:
			case DAC960_V2_RAID_Span:
				level = RAID_LEVEL_LINEAR;
				break;
			case DAC960_V2_RAID_JBOD:
				level = RAID_LEVEL_JBOD;
				break;
			default:
				level = RAID_LEVEL_UNKNOWN;
				break;
			}
			raid_set_level(myrs_raid_template,
				       &sdev->sdev_gendev, level);
			if (ldev_info->State != DAC960_V2_Device_Online) {
				const char *name;

				name = myrs_devstate_name(ldev_info->State);
				sdev_printk(KERN_DEBUG, sdev,
					    "logical device in state %s\n",
					    name ? name : "Invalid");
			}
		}
	} else {
		myrs_pdev_info *pdev_info;

		pdev_info = kzalloc(sizeof(*pdev_info), GFP_KERNEL|GFP_DMA);
		if (!pdev_info)
			return -ENOMEM;

		status = myrs_get_pdev_info(cs, sdev->channel,
					    sdev->id, sdev->lun,
					    pdev_info);
		if (status != DAC960_V2_NormalCompletion) {
			sdev->hostdata = NULL;
			kfree(pdev_info);
			return -ENXIO;
		}
		sdev->hostdata = pdev_info;
	}
	return 0;
}

static int myrs_slave_configure(struct scsi_device *sdev)
{
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	myrs_ldev_info *ldev_info;

	if (sdev->channel > sdev->host->max_channel)
		return -ENXIO;

	if (sdev->channel < cs->ctlr_info->physchan_present) {
		/* Skip HBA device */
		if (sdev->type == TYPE_RAID)
			return -ENXIO;
		sdev->no_uld_attach = 1;
		return 0;
	}
	if (sdev->lun != 0)
		return -ENXIO;

	ldev_info = sdev->hostdata;
	if (!ldev_info)
		return -ENXIO;
	if (ldev_info->ldev_control.WriteCache ==
	    DAC960_V2_WriteCacheEnabled ||
	    ldev_info->ldev_control.WriteCache ==
	    DAC960_V2_IntelligentWriteCacheEnabled)
		sdev->wce_default_on = 1;
	sdev->tagged_supported = 1;
	return 0;
}

static void myrs_slave_destroy(struct scsi_device *sdev)
{
	void *hostdata = sdev->hostdata;

	if (hostdata) {
		kfree(hostdata);
		sdev->hostdata = NULL;
	}
}

struct scsi_host_template myrs_template = {
	.module = THIS_MODULE,
	.name = "DAC960",
	.proc_name = "myrs",
	.queuecommand = myrs_queuecommand,
	.eh_host_reset_handler = myrs_host_reset,
	.slave_alloc = myrs_slave_alloc,
	.slave_configure = myrs_slave_configure,
	.slave_destroy = myrs_slave_destroy,
	.cmd_size = sizeof(myrs_cmdblk),
	.shost_attrs = myrs_shost_attrs,
	.sdev_attrs = myrs_sdev_attrs,
	.this_id = -1,
};

static myrs_hba *myrs_alloc_host(struct pci_dev *pdev,
				 const struct pci_device_id *entry)
{
	struct Scsi_Host *shost;
	myrs_hba *cs;

	shost = scsi_host_alloc(&myrs_template, sizeof(myrs_hba));
	if (!shost)
		return NULL;

	shost->max_cmd_len = 16;
	shost->max_lun = 256;
	cs = (myrs_hba *)shost->hostdata;
	mutex_init(&cs->dcmd_mutex);
	mutex_init(&cs->cinfo_mutex);
	cs->host = shost;

	return cs;
}

/*
 * RAID template functions
 */

/**
 * myrs_is_raid - return boolean indicating device is raid volume
 * @dev the device struct object
 */
static int
myrs_is_raid(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;

	return (sdev->channel >= cs->ctlr_info->physchan_present) ? 1 : 0;
}

/**
 * myrs_get_resync - get raid volume resync percent complete
 * @dev the device struct object
 */
static void
myrs_get_resync(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	myrs_ldev_info *ldev_info = sdev->hostdata;
	u8 percent_complete = 0, status;

	if (sdev->channel < cs->ctlr_info->physchan_present || !ldev_info)
		return;
	if (ldev_info->rbld_active) {
		unsigned short ldev_num = ldev_info->ldev_num;

		status = myrs_get_ldev_info(cs, ldev_num, ldev_info);
		percent_complete = ldev_info->rbld_lba * 100 /
			ldev_info->cfg_devsize;
	}
	raid_set_resync(myrs_raid_template, dev, percent_complete);
}

/**
 * myrs_get_state - get raid volume status
 * @dev the device struct object
 */
static void
myrs_get_state(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	myrs_hba *cs = (myrs_hba *)sdev->host->hostdata;
	myrs_ldev_info *ldev_info = sdev->hostdata;
	enum raid_state state = RAID_STATE_UNKNOWN;

	if (sdev->channel < cs->ctlr_info->physchan_present || !ldev_info)
		state = RAID_STATE_UNKNOWN;
	else {
		switch (ldev_info->State) {
		case DAC960_V2_Device_Online:
			state = RAID_STATE_ACTIVE;
			break;
		case DAC960_V2_Device_SuspectedCritical:
		case DAC960_V2_Device_Critical:
			state = RAID_STATE_DEGRADED;
			break;
		case DAC960_V2_Device_Rebuild:
			state = RAID_STATE_RESYNCING;
			break;
		case DAC960_V2_Device_Unconfigured:
		case DAC960_V2_Device_InvalidState:
			state = RAID_STATE_UNKNOWN;
			break;
		default:
			state = RAID_STATE_OFFLINE;
		}
	}
	raid_set_state(myrs_raid_template, dev, state);
}

struct raid_function_template myrs_raid_functions = {
	.cookie		= &myrs_template,
	.is_raid	= myrs_is_raid,
	.get_resync	= myrs_get_resync,
	.get_state	= myrs_get_state,
};

/*
 * PCI interface functions
 */

void myrs_flush_cache(myrs_hba *cs)
{
	myrs_dev_op(cs, DAC960_V2_FlushDeviceData, DAC960_V2_RAID_Controller);
}

static void myrs_handle_scsi(myrs_hba *cs, myrs_cmdblk *cmd_blk,
			     struct scsi_cmnd *scmd)
{
	unsigned char status;

	if (!cmd_blk)
		return;

	BUG_ON(!scmd);
	scsi_dma_unmap(scmd);

	if (cmd_blk->sense) {
		if (status == DAC960_V2_AbnormalCompletion &&
		    cmd_blk->sense_len) {
			unsigned int sense_len = SCSI_SENSE_BUFFERSIZE;

			if (sense_len > cmd_blk->sense_len)
				sense_len = cmd_blk->sense_len;
			memcpy(scmd->sense_buffer, cmd_blk->sense, sense_len);
		}
		dma_pool_free(cs->sense_pool, cmd_blk->sense,
			      cmd_blk->sense_addr);
		cmd_blk->sense = NULL;
		cmd_blk->sense_addr = 0;
	}
	if (cmd_blk->DCDB) {
		dma_pool_free(cs->dcdb_pool, cmd_blk->DCDB,
			      cmd_blk->DCDB_dma);
		cmd_blk->DCDB = NULL;
		cmd_blk->DCDB_dma = 0;
	}
	if (cmd_blk->sgl) {
		dma_pool_free(cs->sg_pool, cmd_blk->sgl,
			      cmd_blk->sgl_addr);
		cmd_blk->sgl = NULL;
		cmd_blk->sgl_addr = 0;
	}
	if (cmd_blk->residual)
		scsi_set_resid(scmd, cmd_blk->residual);
	status = cmd_blk->status;
	if (status == DAC960_V2_DeviceNonresponsive ||
	    status == DAC960_V2_DeviceNonresponsive2)
		scmd->result = (DID_BAD_TARGET << 16);
	else
		scmd->result = (DID_OK << 16) || status;
	scmd->scsi_done(scmd);
}

static void myrs_handle_cmdblk(myrs_hba *cs, myrs_cmdblk *cmd_blk)
{
	if (!cmd_blk)
		return;

	if (cmd_blk->Completion) {
		complete(cmd_blk->Completion);
		cmd_blk->Completion = NULL;
	}
}

static void myrs_monitor(struct work_struct *work)
{
	myrs_hba *cs = container_of(work, myrs_hba, monitor_work.work);
	struct Scsi_Host *shost = cs->host;
	myrs_ctlr_info *info = cs->ctlr_info;
	unsigned int epoch = cs->fwstat_buf->epoch;
	unsigned long interval = MYRS_PRIMARY_MONITOR_INTERVAL;
	unsigned char status;

	dev_dbg(&shost->shost_gendev, "monitor tick\n");

	status = myrs_get_fwstatus(cs);

	if (cs->needs_update) {
		cs->needs_update = false;
		mutex_lock(&cs->cinfo_mutex);
		status = myrs_get_ctlr_info(cs);
		mutex_unlock(&cs->cinfo_mutex);
	}
	if (cs->fwstat_buf->next_evseq - cs->next_evseq > 0) {
		status = myrs_get_event(cs, cs->next_evseq,
					cs->event_buf);
		if (status == DAC960_V2_NormalCompletion) {
			myrs_log_event(cs, cs->event_buf);
			cs->next_evseq++;
			interval = 1;
		}
	}

	if (time_after(jiffies, cs->secondary_monitor_time
		       + MYRS_SECONDARY_MONITOR_INTERVAL))
		cs->secondary_monitor_time = jiffies;

	if (info->bg_init_active +
	    info->ldev_init_active +
	    info->pdev_init_active +
	    info->cc_active +
	    info->rbld_active +
	    info->exp_active != 0) {
		struct scsi_device *sdev;
		shost_for_each_device(sdev, shost) {
			myrs_ldev_info *ldev_info;
			int ldev_num;

			if (sdev->channel < info->physchan_present)
				continue;
			ldev_info = sdev->hostdata;
			if (!ldev_info)
				continue;
			ldev_num = ldev_info->ldev_num;
			myrs_get_ldev_info(cs, ldev_num, ldev_info);
		}
		cs->needs_update = true;
	}
	if (epoch == cs->epoch &&
	    cs->fwstat_buf->next_evseq == cs->next_evseq &&
	    (cs->needs_update == false ||
	     time_before(jiffies, cs->primary_monitor_time
			 + MYRS_PRIMARY_MONITOR_INTERVAL))) {
		interval = MYRS_SECONDARY_MONITOR_INTERVAL;
	}

	if (interval > 1)
		cs->primary_monitor_time = jiffies;
	queue_delayed_work(cs->work_q, &cs->monitor_work, interval);
}

bool myrs_create_mempools(struct pci_dev *pdev, myrs_hba *cs)
{
	struct Scsi_Host *shost = cs->host;
	size_t elem_size, elem_align;

	elem_align = sizeof(myrs_sge);
	elem_size = shost->sg_tablesize * elem_align;
	cs->sg_pool = dma_pool_create("myrs_sg", &pdev->dev,
				      elem_size, elem_align, 0);
	if (cs->sg_pool == NULL) {
		shost_printk(KERN_ERR, shost,
			     "Failed to allocate SG pool\n");
		return false;
	}

	cs->sense_pool = dma_pool_create("myrs_sense", &pdev->dev,
					 MYRS_SENSE_SIZE, sizeof(int), 0);
	if (cs->sense_pool == NULL) {
		dma_pool_destroy(cs->sg_pool);
		cs->sg_pool = NULL;
		shost_printk(KERN_ERR, shost,
			     "Failed to allocate sense data pool\n");
		return false;
	}

	cs->dcdb_pool = dma_pool_create("myrs_dcdb", &pdev->dev,
					MYRS_DCDB_SIZE,
					sizeof(unsigned char), 0);
	if (!cs->dcdb_pool) {
		dma_pool_destroy(cs->sg_pool);
		cs->sg_pool = NULL;
		dma_pool_destroy(cs->sense_pool);
		cs->sense_pool = NULL;
		shost_printk(KERN_ERR, shost,
			     "Failed to allocate DCDB pool\n");
		return false;
	}

	snprintf(cs->work_q_name, sizeof(cs->work_q_name),
		 "myrs_wq_%d", shost->host_no);
	cs->work_q = create_singlethread_workqueue(cs->work_q_name);
	if (!cs->work_q) {
		dma_pool_destroy(cs->dcdb_pool);
		cs->dcdb_pool = NULL;
		dma_pool_destroy(cs->sg_pool);
		cs->sg_pool = NULL;
		dma_pool_destroy(cs->sense_pool);
		cs->sense_pool = NULL;
		shost_printk(KERN_ERR, shost,
			     "Failed to create workqueue\n");
		return false;
	}

	/*
	  Initialize the Monitoring Timer.
	*/
	INIT_DELAYED_WORK(&cs->monitor_work, myrs_monitor);
	queue_delayed_work(cs->work_q, &cs->monitor_work, 1);

	return true;
}

void myrs_destroy_mempools(myrs_hba *cs)
{
	cancel_delayed_work_sync(&cs->monitor_work);
	destroy_workqueue(cs->work_q);

	if (cs->sg_pool) {
		dma_pool_destroy(cs->sg_pool);
		cs->sg_pool = NULL;
	}

	if (cs->dcdb_pool) {
		dma_pool_destroy(cs->dcdb_pool);
		cs->dcdb_pool = NULL;
	}
	if (cs->sense_pool) {
		dma_pool_destroy(cs->sense_pool);
		cs->sense_pool = NULL;
	}
}

void myrs_unmap(myrs_hba *cs)
{
	if (cs->event_buf) {
		kfree(cs->event_buf);
		cs->event_buf = NULL;
	}
	if (cs->ctlr_info) {
		kfree(cs->ctlr_info);
		cs->ctlr_info = NULL;
	}
	if (cs->fwstat_buf) {
		dma_free_coherent(&cs->pdev->dev, sizeof(myrs_fwstat),
				  cs->fwstat_buf, cs->fwstat_addr);
		cs->fwstat_buf = NULL;
	}
	if (cs->first_stat_mbox) {
		dma_free_coherent(&cs->pdev->dev, cs->stat_mbox_size,
				  cs->first_stat_mbox, cs->stat_mbox_addr);
		cs->first_stat_mbox = NULL;
	}
	if (cs->first_cmd_mbox) {
		dma_free_coherent(&cs->pdev->dev, cs->cmd_mbox_size,
				  cs->first_cmd_mbox, cs->cmd_mbox_addr);
		cs->first_cmd_mbox = NULL;
	}
}

void myrs_cleanup(myrs_hba *cs)
{
	struct pci_dev *pdev = cs->pdev;

	/* Free the memory mailbox, status, and related structures */
	myrs_unmap(cs);

	if (cs->mmio_base) {
		cs->disable_intr(cs);
		iounmap(cs->mmio_base);
	}
	if (cs->irq)
		free_irq(cs->irq, cs);
	if (cs->io_addr)
		release_region(cs->io_addr, 0x80);
	iounmap(cs->mmio_base);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	scsi_host_put(cs->host);
}

static myrs_hba *myrs_detect(struct pci_dev *pdev,
			     const struct pci_device_id *entry)
{
	struct myrs_privdata *privdata =
		(struct myrs_privdata *)entry->driver_data;
	irq_handler_t irq_handler = privdata->irq_handler;
	unsigned int mmio_size = privdata->io_mem_size;
	myrs_hba *cs = NULL;

	cs = myrs_alloc_host(pdev, entry);
	if (!cs) {
		dev_err(&pdev->dev, "Unable to allocate Controller\n");
		return NULL;
	}
	cs->pdev = pdev;

	if (pci_enable_device(pdev))
		goto Failure;

	cs->pci_addr = pci_resource_start(pdev, 0);

	pci_set_drvdata(pdev, cs);
	spin_lock_init(&cs->queue_lock);
	/*
	  Map the Controller Register Window.
	*/
	if (mmio_size < PAGE_SIZE)
		mmio_size = PAGE_SIZE;
	cs->mmio_base = ioremap_nocache(cs->pci_addr & PAGE_MASK, mmio_size);
	if (cs->mmio_base == NULL) {
		dev_err(&pdev->dev,
			"Unable to map Controller Register Window\n");
		goto Failure;
	}

	cs->io_base = cs->mmio_base + (cs->pci_addr & ~PAGE_MASK);
	if (privdata->hw_init(pdev, cs, cs->io_base))
		goto Failure;

	/*
	  Acquire shared access to the IRQ Channel.
	*/
	if (request_irq(pdev->irq, irq_handler, IRQF_SHARED, "myrs", cs) < 0) {
		dev_err(&pdev->dev,
			"Unable to acquire IRQ Channel %d\n", pdev->irq);
		goto Failure;
	}
	cs->irq = pdev->irq;
	return cs;

Failure:
	dev_err(&pdev->dev,
		"Failed to initialize Controller\n");
	myrs_cleanup(cs);
	return NULL;
}

/*
 * Hardware-specific functions
 */

/*
  myrs_err_status reports Controller BIOS Messages passed through
  the Error Status Register when the driver performs the BIOS handshaking.
  It returns true for fatal errors and false otherwise.
*/

bool myrs_err_status(myrs_hba *cs, unsigned char status,
		    unsigned char parm0, unsigned char parm1)
{
	struct pci_dev *pdev = cs->pdev;

	switch (status) {
	case 0x00:
		dev_info(&pdev->dev,
			 "Physical Device %d:%d Not Responding\n",
			 parm1, parm0);
		break;
	case 0x08:
		dev_notice(&pdev->dev, "Spinning Up Drives\n");
		break;
	case 0x30:
		dev_notice(&pdev->dev, "Configuration Checksum Error\n");
		break;
	case 0x60:
		dev_notice(&pdev->dev, "Mirror Race Recovery Failed\n");
		break;
	case 0x70:
		dev_notice(&pdev->dev, "Mirror Race Recovery In Progress\n");
		break;
	case 0x90:
		dev_notice(&pdev->dev, "Physical Device %d:%d COD Mismatch\n",
			   parm1, parm0);
		break;
	case 0xA0:
		dev_notice(&pdev->dev, "Logical Drive Installation Aborted\n");
		break;
	case 0xB0:
		dev_notice(&pdev->dev, "Mirror Race On A Critical Logical Drive\n");
		break;
	case 0xD0:
		dev_notice(&pdev->dev, "New Controller Configuration Found\n");
		break;
	case 0xF0:
		dev_err(&pdev->dev, "Fatal Memory Parity Error\n");
		return true;
	default:
		dev_err(&pdev->dev, "Unknown Initialization Error %02X\n",
			status);
		return true;
	}
	return false;
}

/*
  DAC960_GEM_HardwareInit initializes the hardware for DAC960 GEM Series
  Controllers.
*/

static int DAC960_GEM_HardwareInit(struct pci_dev *pdev,
				   myrs_hba *cs, void __iomem *base)
{
	int timeout = 0;
	unsigned char status, parm0, parm1;

	DAC960_GEM_DisableInterrupts(base);
	DAC960_GEM_AcknowledgeHardwareMailboxStatus(base);
	udelay(1000);
	while (DAC960_GEM_InitializationInProgressP(base) &&
	       timeout < MYRS_MAILBOX_TIMEOUT) {
		if (DAC960_GEM_ReadErrorStatus(base, &status,
					       &parm0, &parm1) &&
		    myrs_err_status(cs, status, parm0, parm1))
			return -EIO;
		udelay(10);
		timeout++;
	}
	if (timeout == MYRS_MAILBOX_TIMEOUT) {
		dev_err(&pdev->dev,
			"Timeout waiting for Controller Initialisation\n");
		return -ETIMEDOUT;
	}
	if (!myrs_enable_mmio_mbox(cs, DAC960_GEM_MailboxInit)) {
		dev_err(&pdev->dev,
			"Unable to Enable Memory Mailbox Interface\n");
		DAC960_GEM_ControllerReset(base);
		return -EAGAIN;
	}
	DAC960_GEM_EnableInterrupts(base);
	cs->write_cmd_mbox = DAC960_GEM_WriteCommandMailbox;
	cs->get_cmd_mbox = DAC960_GEM_MemoryMailboxNewCommand;
	cs->disable_intr = DAC960_GEM_DisableInterrupts;
	cs->reset = DAC960_GEM_ControllerReset;
	return 0;
}

/*
  DAC960_GEM_InterruptHandler handles hardware interrupts from DAC960 GEM Series
  Controllers.
*/

static irqreturn_t DAC960_GEM_InterruptHandler(int irq,
					       void *DeviceIdentifier)
{
	myrs_hba *cs = DeviceIdentifier;
	void __iomem *base = cs->io_base;
	myrs_stat_mbox *next_stat_mbox;
	unsigned long flags;

	spin_lock_irqsave(&cs->queue_lock, flags);
	DAC960_GEM_AcknowledgeInterrupt(base);
	next_stat_mbox = cs->next_stat_mbox;
	while (next_stat_mbox->id > 0) {
		unsigned short id = next_stat_mbox->id;
		struct scsi_cmnd *scmd = NULL;
		myrs_cmdblk *cmd_blk = NULL;

		if (id == MYRS_DCMD_TAG)
			cmd_blk = &cs->dcmd_blk;
		else if (id == MYRS_MCMD_TAG)
			cmd_blk = &cs->mcmd_blk;
		else {
			scmd = scsi_host_find_tag(cs->host, id - 3);
			if (scmd)
				cmd_blk = scsi_cmd_priv(scmd);
		}
		if (cmd_blk) {
			cmd_blk->status = next_stat_mbox->status;
			cmd_blk->sense_len = next_stat_mbox->sense_len;
			cmd_blk->residual = next_stat_mbox->residual;
		} else
			dev_err(&cs->pdev->dev,
				"Unhandled command completion %d\n", id);

		memset(next_stat_mbox, 0, sizeof(myrs_stat_mbox));
		if (++next_stat_mbox > cs->last_stat_mbox)
			next_stat_mbox = cs->first_stat_mbox;

		if (id < 3)
			myrs_handle_cmdblk(cs, cmd_blk);
		else
			myrs_handle_scsi(cs, cmd_blk, scmd);
	}
	cs->next_stat_mbox = next_stat_mbox;
	spin_unlock_irqrestore(&cs->queue_lock, flags);
	return IRQ_HANDLED;
}

struct myrs_privdata DAC960_GEM_privdata = {
	.hw_init =		DAC960_GEM_HardwareInit,
	.irq_handler =		DAC960_GEM_InterruptHandler,
	.io_mem_size =		DAC960_GEM_RegisterWindowSize,
};


/*
  DAC960_BA_HardwareInit initializes the hardware for DAC960 BA Series
  Controllers.
*/

static int DAC960_BA_HardwareInit(struct pci_dev *pdev,
				  myrs_hba *cs, void __iomem *base)
{
	int timeout = 0;
	unsigned char status, parm0, parm1;

	DAC960_BA_DisableInterrupts(base);
	DAC960_BA_AcknowledgeHardwareMailboxStatus(base);
	udelay(1000);
	while (DAC960_BA_InitializationInProgressP(base) &&
	       timeout < MYRS_MAILBOX_TIMEOUT) {
		if (DAC960_BA_ReadErrorStatus(base, &status,
					      &parm0, &parm1) &&
		    myrs_err_status(cs, status, parm0, parm1))
			return -EIO;
		udelay(10);
		timeout++;
	}
	if (timeout == MYRS_MAILBOX_TIMEOUT) {
		dev_err(&pdev->dev,
			"Timeout waiting for Controller Initialisation\n");
		return -ETIMEDOUT;
	}
	if (!myrs_enable_mmio_mbox(cs, DAC960_BA_MailboxInit)) {
		dev_err(&pdev->dev,
			"Unable to Enable Memory Mailbox Interface\n");
		DAC960_BA_ControllerReset(base);
		return -EAGAIN;
	}
	DAC960_BA_EnableInterrupts(base);
	cs->write_cmd_mbox = DAC960_BA_WriteCommandMailbox;
	cs->get_cmd_mbox = DAC960_BA_MemoryMailboxNewCommand;
	cs->disable_intr = DAC960_BA_DisableInterrupts;
	cs->reset = DAC960_BA_ControllerReset;
	return 0;
}


/*
  DAC960_BA_InterruptHandler handles hardware interrupts from DAC960 BA Series
  Controllers.
*/

static irqreturn_t DAC960_BA_InterruptHandler(int irq,
					      void *DeviceIdentifier)
{
	myrs_hba *cs = DeviceIdentifier;
	void __iomem *base = cs->io_base;
	myrs_stat_mbox *next_stat_mbox;
	unsigned long flags;

	spin_lock_irqsave(&cs->queue_lock, flags);
	DAC960_BA_AcknowledgeInterrupt(base);
	next_stat_mbox = cs->next_stat_mbox;
	while (next_stat_mbox->id > 0) {
		unsigned short id = next_stat_mbox->id;
		struct scsi_cmnd *scmd = NULL;
		myrs_cmdblk *cmd_blk = NULL;

		if (id == MYRS_DCMD_TAG)
			cmd_blk = &cs->dcmd_blk;
		else if (id == MYRS_MCMD_TAG)
			cmd_blk = &cs->mcmd_blk;
		else {
			scmd = scsi_host_find_tag(cs->host, id - 3);
			if (scmd)
				cmd_blk = scsi_cmd_priv(scmd);
		}
		if (cmd_blk) {
			cmd_blk->status = next_stat_mbox->status;
			cmd_blk->sense_len = next_stat_mbox->sense_len;
			cmd_blk->residual = next_stat_mbox->residual;
		} else
			dev_err(&cs->pdev->dev,
				"Unhandled command completion %d\n", id);

		memset(next_stat_mbox, 0, sizeof(myrs_stat_mbox));
		if (++next_stat_mbox > cs->last_stat_mbox)
			next_stat_mbox = cs->first_stat_mbox;

		if (id < 3)
			myrs_handle_cmdblk(cs, cmd_blk);
		else
			myrs_handle_scsi(cs, cmd_blk, scmd);
	}
	cs->next_stat_mbox = next_stat_mbox;
	spin_unlock_irqrestore(&cs->queue_lock, flags);
	return IRQ_HANDLED;
}

struct myrs_privdata DAC960_BA_privdata = {
	.hw_init =		DAC960_BA_HardwareInit,
	.irq_handler =		DAC960_BA_InterruptHandler,
	.io_mem_size =		DAC960_BA_RegisterWindowSize,
};


/*
  DAC960_LP_HardwareInit initializes the hardware for DAC960 LP Series
  Controllers.
*/

static int DAC960_LP_HardwareInit(struct pci_dev *pdev,
				  myrs_hba *cs, void __iomem *base)
{
	int timeout = 0;
	unsigned char status, parm0, parm1;

	DAC960_LP_DisableInterrupts(base);
	DAC960_LP_AcknowledgeHardwareMailboxStatus(base);
	udelay(1000);
	while (DAC960_LP_InitializationInProgressP(base) &&
	       timeout < MYRS_MAILBOX_TIMEOUT) {
		if (DAC960_LP_ReadErrorStatus(base, &status,
					      &parm0, &parm1) &&
		    myrs_err_status(cs, status,parm0, parm1))
			return -EIO;
		udelay(10);
		timeout++;
	}
	if (timeout == MYRS_MAILBOX_TIMEOUT) {
		dev_err(&pdev->dev,
			"Timeout waiting for Controller Initialisation\n");
		return -ETIMEDOUT;
	}
	if (!myrs_enable_mmio_mbox(cs, DAC960_LP_MailboxInit)) {
		dev_err(&pdev->dev,
			"Unable to Enable Memory Mailbox Interface\n");
		DAC960_LP_ControllerReset(base);
		return -ENODEV;
	}
	DAC960_LP_EnableInterrupts(base);
	cs->write_cmd_mbox = DAC960_LP_WriteCommandMailbox;
	cs->get_cmd_mbox = DAC960_LP_MemoryMailboxNewCommand;
	cs->disable_intr = DAC960_LP_DisableInterrupts;
	cs->reset = DAC960_LP_ControllerReset;

	return 0;
}

/*
  DAC960_LP_InterruptHandler handles hardware interrupts from DAC960 LP Series
  Controllers.
*/

static irqreturn_t DAC960_LP_InterruptHandler(int irq,
					      void *DeviceIdentifier)
{
	myrs_hba *cs = DeviceIdentifier;
	void __iomem *base = cs->io_base;
	myrs_stat_mbox *next_stat_mbox;
	unsigned long flags;

	spin_lock_irqsave(&cs->queue_lock, flags);
	DAC960_LP_AcknowledgeInterrupt(base);
	next_stat_mbox = cs->next_stat_mbox;
	while (next_stat_mbox->id > 0) {
		unsigned short id = next_stat_mbox->id;
		struct scsi_cmnd *scmd = NULL;
		myrs_cmdblk *cmd_blk = NULL;

		if (id == MYRS_DCMD_TAG)
			cmd_blk = &cs->dcmd_blk;
		else if (id == MYRS_MCMD_TAG)
			cmd_blk = &cs->mcmd_blk;
		else {
			scmd = scsi_host_find_tag(cs->host, id - 3);
			if (scmd)
				cmd_blk = scsi_cmd_priv(scmd);
		}
		if (cmd_blk) {
			cmd_blk->status = next_stat_mbox->status;
			cmd_blk->sense_len = next_stat_mbox->sense_len;
			cmd_blk->residual = next_stat_mbox->residual;
		} else
			dev_err(&cs->pdev->dev,
				"Unhandled command completion %d\n", id);

		memset(next_stat_mbox, 0, sizeof(myrs_stat_mbox));
		if (++next_stat_mbox > cs->last_stat_mbox)
			next_stat_mbox = cs->first_stat_mbox;

		if (id < 3)
			myrs_handle_cmdblk(cs, cmd_blk);
		else
			myrs_handle_scsi(cs, cmd_blk, scmd);
	}
	cs->next_stat_mbox = next_stat_mbox;
	spin_unlock_irqrestore(&cs->queue_lock, flags);
	return IRQ_HANDLED;
}

struct myrs_privdata DAC960_LP_privdata = {
	.hw_init =		DAC960_LP_HardwareInit,
	.irq_handler =		DAC960_LP_InterruptHandler,
	.io_mem_size =		DAC960_LP_RegisterWindowSize,
};

/*
 * Module functions
 */

static int
myrs_probe(struct pci_dev *dev, const struct pci_device_id *entry)
{
	myrs_hba *cs;
	int ret;

	cs = myrs_detect(dev, entry);
	if (!cs)
		return -ENODEV;

	ret = myrs_get_config(cs);
	if (ret < 0) {
		myrs_cleanup(cs);
		return ret;
	}

	if (!myrs_create_mempools(dev, cs)) {
		ret = -ENOMEM;
		goto failed;
	}

	ret = scsi_add_host(cs->host, &dev->dev);
	if (ret) {
		dev_err(&dev->dev, "scsi_add_host failed with %d\n", ret);
		myrs_destroy_mempools(cs);
		goto failed;
	}
	scsi_scan_host(cs->host);
	return 0;
failed:
	myrs_cleanup(cs);
	return ret;
}


static void myrs_remove(struct pci_dev *pdev)
{
	myrs_hba *cs = pci_get_drvdata(pdev);

	if (cs == NULL)
		return;

	shost_printk(KERN_NOTICE, cs->host, "Flushing Cache...");
	myrs_flush_cache(cs);
	myrs_destroy_mempools(cs);
	myrs_cleanup(cs);
}


static const struct pci_device_id myrs_id_table[] = {
	{
		.vendor		= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_GEM,
		.subvendor	= PCI_VENDOR_ID_MYLEX,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_GEM_privdata,
	},
	{
		.vendor		= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_BA,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_BA_privdata,
	},
	{
		.vendor		= PCI_VENDOR_ID_MYLEX,
		.device		= PCI_DEVICE_ID_MYLEX_DAC960_LP,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.driver_data	= (unsigned long) &DAC960_LP_privdata,
	},
	{0, },
};

MODULE_DEVICE_TABLE(pci, myrs_id_table);

static struct pci_driver myrs_pci_driver = {
	.name		= "myrs",
	.id_table	= myrs_id_table,
	.probe		= myrs_probe,
	.remove		= myrs_remove,
};

static int __init myrs_init_module(void)
{
	int ret;

	myrs_raid_template = raid_class_attach(&myrs_raid_functions);
	if (!myrs_raid_template)
		return -ENODEV;

	ret = pci_register_driver(&myrs_pci_driver);
	if (ret)
		raid_class_release(myrs_raid_template);

	return ret;
}

static void __exit myrs_cleanup_module(void)
{
	pci_unregister_driver(&myrs_pci_driver);
	raid_class_release(myrs_raid_template);
}

module_init(myrs_init_module);
module_exit(myrs_cleanup_module);

MODULE_DESCRIPTION("Mylex DAC960/AcceleRAID/eXtremeRAID driver (SCSI Interface)");
MODULE_AUTHOR("Hannes Reinecke <hare@suse.com>");
MODULE_LICENSE("GPL");
