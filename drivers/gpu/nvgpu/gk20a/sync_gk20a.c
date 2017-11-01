/*
 * GK20A Sync Framework Integration
 *
 * Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "sync_gk20a.h"

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <uapi/linux/nvgpu.h>
#include "../../../staging/android/sync.h"
#include "semaphore_gk20a.h"

static const struct sync_timeline_ops gk20a_sync_timeline_ops;

struct gk20a_sync_timeline {
	struct sync_timeline		obj;
	u32				max;
	u32				min;
};

/**
 * The sync framework dups pts when merging fences. We share a single
 * refcounted gk20a_sync_pt for each duped pt.
 */
struct gk20a_sync_pt {
	struct kref			refcount;
	u32				thresh;
	struct gk20a_semaphore		*sema;
	struct gk20a_sync_timeline	*obj;
	struct sync_fence		*dep;
	ktime_t				dep_timestamp;
};

struct gk20a_sync_pt_inst {
	struct sync_pt			pt;
	struct gk20a_sync_pt		*shared;
};

/**
 * Compares sync pt values a and b, both of which will trigger either before
 * or after ref (i.e. a and b trigger before ref, or a and b trigger after
 * ref). Supplying ref allows us to handle wrapping correctly.
 *
 * Returns -1 if a < b (a triggers before b)
 *	    0 if a = b (a and b trigger at the same time)
 *	    1 if a > b (b triggers before a)
 */
static int __gk20a_sync_pt_compare_ref(
	u32 ref,
	u32 a,
	u32 b)
{
	/*
	 * We normalize both a and b by subtracting ref from them.
	 * Denote the normalized values by a_n and b_n. Note that because
	 * of wrapping, a_n and/or b_n may be negative.
	 *
	 * The normalized values a_n and b_n satisfy:
	 * - a positive value triggers before a negative value
	 * - a smaller positive value triggers before a greater positive value
	 * - a smaller negative value (greater in absolute value) triggers
	 *   before a greater negative value (smaller in absolute value).
	 *
	 * Thus we can just stick to unsigned arithmetic and compare
	 * (u32)a_n to (u32)b_n.
	 *
	 * Just to reiterate the possible cases:
	 *
	 *	1A) ...ref..a....b....
	 *	1B) ...ref..b....a....
	 *	2A) ...b....ref..a....              b_n < 0
	 *	2B) ...a....ref..b....     a_n > 0
	 *	3A) ...a....b....ref..     a_n < 0, b_n < 0
	 *	3A) ...b....a....ref..     a_n < 0, b_n < 0
	 */
	u32 a_n = a - ref;
	u32 b_n = b - ref;
	if (a_n < b_n)
		return -1;
	else if (a_n > b_n)
		return 1;
	else
		return 0;
}

static struct gk20a_sync_pt *to_gk20a_sync_pt(struct sync_pt *pt)
{
	struct gk20a_sync_pt_inst *pti =
			container_of(pt, struct gk20a_sync_pt_inst, pt);
	return pti->shared;
}
static struct gk20a_sync_timeline *to_gk20a_timeline(struct sync_timeline *obj)
{
	if (WARN_ON(obj->ops != &gk20a_sync_timeline_ops))
		return NULL;
	return (struct gk20a_sync_timeline *)obj;
}

static void gk20a_sync_pt_free_shared(struct kref *ref)
{
	struct gk20a_sync_pt *pt =
		container_of(ref, struct gk20a_sync_pt, refcount);

	if (pt->dep)
		sync_fence_put(pt->dep);
	if (pt->sema)
		gk20a_semaphore_put(pt->sema);
	kfree(pt);
}

static struct gk20a_sync_pt *gk20a_sync_pt_create_shared(
		struct gk20a_sync_timeline *obj,
		struct gk20a_semaphore *sema,
		struct sync_fence *dependency)
{
	struct gk20a_sync_pt *shared;

	shared = kzalloc(sizeof(*shared), GFP_KERNEL);
	if (!shared)
		return NULL;

	kref_init(&shared->refcount);
	shared->obj = obj;
	shared->sema = sema;
	shared->thresh = ++obj->max; /* sync framework has a lock */

