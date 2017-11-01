/*
 * GK20A Graphics FIFO (gr host)
 *
 * Copyright (c) 2011-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <trace/events/gk20a.h>
#include <linux/dma-mapping.h>
#include <linux/nvhost.h>

#include "gk20a.h"
#include "debug_gk20a.h"
#include "semaphore_gk20a.h"
#include "hw_fifo_gk20a.h"
#include "hw_pbdma_gk20a.h"
#include "hw_ccsr_gk20a.h"
#include "hw_ram_gk20a.h"
#include "hw_proj_gk20a.h"
#include "hw_top_gk20a.h"
#include "hw_mc_gk20a.h"
#include "hw_gr_gk20a.h"
#define FECS_METHOD_WFI_RESTORE 0x80000

static int gk20a_fifo_update_runlist_locked(struct gk20a *g, u32 runlist_id,
					    u32 hw_chid, bool add,
					    bool wait_for_finish);

/*
 * Link engine IDs to MMU IDs and vice versa.
 */

static inline u32 gk20a_engine_id_to_mmu_id(u32 engine_id)
{
	switch (engine_id) {
	case ENGINE_GR_GK20A:
		return 0x00;
	case ENGINE_CE2_GK20A:
		return 0x1b;
	default:
		return ~0;
	}
}

static inline u32 gk20a_mmu_id_to_engine_id(u32 engine_id)
{
	switch (engine_id) {
	case 0x00:
		return ENGINE_GR_GK20A;
	case 0x1b:
		return ENGINE_CE2_GK20A;
	default:
		return ~0;
	}
}


static int init_engine_info(struct fifo_gk20a *f)
{
	struct gk20a *g = f->g;
	struct device *d = dev_from_gk20a(g);
	u32 i;
	u32 max_info_entries = top_device_info__size_1_v();

	gk20a_dbg_fn("");

	/* all we really care about finding is the graphics entry    */
	/* especially early on in sim it probably thinks it has more */
	f->num_engines = 2;

	for (i = 0; i < max_info_entries; i++) {
		struct fifo_engine_info_gk20a *info = NULL;
		u32 table_entry = gk20a_readl(f->g, top_device_info_r(i));
		u32 entry = top_device_info_entry_v(table_entry);
		u32 engine_enum;
		int pbdma_id;
		u32 runlist_bit;

		if (entry != top_device_info_entry_enum_v())
			continue;

		/* we only care about GR engine here */
		engine_enum = top_device_info_engine_enum_v(table_entry);
		if (engine_enum >= ENGINE_INVAL_GK20A)
			continue;

		gk20a_dbg_info("info: engine_id %d",
				top_device_info_engine_enum_v(table_entry));
		info = &g->fifo.engine_info[engine_enum];

		info->runlist_id =
			top_device_info_runlist_enum_v(table_entry);
		gk20a_dbg_info("gr info: runlist_id %d", info->runlist_id);

		info->engine_id =
			top_device_info_engine_enum_v(table_entry);
		gk20a_dbg_info("gr info: engine_id %d", info->engine_id);

		runlist_bit = 1 << info->runlist_id;

		for (pbdma_id = 0; pbdma_id < f->num_pbdma; pbdma_id++) {
			gk20a_dbg_info("gr info: pbdma_map[%d]=%d",
				pbdma_id, f->pbdma_map[pbdma_id]);
			if (f->pbdma_map[pbdma_id] & runlist_bit)
				break;
		}

		if (pbdma_id == f->num_pbdma) {
			gk20a_err(d, "busted pbmda map");
			return -EINVAL;
		}
		info->pbdma_id = pbdma_id;

		info->intr_id =
			top_device_info_intr_enum_v(table_entry);
		gk20a_dbg_info("gr info: intr_id %d", info->intr_id);

		info->reset_id =
			top_device_info_reset_enum_v(table_entry);
		gk20a_dbg_info("gr info: reset_id %d",
				info->reset_id);

	}

	return 0;
}

u32 gk20a_fifo_engine_interrupt_mask(struct gk20a *g)
{
	u32 eng_intr_mask = 0;
	int i = 0;

	for (i = 0; i < g->fifo.max_engines; i++) {
		u32 intr_id = g->fifo.engine_info[i].intr_id;
		if (i == ENGINE_CE2_GK20A &&
			(!g->ops.ce2.isr_stall || !g->ops.ce2.isr_nonstall))
			continue;

		if (intr_id)
			eng_intr_mask |= BIT(intr_id);
	}

	return eng_intr_mask;
}

static void gk20a_remove_fifo_support(struct fifo_gk20a *f)
{
	struct gk20a *g = f->g;
	struct fifo_engine_info_gk20a *engine_info;
	struct fifo_runlist_info_gk20a *runlist;
	u32 runlist_id;
	u32 i;

	gk20a_dbg_fn("");

	if (f->channel) {
		int c;
		for (c = 0; c < f->num_channels; c++) {
			if (f->channel[c].remove_support)
				f->channel[c].remove_support(f->channel+c);
		}
		kfree(f->channel);
	}
	gk20a_gmmu_unmap_free(&g->mm.bar1.vm, &f->userd);

	engine_info = f->engine_info + ENGINE_GR_GK20A;
	runlist_id = engine_info->runlist_id;
	runlist = &f->runlist_info[runlist_id];

	for (i = 0; i < MAX_RUNLIST_BUFFERS; i++)
		gk20a_gmmu_free(g, &runlist->mem[i]);

	kfree(runlist->active_channels);
	kfree(runlist->active_tsgs);

	kfree(f->runlist_info);
	kfree(f->pbdma_map);
	kfree(f->engine_info);
}

/* reads info from hardware and fills in pbmda exception info record */
static inline void get_exception_pbdma_info(
	struct gk20a *g,
	struct fifo_engine_info_gk20a *eng_info)
{
	struct fifo_pbdma_exception_info_gk20a *e =
		&eng_info->pbdma_exception_info;

	u32 pbdma_status_r = e->status_r = gk20a_readl(g,
		   fifo_pbdma_status_r(eng_info->pbdma_id));
	e->id = fifo_pbdma_status_id_v(pbdma_status_r); /* vs. id_hw_v()? */
	e->id_is_chid = fifo_pbdma_status_id_type_v(pbdma_status_r) ==
		fifo_pbdma_status_id_type_chid_v();
	e->chan_status_v  = fifo_pbdma_status_chan_status_v(pbdma_status_r);
	e->next_id_is_chid =
		fifo_pbdma_status_next_id_type_v(pbdma_status_r) ==
		fifo_pbdma_status_next_id_type_chid_v();
	e->next_id = fifo_pbdma_status_next_id_v(pbdma_status_r);
	e->chsw_in_progress =
		fifo_pbdma_status_chsw_v(pbdma_status_r) ==
		fifo_pbdma_status_chsw_in_progress_v();
}

static void fifo_pbdma_exception_status(struct gk20a *g,
	struct fifo_engine_info_gk20a *eng_info)
{
	struct fifo_pbdma_exception_info_gk20a *e;
	get_exception_pbdma_info(g, eng_info);
	e = &eng_info->pbdma_exception_info;

	gk20a_dbg_fn("pbdma_id %d, "
		      "id_type %s, id %d, chan_status %d, "
		      "next_id_type %s, next_id %d, "
		      "chsw_in_progress %d",
		      eng_info->pbdma_id,
		      e->id_is_chid ? "chid" : "tsgid", e->id, e->chan_status_v,
		      e->next_id_is_chid ? "chid" : "tsgid", e->next_id,
		      e->chsw_in_progress);
}

/* reads info from hardware and fills in pbmda exception info record */
static inline void get_exception_engine_info(
	struct gk20a *g,
	struct fifo_engine_info_gk20a *eng_info)
{
	struct fifo_engine_exception_info_gk20a *e =
		&eng_info->engine_exception_info;
	u32 engine_status_r = e->status_r =
		gk20a_readl(g, fifo_engine_status_r(eng_info->engine_id));
	e->id = fifo_engine_status_id_v(engine_status_r); /* vs. id_hw_v()? */
	e->id_is_chid = fifo_engine_status_id_type_v(engine_status_r) ==
		fifo_engine_status_id_type_chid_v();
	e->ctx_status_v = fifo_engine_status_ctx_status_v(engine_status_r);
	e->faulted =
		fifo_engine_status_faulted_v(engine_status_r) ==
		fifo_engine_status_faulted_true_v();
	e->idle =
		fifo_engine_status_engine_v(engine_status_r) ==
		fifo_engine_status_engine_idle_v();
	e->ctxsw_in_progress =
		fifo_engine_status_ctxsw_v(engine_status_r) ==
		fifo_engine_status_ctxsw_in_progress_v();
}

static void fifo_engine_exception_status(struct gk20a *g,
			       struct fifo_engine_info_gk20a *eng_info)
{
	struct fifo_engine_exception_info_gk20a *e;
	get_exception_engine_info(g, eng_info);
	e = &eng_info->engine_exception_info;

	gk20a_dbg_fn("engine_id %d, id_type %s, id %d, ctx_status %d, "
		      "faulted %d, idle %d, ctxsw_in_progress %d, ",
		      eng_info->engine_id, e->id_is_chid ? "chid" : "tsgid",
		      e->id, e->ctx_status_v,
		      e->faulted, e->idle,  e->ctxsw_in_progress);
}

static int init_runlist(struct gk20a *g, struct fifo_gk20a *f)
{
	struct fifo_engine_info_gk20a *engine_info;
	struct fifo_runlist_info_gk20a *runlist;
	struct device *d = dev_from_gk20a(g);
	u32 runlist_id;
	u32 i;
	u64 runlist_size;

	gk20a_dbg_fn("");

	f->max_runlists = fifo_eng_runlist_base__size_1_v();
	f->runlist_info = kzalloc(sizeof(struct fifo_runlist_info_gk20a) *
				  f->max_runlists, GFP_KERNEL);
	if (!f->runlist_info)
		goto clean_up;

	engine_info = f->engine_info + ENGINE_GR_GK20A;
	runlist_id = engine_info->runlist_id;
	runlist = &f->runlist_info[runlist_id];

	runlist->active_channels =
		kzalloc(DIV_ROUND_UP(f->num_channels, BITS_PER_BYTE),
			GFP_KERNEL);
	if (!runlist->active_channels)
		goto clean_up_runlist_info;

	runlist->active_tsgs =
		kzalloc(DIV_ROUND_UP(f->num_channels, BITS_PER_BYTE),
			GFP_KERNEL);
	if (!runlist->active_tsgs)
		goto clean_up_runlist_info;

	runlist_size  = ram_rl_entry_size_v() * f->num_channels;
	for (i = 0; i < MAX_RUNLIST_BUFFERS; i++) {
		int err = gk20a_gmmu_alloc(g, runlist_size, &runlist->mem[i]);
		if (err) {
			dev_err(d, "memory allocation failed\n");
			goto clean_up_runlist;
		}
	}
	mutex_init(&runlist->mutex);

	/* None of buffers is pinned if this value doesn't change.
	    Otherwise, one of them (cur_buffer) must have been pinned. */
	runlist->cur_buffer = MAX_RUNLIST_BUFFERS;

	gk20a_dbg_fn("done");
	return 0;

clean_up_runlist:
	for (i = 0; i < MAX_RUNLIST_BUFFERS; i++)
		gk20a_gmmu_free(g, &runlist->mem[i]);

	kfree(runlist->active_channels);
	runlist->active_channels = NULL;

clean_up_runlist_info:
	kfree(f->runlist_info);
	f->runlist_info = NULL;

clean_up:
	gk20a_dbg_fn("fail");
	return -ENOMEM;
}

#define GRFIFO_TIMEOUT_CHECK_PERIOD_US 100000

int gk20a_init_fifo_reset_enable_hw(struct gk20a *g)
{
	u32 intr_stall;
	u32 mask;
	u32 timeout;
	int i;

	gk20a_dbg_fn("");
	/* enable pmc pfifo */
	gk20a_reset(g, mc_enable_pfifo_enabled_f()
			| mc_enable_ce2_enabled_f());

	if (g->ops.clock_gating.slcg_ce2_load_gating_prod)
		g->ops.clock_gating.slcg_ce2_load_gating_prod(g,
				g->slcg_enabled);
	if (g->ops.clock_gating.slcg_fifo_load_gating_prod)
		g->ops.clock_gating.slcg_fifo_load_gating_prod(g,
				g->slcg_enabled);
	if (g->ops.clock_gating.blcg_fifo_load_gating_prod)
		g->ops.clock_gating.blcg_fifo_load_gating_prod(g,
				g->blcg_enabled);

	/* enable pbdma */
	mask = 0;
	for (i = 0; i < proj_host_num_pbdma_v(); ++i)
		mask |= mc_enable_pb_sel_f(mc_enable_pb_0_enabled_v(), i);
	gk20a_writel(g, mc_enable_pb_r(), mask);

	/* enable pfifo interrupt */
	gk20a_writel(g, fifo_intr_0_r(), 0xFFFFFFFF);
	gk20a_writel(g, fifo_intr_en_0_r(), 0x7FFFFFFF);
	gk20a_writel(g, fifo_intr_en_1_r(), 0x80000000);

	/* enable pbdma interrupt */
	mask = 0;
	for (i = 0; i < proj_host_num_pbdma_v(); i++) {
		intr_stall = gk20a_readl(g, pbdma_intr_stall_r(i));
		intr_stall &= ~pbdma_intr_stall_lbreq_enabled_f();
		gk20a_writel(g, pbdma_intr_stall_r(i), intr_stall);
		gk20a_writel(g, pbdma_intr_0_r(i), 0xFFFFFFFF);
		gk20a_writel(g, pbdma_intr_en_0_r(i),
			~pbdma_intr_en_0_lbreq_enabled_f());
		gk20a_writel(g, pbdma_intr_1_r(i), 0xFFFFFFFF);
		gk20a_writel(g, pbdma_intr_en_1_r(i),
			~pbdma_intr_en_0_lbreq_enabled_f());
	}

	/* TBD: apply overrides */

	/* TBD: BLCG prod */

	/* reset runlist interrupts */
	gk20a_writel(g, fifo_intr_runlist_r(), ~0);

	/* TBD: do we need those? */
	timeout = gk20a_readl(g, fifo_fb_timeout_r());
	timeout = set_field(timeout, fifo_fb_timeout_period_m(),
			fifo_fb_timeout_period_max_f());
	gk20a_writel(g, fifo_fb_timeout_r(), timeout);

	for (i = 0; i < pbdma_timeout__size_1_v(); i++) {
		timeout = gk20a_readl(g, pbdma_timeout_r(i));
		timeout = set_field(timeout, pbdma_timeout_period_m(),
				    pbdma_timeout_period_max_f());
		gk20a_writel(g, pbdma_timeout_r(i), timeout);
	}

	if (g->ops.fifo.apply_pb_timeout)
		g->ops.fifo.apply_pb_timeout(g);

	timeout = GRFIFO_TIMEOUT_CHECK_PERIOD_US |
			fifo_eng_timeout_detection_enabled_f();
	gk20a_writel(g, fifo_eng_timeout_r(), timeout);

	gk20a_dbg_fn("done");

	return 0;
}

static void gk20a_init_fifo_pbdma_intr_descs(struct fifo_gk20a *f)
{
	/* These are all errors which indicate something really wrong
	 * going on in the device. */
	f->intr.pbdma.device_fatal_0 =
		pbdma_intr_0_memreq_pending_f() |
		pbdma_intr_0_memack_timeout_pending_f() |
		pbdma_intr_0_memack_extra_pending_f() |
		pbdma_intr_0_memdat_timeout_pending_f() |
		pbdma_intr_0_memdat_extra_pending_f() |
		pbdma_intr_0_memflush_pending_f() |
		pbdma_intr_0_memop_pending_f() |
		pbdma_intr_0_lbconnect_pending_f() |
		pbdma_intr_0_lback_timeout_pending_f() |
		pbdma_intr_0_lback_extra_pending_f() |
		pbdma_intr_0_lbdat_timeout_pending_f() |
		pbdma_intr_0_lbdat_extra_pending_f() |
		pbdma_intr_0_xbarconnect_pending_f() |
		pbdma_intr_0_pri_pending_f();

	/* These are data parsing, framing errors or others which can be
	 * recovered from with intervention... or just resetting the
	 * channel. */
	f->intr.pbdma.channel_fatal_0 =
		pbdma_intr_0_gpfifo_pending_f() |
		pbdma_intr_0_gpptr_pending_f() |
		pbdma_intr_0_gpentry_pending_f() |
		pbdma_intr_0_gpcrc_pending_f() |
		pbdma_intr_0_pbptr_pending_f() |
		pbdma_intr_0_pbentry_pending_f() |
		pbdma_intr_0_pbcrc_pending_f() |
		pbdma_intr_0_method_pending_f() |
		pbdma_intr_0_methodcrc_pending_f() |
		pbdma_intr_0_pbseg_pending_f() |
		pbdma_intr_0_signature_pending_f();

	/* Can be used for sw-methods, or represents
	 * a recoverable timeout. */
	f->intr.pbdma.restartable_0 =
		pbdma_intr_0_device_pending_f() |
		pbdma_intr_0_acquire_pending_f();
}

static int gk20a_init_fifo_setup_sw(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;
	struct device *d = dev_from_gk20a(g);
	int chid, i, err = 0;

	gk20a_dbg_fn("");

	if (f->sw_ready) {
		gk20a_dbg_fn("skip init");
		return 0;
	}

	f->g = g;

	mutex_init(&f->intr.isr.mutex);
	mutex_init(&f->gr_reset_mutex);
	gk20a_init_fifo_pbdma_intr_descs(f); /* just filling in data/tables */

	f->num_channels = g->ops.fifo.get_num_fifos(g);
	f->num_pbdma = proj_host_num_pbdma_v();
	f->max_engines = ENGINE_INVAL_GK20A;

	f->userd_entry_size = 1 << ram_userd_base_shift_v();

	err = gk20a_gmmu_alloc_map(&g->mm.bar1.vm,
				   f->userd_entry_size * f->num_channels,
				   &f->userd);
	if (err) {
		dev_err(d, "memory allocation failed\n");
		goto clean_up;
	}

	gk20a_dbg(gpu_dbg_map, "userd bar1 va = 0x%llx", f->userd.gpu_va);

	f->channel = kzalloc(f->num_channels * sizeof(*f->channel),
				GFP_KERNEL);
	f->tsg = kzalloc(f->num_channels * sizeof(*f->tsg),
				GFP_KERNEL);
	f->pbdma_map = kzalloc(f->num_pbdma * sizeof(*f->pbdma_map),
				GFP_KERNEL);
	f->engine_info = kzalloc(f->max_engines * sizeof(*f->engine_info),
				GFP_KERNEL);

	if (!(f->channel && f->pbdma_map && f->engine_info)) {
		err = -ENOMEM;
		goto clean_up;
	}

	/* pbdma map needs to be in place before calling engine info init */
	for (i = 0; i < f->num_pbdma; ++i)
		f->pbdma_map[i] = gk20a_readl(g, fifo_pbdma_map_r(i));

	init_engine_info(f);

	init_runlist(g, f);

	INIT_LIST_HEAD(&f->free_chs);
	mutex_init(&f->free_chs_mutex);

	for (chid = 0; chid < f->num_channels; chid++) {
		f->channel[chid].userd_cpu_va =
			f->userd.cpu_va + chid * f->userd_entry_size;
		f->channel[chid].userd_iova =
			g->ops.mm.get_iova_addr(g, f->userd.sgt->sgl, 0)
				+ chid * f->userd_entry_size;
		f->channel[chid].userd_gpu_va =
			f->userd.gpu_va + chid * f->userd_entry_size;

		gk20a_init_channel_support(g, chid);
		gk20a_init_tsg_support(g, chid);
	}
	mutex_init(&f->tsg_inuse_mutex);

	f->remove_support = gk20a_remove_fifo_support;

	f->deferred_reset_pending = false;
	mutex_init(&f->deferred_reset_mutex);

	f->sw_ready = true;

	gk20a_dbg_fn("done");
	return 0;

clean_up:
	gk20a_dbg_fn("fail");
	gk20a_gmmu_unmap_free(&g->mm.bar1.vm, &f->userd);

	kfree(f->channel);
	f->channel = NULL;
	kfree(f->pbdma_map);
	f->pbdma_map = NULL;
	kfree(f->engine_info);
	f->engine_info = NULL;

	return err;
}

static void gk20a_fifo_handle_runlist_event(struct gk20a *g)
{
	u32 runlist_event = gk20a_readl(g, fifo_intr_runlist_r());

	gk20a_dbg(gpu_dbg_intr, "runlist event %08x\n",
		  runlist_event);

	gk20a_writel(g, fifo_intr_runlist_r(), runlist_event);
}