	/* Store the dependency fence for this pt. */
	if (dependency) {
		if (dependency->status == 0) {
			shared->dep = dependency;
		} else {
			shared->dep_timestamp = ktime_get();
			sync_fence_put(dependency);
		}
	}

	gk20a_semaphore_get(sema);
	return shared;
}

static struct sync_pt *gk20a_sync_pt_create_inst(
		struct gk20a_sync_timeline *obj,
		struct gk20a_semaphore *sema,
		struct sync_fence *dependency)
{
	struct gk20a_sync_pt_inst *pti;

	pti = (struct gk20a_sync_pt_inst *)
		sync_pt_create(&obj->obj, sizeof(*pti));
	if (!pti)
		return NULL;

	pti->shared = gk20a_sync_pt_create_shared(obj, sema, dependency);
	if (!pti->shared) {
		sync_pt_free(&pti->pt);
		return NULL;
	}
	return &pti->pt;
}

static void gk20a_sync_pt_free_inst(struct sync_pt *sync_pt)
{
	struct gk20a_sync_pt *pt = to_gk20a_sync_pt(sync_pt);
	if (pt)
		kref_put(&pt->refcount, gk20a_sync_pt_free_shared);
}

static struct sync_pt *gk20a_sync_pt_dup_inst(struct sync_pt *sync_pt)
{
	struct gk20a_sync_pt_inst *pti;
	struct gk20a_sync_pt *pt = to_gk20a_sync_pt(sync_pt);

	pti = (struct gk20a_sync_pt_inst *)
		sync_pt_create(&pt->obj->obj, sizeof(*pti));
	if (!pti)
		return NULL;
	pti->shared = pt;
	kref_get(&pt->refcount);
	return &pti->pt;
}

static int gk20a_sync_pt_has_signaled(struct sync_pt *sync_pt)
{
	struct gk20a_sync_pt *pt = to_gk20a_sync_pt(sync_pt);
	struct gk20a_sync_timeline *obj = pt->obj;
	struct sync_pt *pos;
	bool signaled;

	if (!pt->sema)
		return true;

	/* Acquired == not realeased yet == active == not signaled. */
	signaled = !gk20a_semaphore_is_acquired(pt->sema);

	if (signaled) {
		/* Update min if necessary. */
		if (__gk20a_sync_pt_compare_ref(obj->max, pt->thresh,
						obj->min) == 1)
			obj->min = pt->thresh;

		/* Release the dependency fence, but get its timestamp
		 * first.*/
		if (pt->dep) {
			s64 ns = 0;
			struct list_head *dep_pts = &pt->dep->pt_list_head;
			list_for_each_entry(pos, dep_pts, pt_list) {
				ns = max(ns, ktime_to_ns(pos->timestamp));
			}
			pt->dep_timestamp = ns_to_ktime(ns);
			sync_fence_put(pt->dep);
			pt->dep = NULL;
		}

		/* Release the semaphore to the pool. */
		gk20a_semaphore_put(pt->sema);
		pt->sema = NULL;
	}
	return signaled;
}

static inline ktime_t gk20a_sync_pt_duration(struct sync_pt *sync_pt)
{
	struct gk20a_sync_pt *pt = to_gk20a_sync_pt(sync_pt);
	if (!gk20a_sync_pt_has_signaled(sync_pt) || !pt->dep_timestamp.tv64)
		return ns_to_ktime(0);
	return ktime_sub(sync_pt->timestamp, pt->dep_timestamp);
}

static int gk20a_sync_pt_compare(struct sync_pt *a, struct sync_pt *b)
{
	bool a_expired;
	bool b_expired;
	struct gk20a_sync_pt *pt_a = to_gk20a_sync_pt(a);
	struct gk20a_sync_pt *pt_b = to_gk20a_sync_pt(b);

	if (WARN_ON(pt_a->obj != pt_b->obj))
		return 0;

	/* Early out */
	if (a == b)
		return 0;

	a_expired = gk20a_sync_pt_has_signaled(a);
	b_expired = gk20a_sync_pt_has_signaled(b);
	if (a_expired && !b_expired) {
		/* Easy, a was earlier */
		return -1;
	} else if (!a_expired && b_expired) {
		/* Easy, b was earlier */
		return 1;
	}

	/* Both a and b are expired (trigger before min) or not
	 * expired (trigger after min), so we can use min
	 * as a reference value for __gk20a_sync_pt_compare_ref.
	 */
	return __gk20a_sync_pt_compare_ref(pt_a->obj->min,
			pt_a->thresh, pt_b->thresh);
}