static int gk20a_init_fifo_setup_hw(struct gk20a *g)
{
	struct fifo_gk20a *f = &g->fifo;

	gk20a_dbg_fn("");

	/* test write, read through bar1 @ userd region before
	 * turning on the snooping */
	{
		struct fifo_gk20a *f = &g->fifo;
		u32 v, v1 = 0x33, v2 = 0x55;

		u32 bar1_vaddr = f->userd.gpu_va;
		volatile u32 *cpu_vaddr = f->userd.cpu_va;

		gk20a_dbg_info("test bar1 @ vaddr 0x%x",
			   bar1_vaddr);

		v = gk20a_bar1_readl(g, bar1_vaddr);

		*cpu_vaddr = v1;
		smp_mb();

		if (v1 != gk20a_bar1_readl(g, bar1_vaddr)) {
			gk20a_err(dev_from_gk20a(g), "bar1 broken @ gk20a: CPU wrote 0x%x, \
				GPU read 0x%x", *cpu_vaddr, gk20a_bar1_readl(g, bar1_vaddr));
			return -EINVAL;
		}

		gk20a_bar1_writel(g, bar1_vaddr, v2);

		if (v2 != gk20a_bar1_readl(g, bar1_vaddr)) {
			gk20a_err(dev_from_gk20a(g), "bar1 broken @ gk20a: GPU wrote 0x%x, \
				CPU read 0x%x", gk20a_bar1_readl(g, bar1_vaddr), *cpu_vaddr);
			return -EINVAL;
		}

		/* is it visible to the cpu? */
		if (*cpu_vaddr != v2) {
			gk20a_err(dev_from_gk20a(g),
				"cpu didn't see bar1 write @ %p!",
				cpu_vaddr);
		}

		/* put it back */
		gk20a_bar1_writel(g, bar1_vaddr, v);
	}

	/*XXX all manner of flushes and caching worries, etc */

	/* set the base for the userd region now */
	gk20a_writel(g, fifo_bar1_base_r(),
			fifo_bar1_base_ptr_f(f->userd.gpu_va >> 12) |
			fifo_bar1_base_valid_true_f());

	gk20a_dbg_fn("done");

	return 0;
}

int gk20a_init_fifo_support(struct gk20a *g)
{
	u32 err;

	err = gk20a_init_fifo_setup_sw(g);
	if (err)
		return err;

	err = gk20a_init_fifo_setup_hw(g);
	if (err)
		return err;

	return err;
}

/* return with a reference to the channel, caller must put it back */
static struct channel_gk20a *
channel_from_inst_ptr(struct fifo_gk20a *f, u64 inst_ptr)
{
	int ci;
	if (unlikely(!f->channel))
		return NULL;
	for (ci = 0; ci < f->num_channels; ci++) {
		struct channel_gk20a *ch = gk20a_channel_get(&f->channel[ci]);
		/* only alive channels are searched */
		if (!ch)
			continue;

		if (ch->inst_block.cpu_va &&
		    (inst_ptr == gk20a_mem_phys(&ch->inst_block)))
			return ch;

		gk20a_channel_put(ch);
	}
	return NULL;
}

/* fault info/descriptions.
 * tbd: move to setup
 *  */
static const char * const fault_type_descs[] = {
	 "pde", /*fifo_intr_mmu_fault_info_type_pde_v() == 0 */
	 "pde size",
	 "pte",
	 "va limit viol",
	 "unbound inst",
	 "priv viol",
	 "ro viol",
	 "wo viol",
	 "pitch mask",
	 "work creation",
	 "bad aperture",
	 "compression failure",
	 "bad kind",
	 "region viol",
	 "dual ptes",
	 "poisoned",
};
/* engine descriptions */
static const char * const engine_subid_descs[] = {
	"gpc",
	"hub",
};

static const char * const hub_client_descs[] = {
	"vip", "ce0", "ce1", "dniso", "fe", "fecs", "host", "host cpu",
	"host cpu nb", "iso", "mmu", "mspdec", "msppp", "msvld",
	"niso", "p2p", "pd", "perf", "pmu", "raster twod", "scc",
	"scc nb", "sec", "ssync", "gr copy", "ce2", "xv", "mmu nb",
	"msenc", "d falcon", "sked", "a falcon", "n/a",
};

static const char * const gpc_client_descs[] = {
	"l1 0", "t1 0", "pe 0",
	"l1 1", "t1 1", "pe 1",
	"l1 2", "t1 2", "pe 2",
	"l1 3", "t1 3", "pe 3",
	"rast", "gcc", "gpccs",
	"prop 0", "prop 1", "prop 2", "prop 3",
	"l1 4", "t1 4", "pe 4",
	"l1 5", "t1 5", "pe 5",
	"l1 6", "t1 6", "pe 6",
	"l1 7", "t1 7", "pe 7",
	"gpm",
	"ltp utlb 0", "ltp utlb 1", "ltp utlb 2", "ltp utlb 3",
	"rgg utlb",
};

/* reads info from hardware and fills in mmu fault info record */
static inline void get_exception_mmu_fault_info(
	struct gk20a *g, u32 engine_id,
	struct fifo_mmu_fault_info_gk20a *f)
{
	u32 fault_info_v;

	gk20a_dbg_fn("engine_id %d", engine_id);

	memset(f, 0, sizeof(*f));

	f->fault_info_v = fault_info_v = gk20a_readl(g,
	     fifo_intr_mmu_fault_info_r(engine_id));
	f->fault_type_v =
		fifo_intr_mmu_fault_info_type_v(fault_info_v);
	f->engine_subid_v =
		fifo_intr_mmu_fault_info_engine_subid_v(fault_info_v);
	f->client_v = fifo_intr_mmu_fault_info_client_v(fault_info_v);

	BUG_ON(f->fault_type_v >= ARRAY_SIZE(fault_type_descs));
	f->fault_type_desc =  fault_type_descs[f->fault_type_v];

	BUG_ON(f->engine_subid_v >= ARRAY_SIZE(engine_subid_descs));
	f->engine_subid_desc = engine_subid_descs[f->engine_subid_v];

	if (f->engine_subid_v ==
	    fifo_intr_mmu_fault_info_engine_subid_hub_v()) {

		BUG_ON(f->client_v >= ARRAY_SIZE(hub_client_descs));
		f->client_desc = hub_client_descs[f->client_v];
	} else if (f->engine_subid_v ==
		   fifo_intr_mmu_fault_info_engine_subid_gpc_v()) {
		BUG_ON(f->client_v >= ARRAY_SIZE(gpc_client_descs));
		f->client_desc = gpc_client_descs[f->client_v];
	} else {
		BUG_ON(1);
	}

	f->fault_hi_v = gk20a_readl(g, fifo_intr_mmu_fault_hi_r(engine_id));
	f->fault_lo_v = gk20a_readl(g, fifo_intr_mmu_fault_lo_r(engine_id));
	/* note:ignoring aperture on gk20a... */
	f->inst_ptr = fifo_intr_mmu_fault_inst_ptr_v(
		 gk20a_readl(g, fifo_intr_mmu_fault_inst_r(engine_id)));
	/* note: inst_ptr is a 40b phys addr.  */
	f->inst_ptr <<= fifo_intr_mmu_fault_inst_ptr_align_shift_v();
}

void gk20a_fifo_reset_engine(struct gk20a *g, u32 engine_id)
{
	gk20a_dbg_fn("");

	if (engine_id == top_device_info_type_enum_graphics_v()) {
		if (support_gk20a_pmu(g->dev) && g->elpg_enabled)
			gk20a_pmu_disable_elpg(g);
		/*HALT_PIPELINE method, halt GR engine*/
		if (gr_gk20a_halt_pipe(g))
			gk20a_err(dev_from_gk20a(g),
				"failed to HALT gr pipe");
		/* resetting engine using mc_enable_r() is not
		enough, we do full init sequence */
		gk20a_gr_reset(g);
		if (support_gk20a_pmu(g->dev) && g->elpg_enabled)
			gk20a_pmu_enable_elpg(g);
	}
	if (engine_id == top_device_info_type_enum_copy0_v())
		gk20a_reset(g, mc_enable_ce2_m());
}

static void gk20a_fifo_handle_chsw_fault(struct gk20a *g)
{
	u32 intr;

	intr = gk20a_readl(g, fifo_intr_chsw_error_r());
	gk20a_err(dev_from_gk20a(g), "chsw: %08x\n", intr);
	gk20a_fecs_dump_falcon_stats(g);
	gk20a_writel(g, fifo_intr_chsw_error_r(), intr);
}

static void gk20a_fifo_handle_dropped_mmu_fault(struct gk20a *g)
{
	struct device *dev = dev_from_gk20a(g);
	u32 fault_id = gk20a_readl(g, fifo_intr_mmu_fault_id_r());
	gk20a_err(dev, "dropped mmu fault (0x%08x)", fault_id);
}

static bool gk20a_fifo_should_defer_engine_reset(struct gk20a *g, u32 engine_id,
		struct fifo_mmu_fault_info_gk20a *f, bool fake_fault)
{
	/* channel recovery is only deferred if an sm debugger
	   is attached and has MMU debug mode is enabled */
	if (!gk20a_gr_sm_debugger_attached(g) ||
	    !g->ops.mm.is_debug_mode_enabled(g))
		return false;

	/* if this fault is fake (due to RC recovery), don't defer recovery */
	if (fake_fault)
		return false;

	if (engine_id != ENGINE_GR_GK20A ||
	    f->engine_subid_v != fifo_intr_mmu_fault_info_engine_subid_gpc_v())
		return false;

	return true;
}

/* caller must hold a channel reference */
static bool gk20a_fifo_set_ctx_mmu_error(struct gk20a *g,
		struct channel_gk20a *ch)
{
	bool verbose = true;
	if (!ch)
		return verbose;

	if (ch->error_notifier) {
		u32 err = ch->error_notifier->info32;
		if (ch->error_notifier->status == 0xffff) {
			/* If error code is already set, this mmu fault
			 * was triggered as part of recovery from other
			 * error condition.
			 * Don't overwrite error flag. */
			/* Fifo timeout debug spew is controlled by user */
			if (err == NVGPU_CHANNEL_FIFO_ERROR_IDLE_TIMEOUT)
				verbose = ch->timeout_debug_dump;
		} else {
			gk20a_set_error_notifier(ch,
				NVGPU_CHANNEL_FIFO_ERROR_MMU_ERR_FLT);
		}
	}
	/* mark channel as faulted */
	ch->has_timedout = true;
	wmb();
	/* unblock pending waits */
	wake_up(&ch->semaphore_wq);
	wake_up(&ch->notifier_wq);
	wake_up(&ch->submit_wq);
	return verbose;
}

static bool gk20a_fifo_set_ctx_mmu_error_ch(struct gk20a *g,
		struct channel_gk20a *ch)
{
	gk20a_err(dev_from_gk20a(g),
		"channel %d generated a mmu fault", ch->hw_chid);

	return gk20a_fifo_set_ctx_mmu_error(g, ch);
}

static bool gk20a_fifo_set_ctx_mmu_error_tsg(struct gk20a *g,
		struct tsg_gk20a *tsg)
{
	bool ret = true;
	struct channel_gk20a *ch = NULL;

	gk20a_err(dev_from_gk20a(g),
		"TSG %d generated a mmu fault", tsg->tsgid);

	mutex_lock(&tsg->ch_list_lock);
	list_for_each_entry(ch, &tsg->ch_list, ch_entry) {
		if (gk20a_channel_get(ch)) {
			if (!gk20a_fifo_set_ctx_mmu_error(g, ch))
				ret = false;
			gk20a_channel_put(ch);
		}
	}
	mutex_unlock(&tsg->ch_list_lock);

	return ret;
}

static void gk20a_fifo_abort_tsg(struct gk20a *g, u32 tsgid)
{
	struct tsg_gk20a *tsg = &g->fifo.tsg[tsgid];
	struct channel_gk20a *ch;

	mutex_lock(&tsg->ch_list_lock);
	list_for_each_entry(ch, &tsg->ch_list, ch_entry) {
		if (gk20a_channel_get(ch)) {
			gk20a_channel_abort(ch, false);
			gk20a_channel_put(ch);
		}
	}
	mutex_unlock(&tsg->ch_list_lock);
}

static bool gk20a_fifo_handle_mmu_fault(
	struct gk20a *g,
	u32 mmu_fault_engines, /* queried from HW if 0 */
	u32 hw_id, /* queried from HW if ~(u32)0 OR mmu_fault_engines == 0*/
	bool id_is_tsg)
{
	bool fake_fault;
	unsigned long fault_id;
	unsigned long engine_mmu_id;
	bool verbose = true;
	u32 grfifo_ctl;

	gk20a_dbg_fn("");

	g->fifo.deferred_reset_pending = false;

	/* Disable power management */
	if (support_gk20a_pmu(g->dev) && g->elpg_enabled)
		gk20a_pmu_disable_elpg(g);
	g->ops.clock_gating.slcg_gr_load_gating_prod(g,
			false);
	g->ops.clock_gating.slcg_perf_load_gating_prod(g,
			false);
	g->ops.clock_gating.slcg_ltc_load_gating_prod(g,
			false);
	gr_gk20a_init_elcg_mode(g, ELCG_RUN, ENGINE_GR_GK20A);
	gr_gk20a_init_elcg_mode(g, ELCG_RUN, ENGINE_CE2_GK20A);

	/* Disable fifo access */
	grfifo_ctl = gk20a_readl(g, gr_gpfifo_ctl_r());
	grfifo_ctl &= ~gr_gpfifo_ctl_semaphore_access_f(1);
	grfifo_ctl &= ~gr_gpfifo_ctl_access_f(1);

	gk20a_writel(g, gr_gpfifo_ctl_r(),
		grfifo_ctl | gr_gpfifo_ctl_access_f(0) |
		gr_gpfifo_ctl_semaphore_access_f(0));

	if (mmu_fault_engines) {
		fault_id = mmu_fault_engines;
		fake_fault = true;
	} else {
		fault_id = gk20a_readl(g, fifo_intr_mmu_fault_id_r());
		fake_fault = false;
		gk20a_debug_dump(g->dev);
	}


	/* go through all faulted engines */
	for_each_set_bit(engine_mmu_id, &fault_id, 32) {
		/* bits in fifo_intr_mmu_fault_id_r do not correspond 1:1 to
		 * engines. Convert engine_mmu_id to engine_id */
		u32 engine_id = gk20a_mmu_id_to_engine_id(engine_mmu_id);
		struct fifo_mmu_fault_info_gk20a f;
		struct channel_gk20a *ch = NULL;
		struct tsg_gk20a *tsg = NULL;
		struct channel_gk20a *referenced_channel = NULL;
		bool was_reset;
		/* read and parse engine status */
		u32 status = gk20a_readl(g, fifo_engine_status_r(engine_id));
		u32 ctx_status = fifo_engine_status_ctx_status_v(status);
		bool ctxsw = (ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_switch_v()
				|| ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_save_v()
				|| ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_load_v());

		get_exception_mmu_fault_info(g, engine_mmu_id, &f);
		trace_gk20a_mmu_fault(f.fault_hi_v,
				      f.fault_lo_v,
				      f.fault_info_v,
				      f.inst_ptr,
				      engine_id,
				      f.engine_subid_desc,
				      f.client_desc,
				      f.fault_type_desc);
		gk20a_err(dev_from_gk20a(g), "mmu fault on engine %d, "
			   "engine subid %d (%s), client %d (%s), "
			   "addr 0x%08x:0x%08x, type %d (%s), info 0x%08x,"
			   "inst_ptr 0x%llx\n",
			   engine_id,
			   f.engine_subid_v, f.engine_subid_desc,
			   f.client_v, f.client_desc,
			   f.fault_hi_v, f.fault_lo_v,
			   f.fault_type_v, f.fault_type_desc,
			   f.fault_info_v, f.inst_ptr);

		if (ctxsw) {
			gk20a_fecs_dump_falcon_stats(g);
			gk20a_err(dev_from_gk20a(g), "gr_status_r : 0x%x",
					gk20a_readl(g, gr_status_r()));
		}

		/* get the channel/TSG */
		if (fake_fault) {
			/* use next_id if context load is failing */
			u32 id, type;

			if (hw_id == ~(u32)0) {
				id = (ctx_status ==
				      fifo_engine_status_ctx_status_ctxsw_load_v()) ?
					fifo_engine_status_next_id_v(status) :
					fifo_engine_status_id_v(status);
				type = (ctx_status ==
					fifo_engine_status_ctx_status_ctxsw_load_v()) ?
					fifo_engine_status_next_id_type_v(status) :
					fifo_engine_status_id_type_v(status);
			} else {
				id = hw_id;
				type = id_is_tsg ?
					fifo_engine_status_id_type_tsgid_v() :
					fifo_engine_status_id_type_chid_v();
			}

			if (type == fifo_engine_status_id_type_tsgid_v())
				tsg = &g->fifo.tsg[id];
			else if (type == fifo_engine_status_id_type_chid_v()) {
				ch = &g->fifo.channel[id];
				referenced_channel = gk20a_channel_get(ch);
			}
		} else {
			/* read channel based on instruction pointer */
			ch = channel_from_inst_ptr(&g->fifo, f.inst_ptr);
			referenced_channel = ch;
		}

		if (ch && gk20a_is_channel_marked_as_tsg(ch))
			tsg = &g->fifo.tsg[ch->tsgid];

		/* check if engine reset should be deferred */
		if ((ch || tsg) && gk20a_fifo_should_defer_engine_reset(g,
				engine_id, &f, fake_fault)) {
			g->fifo.deferred_fault_engines = fault_id;

			/* handled during channel free */
			g->fifo.deferred_reset_pending = true;
		} else if (engine_id != ~0) {
			was_reset = mutex_is_locked(&g->fifo.gr_reset_mutex);
			mutex_lock(&g->fifo.gr_reset_mutex);
			/* if lock is already taken, a reset is taking place
			so no need to repeat */
			if (!was_reset)
				gk20a_fifo_reset_engine(g, engine_id);
			mutex_unlock(&g->fifo.gr_reset_mutex);
		}
		/* disable the channel/TSG from hw and increment
		 * syncpoints */

		if (tsg) {
			if (!g->fifo.deferred_reset_pending)
				verbose =
				       gk20a_fifo_set_ctx_mmu_error_tsg(g, tsg);

			gk20a_fifo_abort_tsg(g, tsg->tsgid);

			/* put back the ref taken early above */
			if (referenced_channel)
				gk20a_channel_put(ch);
		} else if (ch) {
			if (referenced_channel) {
				if (!g->fifo.deferred_reset_pending)
					verbose = gk20a_fifo_set_ctx_mmu_error_ch(g, ch);
				gk20a_channel_abort(ch, false);
				gk20a_channel_put(ch);
			} else {
				gk20a_err(dev_from_gk20a(g),
						"mmu error in freed channel %d",
						ch->hw_chid);
			}
		} else if (f.inst_ptr ==
				gk20a_mem_phys(&g->mm.bar1.inst_block)) {
			gk20a_err(dev_from_gk20a(g), "mmu fault from bar1");
		} else if (f.inst_ptr ==
				gk20a_mem_phys(&g->mm.pmu.inst_block)) {
			gk20a_err(dev_from_gk20a(g), "mmu fault from pmu");
		} else
			gk20a_err(dev_from_gk20a(g), "couldn't locate channel for mmu fault");
	}

	if (g->fifo.deferred_reset_pending) {
		gk20a_dbg(gpu_dbg_intr | gpu_dbg_gpu_dbg, "sm debugger attached,"
			   " deferring channel recovery to channel free");
		/* clear interrupt */
		gk20a_writel(g, fifo_intr_mmu_fault_id_r(), fault_id);
		goto exit_enable;
	}

	/* clear interrupt */
	gk20a_writel(g, fifo_intr_mmu_fault_id_r(), fault_id);

	/* resume scheduler */
	gk20a_writel(g, fifo_error_sched_disable_r(),
		     gk20a_readl(g, fifo_error_sched_disable_r()));

	/* Re-enable fifo access */
	gk20a_writel(g, gr_gpfifo_ctl_r(),
		     gr_gpfifo_ctl_access_enabled_f() |
		     gr_gpfifo_ctl_semaphore_access_enabled_f());

exit_enable:
	/* It is safe to enable ELPG again. */
	if (support_gk20a_pmu(g->dev) && g->elpg_enabled)
		gk20a_pmu_enable_elpg(g);
	return verbose;
}

static void gk20a_fifo_get_faulty_id_type(struct gk20a *g, int engine_id,
					  u32 *id, u32 *type)
{
	u32 status = gk20a_readl(g, fifo_engine_status_r(engine_id));
	u32 ctx_status = fifo_engine_status_ctx_status_v(status);

	/* use next_id if context load is failing */
	*id = (ctx_status ==
		fifo_engine_status_ctx_status_ctxsw_load_v()) ?
		fifo_engine_status_next_id_v(status) :
		fifo_engine_status_id_v(status);

	*type = (ctx_status ==
		fifo_engine_status_ctx_status_ctxsw_load_v()) ?
		fifo_engine_status_next_id_type_v(status) :
		fifo_engine_status_id_type_v(status);
}

static void gk20a_fifo_trigger_mmu_fault(struct gk20a *g,
		unsigned long engine_ids)
{
	unsigned long end_jiffies = jiffies +
		msecs_to_jiffies(gk20a_get_gr_idle_timeout(g));
	unsigned long delay = GR_IDLE_CHECK_DEFAULT;
	unsigned long engine_id;
	int ret;

	/* trigger faults for all bad engines */
	for_each_set_bit(engine_id, &engine_ids, 32) {
		if (engine_id > g->fifo.max_engines) {
			WARN_ON(true);
			break;
		}

		gk20a_writel(g, fifo_trigger_mmu_fault_r(engine_id),
			     fifo_trigger_mmu_fault_id_f(
			     gk20a_engine_id_to_mmu_id(engine_id)) |
			     fifo_trigger_mmu_fault_enable_f(1));
	}

	/* Wait for MMU fault to trigger */
	ret = -EBUSY;
	do {
		if (gk20a_readl(g, fifo_intr_0_r()) &
				fifo_intr_0_mmu_fault_pending_f()) {
			ret = 0;
			break;
		}

		usleep_range(delay, delay * 2);
		delay = min_t(u32, delay << 1, GR_IDLE_CHECK_MAX);
	} while (time_before(jiffies, end_jiffies) ||
			!tegra_platform_is_silicon());

	if (ret)
		gk20a_err(dev_from_gk20a(g), "mmu fault timeout");

	/* release mmu fault trigger */
	for_each_set_bit(engine_id, &engine_ids, 32)
		gk20a_writel(g, fifo_trigger_mmu_fault_r(engine_id), 0);
}