static u32 gk20a_sync_timeline_current(struct gk20a_sync_timeline *obj)
{
	return obj->min;
}

static void gk20a_sync_timeline_value_str(struct sync_timeline *timeline,
		char *str, int size)
{
	struct gk20a_sync_timeline *obj =
		(struct gk20a_sync_timeline *)timeline;
	snprintf(str, size, "%d", gk20a_sync_timeline_current(obj));
}

static void gk20a_sync_pt_value_str(struct sync_pt *sync_pt, char *str,
		int size)
{
	struct gk20a_sync_pt *pt = to_gk20a_sync_pt(sync_pt);
	ktime_t dur = gk20a_sync_pt_duration(sync_pt);
	if (pt->dep) {
		snprintf(str, size, "(dep: [%p] %s) %d",
			 pt->dep, pt->dep->name, pt->thresh);
	} else if (dur.tv64) {
		struct timeval tv = ktime_to_timeval(dur);
		snprintf(str, size, "(took %ld.%03ld ms) %d",
			 tv.tv_sec * 1000 + tv.tv_usec / 1000,
			 tv.tv_usec % 1000,
			 pt->thresh);
	} else {
		snprintf(str, size, "%d", pt->thresh);
	}
}

static int gk20a_sync_fill_driver_data(struct sync_pt *sync_pt,
		void *data, int size)
{
	struct gk20a_sync_pt_info info;

	if (size < sizeof(info))
		return -ENOMEM;

	info.hw_op_ns = ktime_to_ns(gk20a_sync_pt_duration(sync_pt));
	memcpy(data, &info, sizeof(info));

	return sizeof(info);
}

static const struct sync_timeline_ops gk20a_sync_timeline_ops = {
	.driver_name = "gk20a_semaphore",
	.dup = gk20a_sync_pt_dup_inst,
	.has_signaled = gk20a_sync_pt_has_signaled,
	.compare = gk20a_sync_pt_compare,
	.free_pt = gk20a_sync_pt_free_inst,
	.fill_driver_data = gk20a_sync_fill_driver_data,
	.timeline_value_str = gk20a_sync_timeline_value_str,
	.pt_value_str = gk20a_sync_pt_value_str,
};

/* Public API */

struct sync_fence *gk20a_sync_fence_fdget(int fd)
{
	return sync_fence_fdget(fd);
}

void gk20a_sync_timeline_signal(struct sync_timeline *timeline)
{
	sync_timeline_signal(timeline, 0);
}

void gk20a_sync_timeline_destroy(struct sync_timeline *timeline)
{
	sync_timeline_destroy(timeline);
}

struct sync_timeline *gk20a_sync_timeline_create(
		const char *fmt, ...)
{
	struct gk20a_sync_timeline *obj;
	char name[30];
	va_list args;

	va_start(args, fmt);
	vsnprintf(name, sizeof(name), fmt, args);
	va_end(args);

	obj = (struct gk20a_sync_timeline *)
		sync_timeline_create(&gk20a_sync_timeline_ops,
				     sizeof(struct gk20a_sync_timeline),
				     name);
	if (!obj)
		return NULL;
	obj->max = 0;
	obj->min = 0;
	return &obj->obj;
}

struct sync_fence *gk20a_sync_fence_create(struct sync_timeline *obj,
		struct gk20a_semaphore *sema,
		struct sync_fence *dependency,
		const char *fmt, ...)
{
	char name[30];
	va_list args;
	struct sync_pt *pt;
	struct sync_fence *fence;
	struct gk20a_sync_timeline *timeline = to_gk20a_timeline(obj);

	pt = gk20a_sync_pt_create_inst(timeline, sema, dependency);
	if (pt == NULL)
		return NULL;

	va_start(args, fmt);
	vsnprintf(name, sizeof(name), fmt, args);
	va_end(args);

	fence = sync_fence_create(name, pt);
	if (fence == NULL) {
		sync_pt_free(pt);
		return NULL;
	}
	return fence;
}