static u32 gk20a_fifo_engines_on_id(struct gk20a *g, u32 id, bool is_tsg)
{
	int i;
	u32 engines = 0;

	for (i = 0; i < g->fifo.max_engines; i++) {
		u32 status = gk20a_readl(g, fifo_engine_status_r(i));
		u32 ctx_status =
			fifo_engine_status_ctx_status_v(status);
		u32 ctx_id = (ctx_status ==
			fifo_engine_status_ctx_status_ctxsw_load_v()) ?
			fifo_engine_status_next_id_v(status) :
			fifo_engine_status_id_v(status);
		u32 type = (ctx_status ==
			fifo_engine_status_ctx_status_ctxsw_load_v()) ?
			fifo_engine_status_next_id_type_v(status) :
			fifo_engine_status_id_type_v(status);
		bool busy = fifo_engine_status_engine_v(status) ==
			fifo_engine_status_engine_busy_v();
		if (busy && ctx_id == id) {
			if ((is_tsg && type ==
					fifo_engine_status_id_type_tsgid_v()) ||
				    (!is_tsg && type ==
					fifo_engine_status_id_type_chid_v()))
				engines |= BIT(i);
		}
	}

	return engines;
}

void gk20a_fifo_recover_ch(struct gk20a *g, u32 hw_chid, bool verbose)
{
	u32 engines;

	/* stop context switching to prevent engine assignments from
	   changing until channel is recovered */
	mutex_lock(&g->dbg_sessions_lock);
	gr_gk20a_disable_ctxsw(g);

	engines = gk20a_fifo_engines_on_id(g, hw_chid, false);

	if (engines)
		gk20a_fifo_recover(g, engines, hw_chid, false, true, verbose);
	else {
		struct channel_gk20a *ch = &g->fifo.channel[hw_chid];

		if (gk20a_channel_get(ch)) {
			gk20a_channel_abort(ch, false);

			if (gk20a_fifo_set_ctx_mmu_error_ch(g, ch))
				gk20a_debug_dump(g->dev);

			gk20a_channel_put(ch);
		}
	}

	gr_gk20a_enable_ctxsw(g);
	mutex_unlock(&g->dbg_sessions_lock);
}

void gk20a_fifo_recover_tsg(struct gk20a *g, u32 tsgid, bool verbose)
{
	u32 engines;

	/* stop context switching to prevent engine assignments from
	   changing until TSG is recovered */
	mutex_lock(&g->dbg_sessions_lock);
	gr_gk20a_disable_ctxsw(g);

	engines = gk20a_fifo_engines_on_id(g, tsgid, true);

	if (engines)
		gk20a_fifo_recover(g, engines, tsgid, true, true, verbose);
	else {
		struct tsg_gk20a *tsg = &g->fifo.tsg[tsgid];

		if (gk20a_fifo_set_ctx_mmu_error_tsg(g, tsg))
			gk20a_debug_dump(g->dev);

		gk20a_fifo_abort_tsg(g, tsgid);
	}

	gr_gk20a_enable_ctxsw(g);
	mutex_unlock(&g->dbg_sessions_lock);
}

void gk20a_fifo_recover(struct gk20a *g, u32 __engine_ids,
			u32 hw_id, bool id_is_tsg,
			bool id_is_known, bool verbose)
{
	unsigned long engine_id, i;
	unsigned long _engine_ids = __engine_ids;
	unsigned long engine_ids = 0;
	u32 val;
	u32 mmu_fault_engines = 0;
	u32 ref_type;
	u32 ref_id;
	u32 ref_id_is_tsg = false;

	if (verbose)
		gk20a_debug_dump(g->dev);

	if (g->ops.ltc.flush)
		g->ops.ltc.flush(g);

	if (id_is_known) {
		engine_ids = gk20a_fifo_engines_on_id(g, hw_id, id_is_tsg);
		ref_id = hw_id;
		ref_type = id_is_tsg ?
			fifo_engine_status_id_type_tsgid_v() :
			fifo_engine_status_id_type_chid_v();
		ref_id_is_tsg = id_is_tsg;
		/* atleast one engine will get passed during sched err*/
		engine_ids |= __engine_ids;
		for_each_set_bit(engine_id, &engine_ids, 32) {
			mmu_fault_engines |=
				BIT(gk20a_engine_id_to_mmu_id(engine_id));
		}
	} else {
		/* store faulted engines in advance */
		for_each_set_bit(engine_id, &_engine_ids, 32) {
			gk20a_fifo_get_faulty_id_type(g, engine_id, &ref_id,
						      &ref_type);
			if (ref_type == fifo_engine_status_id_type_tsgid_v())
				ref_id_is_tsg = true;
			else
				ref_id_is_tsg = false;
			/* Reset *all* engines that use the
			 * same channel as faulty engine */
			for (i = 0; i < g->fifo.max_engines; i++) {
				u32 type;
				u32 id;
				gk20a_fifo_get_faulty_id_type(g, i, &id, &type);
				if (ref_type == type && ref_id == id) {
					engine_ids |= BIT(i);
					mmu_fault_engines |=
					BIT(gk20a_engine_id_to_mmu_id(i));
				}
			}
		}
	}

	if (mmu_fault_engines) {
		/*
		 * sched error prevents recovery, and ctxsw error will retrigger
		 * every 100ms. Disable the sched error to allow recovery.
		 */
		val = gk20a_readl(g, fifo_intr_en_0_r());
		val &= ~(fifo_intr_en_0_sched_error_m() |
			fifo_intr_en_0_mmu_fault_m());
		gk20a_writel(g, fifo_intr_en_0_r(), val);
		gk20a_writel(g, fifo_intr_0_r(),
				fifo_intr_0_sched_error_reset_f());

		g->ops.fifo.trigger_mmu_fault(g, engine_ids);
		gk20a_fifo_handle_mmu_fault(g, mmu_fault_engines, ref_id,
				ref_id_is_tsg);

		val = gk20a_readl(g, fifo_intr_en_0_r());
		val |= fifo_intr_en_0_mmu_fault_f(1)
			| fifo_intr_en_0_sched_error_f(1);
		gk20a_writel(g, fifo_intr_en_0_r(), val);
	}
}

/* force reset channel and tsg (if it's part of one) */
int gk20a_fifo_force_reset_ch(struct channel_gk20a *ch, bool verbose)
{
	struct tsg_gk20a *tsg = NULL;
	struct channel_gk20a *ch_tsg = NULL;
	struct gk20a *g = ch->g;

	if (gk20a_is_channel_marked_as_tsg(ch)) {
		tsg = &g->fifo.tsg[ch->hw_chid];

		mutex_lock(&tsg->ch_list_lock);

		list_for_each_entry(ch_tsg, &tsg->ch_list, ch_entry) {
			if (gk20a_channel_get(ch_tsg)) {
				gk20a_set_error_notifier(ch_tsg,
				       NVGPU_CHANNEL_RESETCHANNEL_VERIF_ERROR);
				gk20a_channel_put(ch_tsg);
			}
		}

		mutex_unlock(&tsg->ch_list_lock);
		gk20a_fifo_recover_tsg(g, ch->tsgid, verbose);
	} else {
		gk20a_set_error_notifier(ch,
			NVGPU_CHANNEL_RESETCHANNEL_VERIF_ERROR);
		gk20a_fifo_recover_ch(g, ch->hw_chid, verbose);
	}

	return 0;
}

static bool gk20a_fifo_handle_sched_error(struct gk20a *g)
{
	u32 sched_error;
	u32 engine_id;
	int id = -1;
	bool non_chid = false;
	bool ret = false;
	u32 mailbox2;
	/* read the scheduler error register */
	sched_error = gk20a_readl(g, fifo_intr_sched_error_r());

	for (engine_id = 0; engine_id < g->fifo.max_engines; engine_id++) {
		u32 status = gk20a_readl(g, fifo_engine_status_r(engine_id));
		u32 ctx_status = fifo_engine_status_ctx_status_v(status);
		bool failing_engine;

		/* we are interested in busy engines */
		failing_engine = fifo_engine_status_engine_v(status) ==
			fifo_engine_status_engine_busy_v();

		/* ..that are doing context switch */
		failing_engine = failing_engine &&
			(ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_switch_v()
			|| ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_save_v()
			|| ctx_status ==
				fifo_engine_status_ctx_status_ctxsw_load_v());

		if (!failing_engine)
			continue;
		if (ctx_status ==
		fifo_engine_status_ctx_status_ctxsw_load_v()) {
			id = fifo_engine_status_next_id_v(status);
			non_chid = fifo_pbdma_status_id_type_v(status)
				!= fifo_pbdma_status_id_type_chid_v();
		} else if (ctx_status ==
		fifo_engine_status_ctx_status_ctxsw_switch_v()) {
			mailbox2 = gk20a_readl(g, gr_fecs_ctxsw_mailbox_r(2));
			if (mailbox2 & FECS_METHOD_WFI_RESTORE)
				id = fifo_engine_status_next_id_v(status);
			else
				id = fifo_engine_status_id_v(status);
		} else {
			id = fifo_engine_status_id_v(status);
		}
		break;
	}

	/* could not find the engine - should never happen */
	if (unlikely(engine_id >= g->fifo.max_engines)) {
		gk20a_err(dev_from_gk20a(g), "fifo sched error : 0x%08x, failed to find engine\n",
			sched_error);
		ret = false;
		goto err;
	}

	if (fifo_intr_sched_error_code_f(sched_error) ==
			fifo_intr_sched_error_code_ctxsw_timeout_v()) {
		struct fifo_gk20a *f = &g->fifo;
		struct channel_gk20a *ch = &f->channel[id];

		if (non_chid) {
			gk20a_fifo_recover(g, BIT(engine_id), id, true,
					true, true);
			ret = true;
			goto err;
		}

		if (!gk20a_channel_get(ch))
			goto err;

		if (gk20a_channel_update_and_check_timeout(ch,
			GRFIFO_TIMEOUT_CHECK_PERIOD_US / 1000)) {
			gk20a_set_error_notifier(ch,
				NVGPU_CHANNEL_FIFO_ERROR_IDLE_TIMEOUT);
			gk20a_err(dev_from_gk20a(g),
				"fifo sched ctxsw timeout error:"
				"engine = %u, ch = %d", engine_id, id);
			gk20a_gr_debug_dump(g->dev);
			gk20a_fifo_recover(g, BIT(engine_id), id, false,
				true, ch->timeout_debug_dump);
			ret = true;
		} else {
			gk20a_dbg_info(
				"fifo is waiting for ctx switch for %d ms,"
				"ch = %d\n",
				ch->timeout_accumulated_ms,
				id);
			ret = false;
		}
		gk20a_channel_put(ch);
		return ret;
	}

	gk20a_err(dev_from_gk20a(g), "fifo sched error : 0x%08x, engine=%u, %s=%d",
		sched_error, engine_id, non_chid ? "non-ch" : "ch", id);

err:
	return ret;
}

static u32 fifo_error_isr(struct gk20a *g, u32 fifo_intr)
{
	bool print_channel_reset_log = false;
	struct device *dev = dev_from_gk20a(g);
	u32 handled = 0;

	gk20a_dbg_fn("");

	if (fifo_intr & fifo_intr_0_pio_error_pending_f()) {
		/* pio mode is unused.  this shouldn't happen, ever. */
		/* should we clear it or just leave it pending? */
		gk20a_err(dev, "fifo pio error!\n");
		BUG_ON(1);
	}

	if (fifo_intr & fifo_intr_0_bind_error_pending_f()) {
		u32 bind_error = gk20a_readl(g, fifo_intr_bind_error_r());
		gk20a_err(dev, "fifo bind error: 0x%08x", bind_error);
		print_channel_reset_log = true;
		handled |= fifo_intr_0_bind_error_pending_f();
	}

	if (fifo_intr & fifo_intr_0_sched_error_pending_f()) {
		print_channel_reset_log = gk20a_fifo_handle_sched_error(g);
		handled |= fifo_intr_0_sched_error_pending_f();
	}

	if (fifo_intr & fifo_intr_0_chsw_error_pending_f()) {
		gk20a_fifo_handle_chsw_fault(g);
		handled |= fifo_intr_0_chsw_error_pending_f();
	}

	if (fifo_intr & fifo_intr_0_mmu_fault_pending_f()) {
		print_channel_reset_log =
			gk20a_fifo_handle_mmu_fault(g, 0, ~(u32)0, false);
		handled |= fifo_intr_0_mmu_fault_pending_f();
	}

	if (fifo_intr & fifo_intr_0_dropped_mmu_fault_pending_f()) {
		gk20a_fifo_handle_dropped_mmu_fault(g);
		handled |= fifo_intr_0_dropped_mmu_fault_pending_f();
	}

	print_channel_reset_log = !g->fifo.deferred_reset_pending
			&& print_channel_reset_log;

	if (print_channel_reset_log) {
		int engine_id;
		gk20a_err(dev_from_gk20a(g),
			   "channel reset initiated from %s; intr=0x%08x",
			   __func__, fifo_intr);
		for (engine_id = 0;
		     engine_id < g->fifo.max_engines;
		     engine_id++) {
			gk20a_dbg_fn("enum:%d -> engine_id:%d", engine_id,
				g->fifo.engine_info[engine_id].engine_id);
			fifo_pbdma_exception_status(g,
					&g->fifo.engine_info[engine_id]);
			fifo_engine_exception_status(g,
					&g->fifo.engine_info[engine_id]);
		}
	}

	return handled;
}

static inline void gk20a_fifo_reset_pbdma_header(struct gk20a *g, int pbdma_id)
{
	gk20a_writel(g, pbdma_pb_header_r(pbdma_id),
			pbdma_pb_header_first_true_f() |
			pbdma_pb_header_type_non_inc_f());
}

static inline void gk20a_fifo_reset_pbdma_method(struct gk20a *g, int pbdma_id,
						int pbdma_method_index)
{
	u32 pbdma_method_stride;
	u32 pbdma_method_reg;

	pbdma_method_stride = pbdma_method1_r(pbdma_id) -
				pbdma_method0_r(pbdma_id);

	pbdma_method_reg = pbdma_method0_r(pbdma_id) +
		(pbdma_method_index * pbdma_method_stride);

	gk20a_writel(g, pbdma_method_reg,
			pbdma_method0_valid_true_f() |
			pbdma_method0_first_true_f() |
			pbdma_method0_addr_f(
			     pbdma_udma_nop_r() >> 2));
}

static bool gk20a_fifo_is_sw_method_subch(struct gk20a *g, int pbdma_id,
						int pbdma_method_index)
{
	u32 pbdma_method_stride;
	u32 pbdma_method_reg, pbdma_method_subch;

	pbdma_method_stride = pbdma_method1_r(pbdma_id) -
				pbdma_method0_r(pbdma_id);

	pbdma_method_reg = pbdma_method0_r(pbdma_id) +
			(pbdma_method_index * pbdma_method_stride);

	pbdma_method_subch = pbdma_method0_subch_v(
			gk20a_readl(g, pbdma_method_reg));

	if (pbdma_method_subch == 5 || pbdma_method_subch == 6 ||
				       pbdma_method_subch == 7)
		return true;

	return false;
}

static u32 gk20a_fifo_handle_pbdma_intr(struct device *dev,
					struct gk20a *g,
					struct fifo_gk20a *f,
					u32 pbdma_id)
{
	u32 pbdma_intr_0 = gk20a_readl(g, pbdma_intr_0_r(pbdma_id));
	u32 pbdma_intr_1 = gk20a_readl(g, pbdma_intr_1_r(pbdma_id));
	u32 handled = 0;
	bool reset = false;
	int i;

	gk20a_dbg_fn("");

	gk20a_dbg(gpu_dbg_intr, "pbdma id intr pending %d %08x %08x", pbdma_id,
			pbdma_intr_0, pbdma_intr_1);
	if (pbdma_intr_0) {
		if ((f->intr.pbdma.device_fatal_0 |
		     f->intr.pbdma.channel_fatal_0 |
		     f->intr.pbdma.restartable_0) & pbdma_intr_0) {
			gk20a_err(dev_from_gk20a(g),
				"pbdma_intr_0(%d):0x%08x PBH: %08x SHADOW: %08x M0: %08x %08x %08x %08x",
				pbdma_id, pbdma_intr_0,
				gk20a_readl(g, pbdma_pb_header_r(pbdma_id)),
				gk20a_readl(g, pbdma_hdr_shadow_r(pbdma_id)),
				gk20a_readl(g, pbdma_method0_r(pbdma_id)),
				gk20a_readl(g, pbdma_method1_r(pbdma_id)),
				gk20a_readl(g, pbdma_method2_r(pbdma_id)),
				gk20a_readl(g, pbdma_method3_r(pbdma_id))
				);
			reset = true;
			handled |= ((f->intr.pbdma.device_fatal_0 |
				     f->intr.pbdma.channel_fatal_0 |
				     f->intr.pbdma.restartable_0) &
				    pbdma_intr_0);
		}

		if (pbdma_intr_0 & pbdma_intr_0_acquire_pending_f()) {
			u32 val = gk20a_readl(g, pbdma_acquire_r(pbdma_id));
			val &= ~pbdma_acquire_timeout_en_enable_f();
			gk20a_writel(g, pbdma_acquire_r(pbdma_id), val);
		}

		if (pbdma_intr_0 & pbdma_intr_0_pbentry_pending_f()) {
			gk20a_fifo_reset_pbdma_header(g, pbdma_id);
			gk20a_fifo_reset_pbdma_method(g, pbdma_id, 0);
			reset = true;
		}

		if (pbdma_intr_0 & pbdma_intr_0_method_pending_f()) {
			gk20a_fifo_reset_pbdma_method(g, pbdma_id, 0);
			reset = true;
		}

		if (pbdma_intr_0 & pbdma_intr_0_device_pending_f()) {
			gk20a_fifo_reset_pbdma_header(g, pbdma_id);

			for (i = 0; i < 4; i++) {
				if (gk20a_fifo_is_sw_method_subch(g,
						pbdma_id, i))
					gk20a_fifo_reset_pbdma_method(g,
							pbdma_id, i);
			}
			reset = true;
		}

		gk20a_writel(g, pbdma_intr_0_r(pbdma_id), pbdma_intr_0);
	}

	/* all intrs in _intr_1 are "host copy engine" related,
	 * which gk20a doesn't have. for now just make them channel fatal. */
	if (pbdma_intr_1) {
		dev_err(dev, "channel hce error: pbdma_intr_1(%d): 0x%08x",
			pbdma_id, pbdma_intr_1);
		reset = true;
		gk20a_writel(g, pbdma_intr_1_r(pbdma_id), pbdma_intr_1);
	}

	if (reset) {
		/* Remove the channel from runlist */
		u32 status = gk20a_readl(g, fifo_pbdma_status_r(pbdma_id));
		u32 id = fifo_pbdma_status_id_v(status);
		if (fifo_pbdma_status_id_type_v(status)
				== fifo_pbdma_status_id_type_chid_v()) {
			struct channel_gk20a *ch = &f->channel[id];

			if (gk20a_channel_get(ch)) {
				gk20a_set_error_notifier(ch,
						NVGPU_CHANNEL_PBDMA_ERROR);
				gk20a_fifo_recover_ch(g, id, true);
				gk20a_channel_put(ch);
			}
		} else if (fifo_pbdma_status_id_type_v(status)
				== fifo_pbdma_status_id_type_tsgid_v()) {
			struct tsg_gk20a *tsg = &f->tsg[id];
			struct channel_gk20a *ch = NULL;

			mutex_lock(&tsg->ch_list_lock);
			list_for_each_entry(ch, &tsg->ch_list, ch_entry) {
				if (gk20a_channel_get(ch)) {
					gk20a_set_error_notifier(ch,
						NVGPU_CHANNEL_PBDMA_ERROR);
					gk20a_channel_put(ch);
				}
			}
			mutex_unlock(&tsg->ch_list_lock);
			gk20a_fifo_recover_tsg(g, id, true);
		}
	}

	return handled;
}

static u32 fifo_channel_isr(struct gk20a *g, u32 fifo_intr)
{
	gk20a_channel_semaphore_wakeup(g);
	return fifo_intr_0_channel_intr_pending_f();
}


static u32 fifo_pbdma_isr(struct gk20a *g, u32 fifo_intr)
{
	struct device *dev = dev_from_gk20a(g);
	struct fifo_gk20a *f = &g->fifo;
	u32 clear_intr = 0, i;
	u32 pbdma_pending = gk20a_readl(g, fifo_intr_pbdma_id_r());

	for (i = 0; i < fifo_intr_pbdma_id_status__size_1_v(); i++) {
		if (fifo_intr_pbdma_id_status_f(pbdma_pending, i)) {
			gk20a_dbg(gpu_dbg_intr, "pbdma id %d intr pending", i);
			clear_intr |=
				gk20a_fifo_handle_pbdma_intr(dev, g, f, i);
		}
	}
	return fifo_intr_0_pbdma_intr_pending_f();
}

void gk20a_fifo_isr(struct gk20a *g)
{
	u32 error_intr_mask =
		fifo_intr_0_bind_error_pending_f() |
		fifo_intr_0_sched_error_pending_f() |
		fifo_intr_0_chsw_error_pending_f() |
		fifo_intr_0_fb_flush_timeout_pending_f() |
		fifo_intr_0_dropped_mmu_fault_pending_f() |
		fifo_intr_0_mmu_fault_pending_f() |
		fifo_intr_0_lb_error_pending_f() |
		fifo_intr_0_pio_error_pending_f();

	u32 fifo_intr = gk20a_readl(g, fifo_intr_0_r());
	u32 clear_intr = 0;

	if (g->fifo.sw_ready) {
		/* note we're not actually in an "isr", but rather
		 * in a threaded interrupt context... */
		mutex_lock(&g->fifo.intr.isr.mutex);

		gk20a_dbg(gpu_dbg_intr, "fifo isr %08x\n", fifo_intr);

		/* handle runlist update */
		if (fifo_intr & fifo_intr_0_runlist_event_pending_f()) {
			gk20a_fifo_handle_runlist_event(g);
			clear_intr |= fifo_intr_0_runlist_event_pending_f();
		}
		if (fifo_intr & fifo_intr_0_pbdma_intr_pending_f())
			clear_intr |= fifo_pbdma_isr(g, fifo_intr);

		if (unlikely(fifo_intr & error_intr_mask))
			clear_intr = fifo_error_isr(g, fifo_intr);

		mutex_unlock(&g->fifo.intr.isr.mutex);
	}
	gk20a_writel(g, fifo_intr_0_r(), clear_intr);

	return;
}

void gk20a_fifo_nonstall_isr(struct gk20a *g)
{
	u32 fifo_intr = gk20a_readl(g, fifo_intr_0_r());
	u32 clear_intr = 0;

	gk20a_dbg(gpu_dbg_intr, "fifo nonstall isr %08x\n", fifo_intr);

	if (fifo_intr & fifo_intr_0_channel_intr_pending_f())
		clear_intr |= fifo_channel_isr(g, fifo_intr);

	gk20a_writel(g, fifo_intr_0_r(), clear_intr);

	return;
}

static int __locked_fifo_preempt(struct gk20a *g, u32 id, bool is_tsg)
{
	u32 delay = GR_IDLE_CHECK_DEFAULT;
	unsigned long end_jiffies = jiffies
		+ msecs_to_jiffies(gk20a_get_gr_idle_timeout(g));
	u32 ret = 0;

	gk20a_dbg_fn("%d", id);

	/* issue preempt */
	if (is_tsg)
		gk20a_writel(g, fifo_preempt_r(),
			fifo_preempt_id_f(id) |
			fifo_preempt_type_tsg_f());
	else
		gk20a_writel(g, fifo_preempt_r(),
			fifo_preempt_chid_f(id) |
			fifo_preempt_type_channel_f());

	gk20a_dbg_fn("%d", id);
	/* wait for preempt */
	ret = -EBUSY;
	do {
		if (!(gk20a_readl(g, fifo_preempt_r()) &
			fifo_preempt_pending_true_f())) {
			ret = 0;
			break;
		}

		usleep_range(delay, delay * 2);
		delay = min_t(u32, delay << 1, GR_IDLE_CHECK_MAX);
	} while (time_before(jiffies, end_jiffies) ||
			!tegra_platform_is_silicon());

	gk20a_dbg_fn("%d", id);
	if (ret) {
		if (is_tsg) {
			struct tsg_gk20a *tsg = &g->fifo.tsg[id];
			struct channel_gk20a *ch = NULL;

			gk20a_err(dev_from_gk20a(g),
				"preempt TSG %d timeout\n", id);

			mutex_lock(&tsg->ch_list_lock);
			list_for_each_entry(ch, &tsg->ch_list, ch_entry) {
				if (!gk20a_channel_get(ch))
					continue;
				gk20a_set_error_notifier(ch,
					NVGPU_CHANNEL_FIFO_ERROR_IDLE_TIMEOUT);
				gk20a_channel_put(ch);
			}
			mutex_unlock(&tsg->ch_list_lock);
			gk20a_fifo_recover_tsg(g, id, true);
		} else {
			struct channel_gk20a *ch = &g->fifo.channel[id];

			gk20a_err(dev_from_gk20a(g),
				"preempt channel %d timeout\n", id);

			if (gk20a_channel_get(ch)) {
				gk20a_set_error_notifier(ch,
						NVGPU_CHANNEL_FIFO_ERROR_IDLE_TIMEOUT);
				gk20a_fifo_recover_ch(g, id, true);
				gk20a_channel_put(ch);
			}
		}
	}

	return ret;
}

int gk20a_fifo_preempt_channel(struct gk20a *g, u32 hw_chid)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 ret = 0;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	u32 mutex_ret = 0;
	u32 i;

	gk20a_dbg_fn("%d", hw_chid);

	/* we have no idea which runlist we are using. lock all */
	for (i = 0; i < g->fifo.max_runlists; i++)
		mutex_lock(&f->runlist_info[i].mutex);

	mutex_ret = pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	ret = __locked_fifo_preempt(g, hw_chid, false);

	if (!mutex_ret)
		pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	for (i = 0; i < g->fifo.max_runlists; i++)
		mutex_unlock(&f->runlist_info[i].mutex);

	return ret;
}

int gk20a_fifo_preempt_tsg(struct gk20a *g, u32 tsgid)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 ret = 0;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	u32 mutex_ret = 0;
	u32 i;

	gk20a_dbg_fn("%d", tsgid);

	/* we have no idea which runlist we are using. lock all */
	for (i = 0; i < g->fifo.max_runlists; i++)
		mutex_lock(&f->runlist_info[i].mutex);

	mutex_ret = pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	ret = __locked_fifo_preempt(g, tsgid, true);

	if (!mutex_ret)
		pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	for (i = 0; i < g->fifo.max_runlists; i++)
		mutex_unlock(&f->runlist_info[i].mutex);

	return ret;
}

int gk20a_fifo_preempt(struct gk20a *g, struct channel_gk20a *ch)
{
	int err;

	if (gk20a_is_channel_marked_as_tsg(ch))
		err = gk20a_fifo_preempt_tsg(ch->g, ch->tsgid);
	else
		err = gk20a_fifo_preempt_channel(ch->g, ch->hw_chid);

	return err;
}

int gk20a_fifo_enable_engine_activity(struct gk20a *g,
				struct fifo_engine_info_gk20a *eng_info)
{
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	u32 mutex_ret;
	u32 enable;

	gk20a_dbg_fn("");

	mutex_ret = pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	enable = gk20a_readl(g, fifo_sched_disable_r());
	enable &= ~(fifo_sched_disable_true_v() >> eng_info->runlist_id);
	gk20a_writel(g, fifo_sched_disable_r(), enable);

	if (!mutex_ret)
		pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	gk20a_dbg_fn("done");
	return 0;
}

int gk20a_fifo_enable_all_engine_activity(struct gk20a *g)
{
	int i;
	int err = 0, ret = 0;

	for (i = 0; i < g->fifo.max_engines; i++) {
		err = gk20a_fifo_enable_engine_activity(g,
				&g->fifo.engine_info[i]);
		if (err) {
			gk20a_err(dev_from_gk20a(g),
				"failed to enable engine %d activity\n", i);
			ret = err;
		}
	}

	return ret;
}

int gk20a_fifo_disable_engine_activity(struct gk20a *g,
				struct fifo_engine_info_gk20a *eng_info,
				bool wait_for_idle)
{
	u32 gr_stat, pbdma_stat, chan_stat, eng_stat, ctx_stat;
	u32 pbdma_chid = ~0, engine_chid = ~0, disable;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	u32 mutex_ret;
	u32 err = 0;

	gk20a_dbg_fn("");

	gr_stat =
		gk20a_readl(g, fifo_engine_status_r(eng_info->engine_id));
	if (fifo_engine_status_engine_v(gr_stat) ==
	    fifo_engine_status_engine_busy_v() && !wait_for_idle)
		return -EBUSY;

	mutex_ret = pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	disable = gk20a_readl(g, fifo_sched_disable_r());
	disable = set_field(disable,
			fifo_sched_disable_runlist_m(eng_info->runlist_id),
			fifo_sched_disable_runlist_f(fifo_sched_disable_true_v(),
				eng_info->runlist_id));
	gk20a_writel(g, fifo_sched_disable_r(), disable);

	/* chid from pbdma status */
	pbdma_stat = gk20a_readl(g, fifo_pbdma_status_r(eng_info->pbdma_id));
	chan_stat  = fifo_pbdma_status_chan_status_v(pbdma_stat);
	if (chan_stat == fifo_pbdma_status_chan_status_valid_v() ||
	    chan_stat == fifo_pbdma_status_chan_status_chsw_save_v())
		pbdma_chid = fifo_pbdma_status_id_v(pbdma_stat);
	else if (chan_stat == fifo_pbdma_status_chan_status_chsw_load_v() ||
		 chan_stat == fifo_pbdma_status_chan_status_chsw_switch_v())
		pbdma_chid = fifo_pbdma_status_next_id_v(pbdma_stat);

	if (pbdma_chid != ~0) {
		err = g->ops.fifo.preempt_channel(g, pbdma_chid);
		if (err)
			goto clean_up;
	}

	/* chid from engine status */
	eng_stat = gk20a_readl(g, fifo_engine_status_r(eng_info->engine_id));
	ctx_stat  = fifo_engine_status_ctx_status_v(eng_stat);
	if (ctx_stat == fifo_engine_status_ctx_status_valid_v() ||
	    ctx_stat == fifo_engine_status_ctx_status_ctxsw_save_v())
		engine_chid = fifo_engine_status_id_v(eng_stat);
	else if (ctx_stat == fifo_engine_status_ctx_status_ctxsw_load_v() ||
		 ctx_stat == fifo_engine_status_ctx_status_ctxsw_switch_v())
		engine_chid = fifo_engine_status_next_id_v(eng_stat);

	if (engine_chid != ~0 && engine_chid != pbdma_chid) {
		err = g->ops.fifo.preempt_channel(g, engine_chid);
		if (err)
			goto clean_up;
	}

clean_up:
	if (!mutex_ret)
		pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	if (err) {
		gk20a_dbg_fn("failed");
		if (gk20a_fifo_enable_engine_activity(g, eng_info))
			gk20a_err(dev_from_gk20a(g),
				"failed to enable gr engine activity\n");
	} else {
		gk20a_dbg_fn("done");
	}
	return err;
}

int gk20a_fifo_disable_all_engine_activity(struct gk20a *g,
				bool wait_for_idle)
{
	int i;
	int err = 0, ret = 0;

	for (i = 0; i < g->fifo.max_engines; i++) {
		err = gk20a_fifo_disable_engine_activity(g,
				&g->fifo.engine_info[i],
				wait_for_idle);
		if (err) {
			gk20a_err(dev_from_gk20a(g),
				"failed to disable engine %d activity\n", i);
			ret = err;
			break;
		}
	}

	if (err) {
		while (--i >= 0) {
			err = gk20a_fifo_enable_engine_activity(g,
						&g->fifo.engine_info[i]);
			if (err)
				gk20a_err(dev_from_gk20a(g),
				 "failed to re-enable engine %d activity\n", i);
		}
	}

	return ret;
}

static void gk20a_fifo_runlist_reset_engines(struct gk20a *g, u32 runlist_id)
{
	struct fifo_gk20a *f = &g->fifo;
	u32 engines = 0;
	int i;

	for (i = 0; i < f->max_engines; i++) {
		u32 status = gk20a_readl(g, fifo_engine_status_r(i));
		bool engine_busy = fifo_engine_status_engine_v(status) ==
			fifo_engine_status_engine_busy_v();

		if (engine_busy &&
		    (f->engine_info[i].runlist_id == runlist_id))
			engines |= BIT(i);
	}

	if (engines)
		gk20a_fifo_recover(g, engines, ~(u32)0, false, false, true);
}

static int gk20a_fifo_runlist_wait_pending(struct gk20a *g, u32 runlist_id)
{
	struct fifo_runlist_info_gk20a *runlist;
	unsigned long end_jiffies = jiffies +
		msecs_to_jiffies(gk20a_get_gr_idle_timeout(g));
	unsigned long delay = GR_IDLE_CHECK_DEFAULT;
	int ret = -ETIMEDOUT;

	runlist = &g->fifo.runlist_info[runlist_id];
	do {
		if ((gk20a_readl(g, fifo_eng_runlist_r(runlist_id)) &
				fifo_eng_runlist_pending_true_f()) == 0) {
			ret = 0;
			break;
		}

		usleep_range(delay, delay * 2);
		delay = min_t(u32, delay << 1, GR_IDLE_CHECK_MAX);
	} while (time_before(jiffies, end_jiffies) ||
		 !tegra_platform_is_silicon());

	return ret;
}

static int gk20a_fifo_update_runlist_locked(struct gk20a *g, u32 runlist_id,
					    u32 hw_chid, bool add,
					    bool wait_for_finish)
{
	u32 ret = 0;
	struct fifo_gk20a *f = &g->fifo;
	struct fifo_runlist_info_gk20a *runlist = NULL;
	u32 *runlist_entry_base = NULL;
	u32 *runlist_entry = NULL;
	phys_addr_t runlist_pa;
	u32 old_buf, new_buf;
	u32 chid, tsgid;
	struct channel_gk20a *ch = NULL;
	struct tsg_gk20a *tsg = NULL;
	u32 count = 0;
	u32 count_channels_in_tsg;
	runlist = &f->runlist_info[runlist_id];

	/* valid channel, add/remove it from active list.
	   Otherwise, keep active list untouched for suspend/resume. */
	if (hw_chid != ~0) {
		ch = &f->channel[hw_chid];
		if (gk20a_is_channel_marked_as_tsg(ch))
			tsg = &f->tsg[ch->tsgid];

		if (add) {
			if (test_and_set_bit(hw_chid,
				runlist->active_channels) == 1)
				return 0;
			if (tsg && ++tsg->num_active_channels)
				set_bit(f->channel[hw_chid].tsgid,
					runlist->active_tsgs);
		} else {
			if (test_and_clear_bit(hw_chid,
				runlist->active_channels) == 0)
				return 0;
			if (tsg && --tsg->num_active_channels == 0)
				clear_bit(f->channel[hw_chid].tsgid,
					runlist->active_tsgs);
		}
	}

	old_buf = runlist->cur_buffer;
	new_buf = !runlist->cur_buffer;

	gk20a_dbg_info("runlist_id : %d, switch to new buffer 0x%16llx",
		runlist_id, (u64)gk20a_mem_phys(&runlist->mem[new_buf]));

	runlist_pa = gk20a_mem_phys(&runlist->mem[new_buf]);
	if (!runlist_pa) {
		ret = -EINVAL;
		goto clean_up;
	}

	runlist_entry_base = runlist->mem[new_buf].cpu_va;
	if (!runlist_entry_base) {
		ret = -ENOMEM;
		goto clean_up;
	}

	if (hw_chid != ~0 || /* add/remove a valid channel */
	    add /* resume to add all channels back */) {
		runlist_entry = runlist_entry_base;

		/* add non-TSG channels first */
		for_each_set_bit(chid,
			runlist->active_channels, f->num_channels) {
			ch = &f->channel[chid];

			if (!gk20a_is_channel_marked_as_tsg(ch)) {
				gk20a_dbg_info("add channel %d to runlist",
					chid);
				runlist_entry[0] = ram_rl_entry_chid_f(chid);
				runlist_entry[1] = 0;
				runlist_entry += 2;
				count++;
			}
		}

		/* now add TSG entries and channels bound to TSG */
		mutex_lock(&f->tsg_inuse_mutex);
		for_each_set_bit(tsgid,
				runlist->active_tsgs, f->num_channels) {
			tsg = &f->tsg[tsgid];
			/* add TSG entry */
			gk20a_dbg_info("add TSG %d to runlist", tsg->tsgid);
			runlist_entry[0] = ram_rl_entry_id_f(tsg->tsgid) |
				ram_rl_entry_type_tsg_f() |
				ram_rl_entry_timeslice_scale_3_f() |
				ram_rl_entry_timeslice_timeout_128_f() |
				ram_rl_entry_tsg_length_f(
					tsg->num_active_channels);
			runlist_entry[1] = 0;
			runlist_entry += 2;
			count++;

			/* add runnable channels bound to this TSG */
			count_channels_in_tsg = 0;
			mutex_lock(&tsg->ch_list_lock);
			list_for_each_entry(ch, &tsg->ch_list, ch_entry) {
				if (!test_bit(ch->hw_chid,
						runlist->active_channels))
					continue;
				gk20a_dbg_info("add channel %d to runlist",
					ch->hw_chid);
				runlist_entry[0] =
					ram_rl_entry_chid_f(ch->hw_chid);
				runlist_entry[1] = 0;
				runlist_entry += 2;
				count++;
				count_channels_in_tsg++;
			}
			mutex_unlock(&tsg->ch_list_lock);

			WARN_ON(tsg->num_active_channels !=
				count_channels_in_tsg);
		}
		mutex_unlock(&f->tsg_inuse_mutex);
	} else	/* suspend to remove all channels */
		count = 0;

	if (count != 0) {
		gk20a_writel(g, fifo_runlist_base_r(),
			fifo_runlist_base_ptr_f(u64_lo32(runlist_pa >> 12)) |
			fifo_runlist_base_target_vid_mem_f());
	}

	gk20a_writel(g, fifo_runlist_r(),
		fifo_runlist_engine_f(runlist_id) |
		fifo_eng_runlist_length_f(count));

	if (wait_for_finish) {
		ret = gk20a_fifo_runlist_wait_pending(g, runlist_id);

		if (ret == -ETIMEDOUT) {
			gk20a_err(dev_from_gk20a(g),
				   "runlist update timeout");

			gk20a_fifo_runlist_reset_engines(g, runlist_id);

			/* engine reset needs the lock. drop it */
			/* wait until the runlist is active again */
			ret = gk20a_fifo_runlist_wait_pending(g, runlist_id);
			/* get the lock back. at this point everything should
			 * should be fine */

			if (ret)
				gk20a_err(dev_from_gk20a(g),
					   "runlist update failed: %d", ret);
		} else if (ret == -EINTR)
			gk20a_err(dev_from_gk20a(g),
				   "runlist update interrupted");
	}

	runlist->cur_buffer = new_buf;

clean_up:
	return ret;
}

/* add/remove a channel from runlist
   special cases below: runlist->active_channels will NOT be changed.
   (hw_chid == ~0 && !add) means remove all active channels from runlist.
   (hw_chid == ~0 &&  add) means restore all active channels on runlist. */
int gk20a_fifo_update_runlist(struct gk20a *g, u32 runlist_id, u32 hw_chid,
			      bool add, bool wait_for_finish)
{
	struct fifo_runlist_info_gk20a *runlist = NULL;
	struct fifo_gk20a *f = &g->fifo;
	u32 token = PMU_INVALID_MUTEX_OWNER_ID;
	u32 mutex_ret;
	u32 ret = 0;

	gk20a_dbg_fn("");

	runlist = &f->runlist_info[runlist_id];

	mutex_lock(&runlist->mutex);

	mutex_ret = pmu_mutex_acquire(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	ret = gk20a_fifo_update_runlist_locked(g, runlist_id, hw_chid, add,
					       wait_for_finish);

	if (!mutex_ret)
		pmu_mutex_release(&g->pmu, PMU_MUTEX_ID_FIFO, &token);

	mutex_unlock(&runlist->mutex);
	return ret;
}

int gk20a_fifo_suspend(struct gk20a *g)
{
	gk20a_dbg_fn("");

	/* stop bar1 snooping */
	gk20a_writel(g, fifo_bar1_base_r(),
			fifo_bar1_base_valid_false_f());

	/* disable fifo intr */
	gk20a_writel(g, fifo_intr_en_0_r(), 0);
	gk20a_writel(g, fifo_intr_en_1_r(), 0);

	gk20a_dbg_fn("done");
	return 0;
}

bool gk20a_fifo_mmu_fault_pending(struct gk20a *g)
{
	if (gk20a_readl(g, fifo_intr_0_r()) &
			fifo_intr_0_mmu_fault_pending_f())
		return true;
	else
		return false;
}

int gk20a_fifo_wait_engine_idle(struct gk20a *g)
{
	unsigned long end_jiffies = jiffies +
		msecs_to_jiffies(gk20a_get_gr_idle_timeout(g));
	unsigned long delay = GR_IDLE_CHECK_DEFAULT;
	int ret = -ETIMEDOUT;
	u32 i;
	struct device *d = dev_from_gk20a(g);

	gk20a_dbg_fn("");

	for (i = 0; i < fifo_engine_status__size_1_v(); i++) {
		do {
			u32 status = gk20a_readl(g, fifo_engine_status_r(i));
			if (!fifo_engine_status_engine_v(status)) {
				ret = 0;
				break;
			}

			usleep_range(delay, delay * 2);
			delay = min_t(unsigned long,
					delay << 1, GR_IDLE_CHECK_MAX);
		} while (time_before(jiffies, end_jiffies) ||
				!tegra_platform_is_silicon());
		if (ret) {
			gk20a_err(d, "cannot idle engine %u\n", i);
			break;
		}
	}

	gk20a_dbg_fn("done");

	return ret;
}

static void gk20a_fifo_apply_pb_timeout(struct gk20a *g)
{
	u32 timeout;

	if (tegra_platform_is_silicon()) {
		timeout = gk20a_readl(g, fifo_pb_timeout_r());
		timeout &= ~fifo_pb_timeout_detection_enabled_f();
		gk20a_writel(g, fifo_pb_timeout_r(), timeout);
	}
}

static u32 gk20a_fifo_get_num_fifos(struct gk20a *g)
{
	return ccsr_channel__size_1_v();
}

u32 gk20a_fifo_get_pbdma_signature(struct gk20a *g)
{
	return pbdma_signature_hw_valid_f() | pbdma_signature_sw_zero_f();
}

void gk20a_init_fifo(struct gpu_ops *gops)
{
	gk20a_init_channel(gops);
	gops->fifo.preempt_channel = gk20a_fifo_preempt_channel;
	gops->fifo.update_runlist = gk20a_fifo_update_runlist;
	gops->fifo.trigger_mmu_fault = gk20a_fifo_trigger_mmu_fault;
	gops->fifo.apply_pb_timeout = gk20a_fifo_apply_pb_timeout;
	gops->fifo.wait_engine_idle = gk20a_fifo_wait_engine_idle;
	gops->fifo.get_num_fifos = gk20a_fifo_get_num_fifos;
	gops->fifo.get_pbdma_signature = gk20a_fifo_get_pbdma_signature;
}
