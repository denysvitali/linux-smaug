/*
 * gk20a allocator
 *
 * Copyright (c) 2011-2015, NVIDIA CORPORATION.  All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

#include "platform_gk20a.h"
#include "gk20a_allocator.h"

#include "mm_gk20a.h"

static struct dentry *balloc_debugfs_root;

static struct kmem_cache *buddy_cache;	/* slab cache for meta data. */

static u32 balloc_tracing_on;

#define balloc_trace_func()				\
	do {						\
		if (balloc_tracing_on)			\
			trace_printk("%s\n", __func__);	\
	} while (0)

#define balloc_trace_func_done()				\
	do {							\
		if (balloc_tracing_on)				\
			trace_printk("%s_done\n", __func__);	\
	} while (0)


static void balloc_init_alloc_debug(struct gk20a_allocator *a);
static void balloc_print_stats(struct gk20a_allocator *a, struct seq_file *s,
			       int lock);
static struct gk20a_buddy *balloc_free_buddy(struct gk20a_allocator *a,
					     u64 addr);
static void balloc_coalesce(struct gk20a_allocator *a, struct gk20a_buddy *b);
static void __balloc_do_free_fixed(struct gk20a_allocator *a,
				   struct gk20a_fixed_alloc *falloc);

/*
 * This function is not present in older kernel's list.h code.
 */
#ifndef list_last_entry
#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)
#endif

/*
 * GPU buddy allocator for various address spaces.
 *
 * Current limitations:
 *   o  A fixed allocation could potentially be made that borders PDEs with
 *      different PTE sizes. This would require that fixed buffer to have
 *      different sized PTEs for different parts of the allocation. Probably
 *      best to just require PDE alignment for fixed address allocs.
 *
 *   o  It is currently possible to make an allocator that has a buddy alignment
 *      out of sync with the PDE block size alignment. A simple example is a
 *      32GB address space starting at byte 1. Every buddy is shifted off by 1
 *      which means each buddy corresponf to more than one actual GPU page. The
 *      best way to fix this is probably just require PDE blocksize alignment
 *      for the start of the address space. At the moment all allocators are
 *      easily PDE aligned so this hasn't been a problem.
 */

/*
 * Pick a suitable maximum order for this allocator.
 *
 * Hueristic: Just guessing that the best max order is the largest single
 * block that will fit in the address space.
 */
static void balloc_compute_max_order(struct gk20a_allocator *a)
{
	u64 true_max_order = ilog2(a->blks);

	if (a->max_order > true_max_order)
		a->max_order = true_max_order;
	if (a->max_order > GPU_BALLOC_MAX_ORDER)
		a->max_order = GPU_BALLOC_MAX_ORDER;
}

/*
 * Since we can only allocate in chucks of a->blk_size we need to trim off
 * any excess data that is not aligned to a->blk_size.
 */
static void balloc_allocator_align(struct gk20a_allocator *a)
{
	a->start = ALIGN(a->base, a->blk_size);
	a->end   = (a->base + a->length) & ~(a->blk_size - 1);
	a->count = a->end - a->start;
	a->blks  = a->count >> a->blk_shift;
}

/*
 * Pass NULL for parent if you want a top level buddy.
 */
static struct gk20a_buddy *balloc_new_buddy(struct gk20a_allocator *a,
					    struct gk20a_buddy *parent,
					    u64 start, u64 order)
{
	struct gk20a_buddy *new_buddy;

	new_buddy = kmem_cache_alloc(buddy_cache, GFP_KERNEL);
	if (!new_buddy)
		return NULL;

	memset(new_buddy, 0, sizeof(struct gk20a_buddy));

	new_buddy->parent = parent;
	new_buddy->start = start;
	new_buddy->order = order;
	new_buddy->end = start + (1 << order) * a->blk_size;

	return new_buddy;
}

static void __balloc_buddy_list_add(struct gk20a_allocator *a,
				    struct gk20a_buddy *b,
				    struct list_head *list)
{
	if (buddy_is_in_list(b)) {
		balloc_dbg(a, "Oops: adding added buddy (%llu:0x%llx)\n",
			   b->order, b->start);
		BUG();
	}

	/*
	 * Add big PTE blocks to the tail, small to the head for GVA spaces.
	 * This lets the code that checks if there are available blocks check
	 * without cycling through the entire list.
	 */
	if (a->flags & GPU_BALLOC_GVA_SPACE &&
	    b->pte_size == BALLOC_PTE_SIZE_BIG)
		list_add_tail(&b->buddy_entry, list);
	else
		list_add(&b->buddy_entry, list);

	buddy_set_in_list(b);
}

static void __balloc_buddy_list_rem(struct gk20a_allocator *a,
				    struct gk20a_buddy *b)
{
	if (!buddy_is_in_list(b)) {
		balloc_dbg(a, "Oops: removing removed buddy (%llu:0x%llx)\n",
			   b->order, b->start);
		BUG();
	}

	list_del_init(&b->buddy_entry);
	buddy_clr_in_list(b);
}

/*
 * Add a buddy to one of the buddy lists and deal with the necessary
 * book keeping. Adds the buddy to the list specified by the buddy's order.
 */
static void balloc_blist_add(struct gk20a_allocator *a, struct gk20a_buddy *b)
{
	__balloc_buddy_list_add(a, b, balloc_get_order_list(a, b->order));
	a->buddy_list_len[b->order]++;
}

static void balloc_blist_rem(struct gk20a_allocator *a, struct gk20a_buddy *b)
{
	__balloc_buddy_list_rem(a, b);
	a->buddy_list_len[b->order]--;
}

static u64 balloc_get_order(struct gk20a_allocator *a, u64 len)
{
	if (len == 0)
		return 0;

	len--;
	len >>= a->blk_shift;

	return fls(len);
}

static u64 __balloc_max_order_in(struct gk20a_allocator *a, u64 start, u64 end)
{
	u64 size = (end - start) >> a->blk_shift;

	if (size > 0)
		return min_t(u64, ilog2(size), a->max_order);
	else
		return GPU_BALLOC_MAX_ORDER;
}

/*
 * Initialize the buddy lists.
 */
static int balloc_init_lists(struct gk20a_allocator *a)
{
	int i;
	u64 bstart, bend, order;
	struct gk20a_buddy *buddy;

	bstart = a->start;
	bend = a->end;

	/* First make sure the LLs are valid. */
	for (i = 0; i < GPU_BALLOC_ORDER_LIST_LEN; i++)
		INIT_LIST_HEAD(balloc_get_order_list(a, i));

	while (bstart < bend) {
		order = __balloc_max_order_in(a, bstart, bend);

		buddy = balloc_new_buddy(a, NULL, bstart, order);
		if (!buddy)
			goto cleanup;

		balloc_blist_add(a, buddy);
		bstart += balloc_order_to_len(a, order);
	}

	return 0;

cleanup:
	for (i = 0; i < GPU_BALLOC_ORDER_LIST_LEN; i++) {
		if (!list_empty(balloc_get_order_list(a, i))) {
			buddy = list_first_entry(balloc_get_order_list(a, i),
					struct gk20a_buddy, buddy_entry);
			balloc_blist_rem(a, buddy);
			kmem_cache_free(buddy_cache, buddy);
		}
	}

	return -ENOMEM;
}

/*
 * Initialize a buddy allocator. Returns 0 on success. This allocator does
 * not necessarily manage bytes. It manages distinct ranges of resources. This
 * allows the allocator to work for things like comp_tags, semaphores, etc.
 *
 * @allocator: Ptr to an allocator struct to init.
 * @vm: GPU VM to associate this allocator with. Can be NULL. Will be used to
 *      get PTE size for GVA spaces.
 * @name: Name of the allocator. Doesn't have to be static storage.
 * @base: The base address of the resource pool being managed.
 * @size: Number of resources in the pool.
 * @blk_size: Minimum number of resources to allocate at once. For things like
 *            semaphores this is 1. For GVA this might be as much as 64k. This
 *            corresponds to order 0. Must be power of 2.
 * @max_order: Pick a maximum order. If you leave this as 0, the buddy allocator
 *             will try and pick a reasonable max order.
 * @flags: Extra flags necessary. See GPU_BALLOC_*.
 */
int __gk20a_allocator_init(struct gk20a_allocator *a,
			   struct vm_gk20a *vm, const char *name,
			   u64 base, u64 size, u64 blk_size, u64 max_order,
			   u64 flags)
{
	int err;

	memset(a, 0, sizeof(struct gk20a_allocator));
	strncpy(a->name, name, 32);

	a->base = base;
	a->length = size;
	a->blk_size = blk_size;
	a->blk_shift = __ffs(blk_size);

	/* blk_size must be greater than 0 and a power of 2. */
	if (blk_size == 0)
		return -EINVAL;
	if (blk_size & (blk_size - 1))
		return -EINVAL;

	if (max_order > GPU_BALLOC_MAX_ORDER)
		return -EINVAL;

	/* If this is to manage a GVA space we need a VM. */
	if (flags & GPU_BALLOC_GVA_SPACE && !vm)
		return -EINVAL;

	a->vm = vm;
	if (flags & GPU_BALLOC_GVA_SPACE)
		a->pte_blk_order = balloc_get_order(a, vm->big_page_size << 10);

	a->flags = flags;
	a->max_order = max_order;

	balloc_allocator_align(a);
	balloc_compute_max_order(a);

	/* Shared buddy kmem_cache for all allocators. */
	if (!buddy_cache)
		buddy_cache = KMEM_CACHE(gk20a_buddy, 0);
	if (!buddy_cache)
		return -ENOMEM;

	a->alloced_buddies = RB_ROOT;
	err = balloc_init_lists(a);
	if (err)
		return err;

	mutex_init(&a->lock);

	a->init = 1;

	balloc_init_alloc_debug(a);
	balloc_dbg(a, "New allocator: base      0x%llx\n", a->base);
	balloc_dbg(a, "               size      0x%llx\n", a->length);
	balloc_dbg(a, "               blk_size  0x%llx\n", a->blk_size);
	balloc_dbg(a, "               max_order %llu\n", a->max_order);
	balloc_dbg(a, "               flags     0x%llx\n", a->flags);

	return 0;
}

int gk20a_allocator_init(struct gk20a_allocator *a, const char *name,
			 u64 base, u64 size, u64 blk_size)
{
	return __gk20a_allocator_init(a, NULL, name,
				      base, size, blk_size, 0, 0);
}

/*
 * Clean up and destroy the passed allocator.
 */
void gk20a_allocator_destroy(struct gk20a_allocator *a)
{
	struct rb_node *node;
	struct gk20a_buddy *bud;
	struct gk20a_fixed_alloc *falloc;
	int i;

	balloc_lock(a);

	if (!IS_ERR_OR_NULL(a->debugfs_entry))
		debugfs_remove(a->debugfs_entry);

	/*
	 * Free the fixed allocs first.
	 */
	while ((node = rb_first(&a->fixed_allocs)) != NULL) {
		falloc = container_of(node,
				      struct gk20a_fixed_alloc, alloced_entry);

		rb_erase(node, &a->fixed_allocs);
		__balloc_do_free_fixed(a, falloc);
	}

	/*
	 * And now free all outstanding allocations.
	 */
	while ((node = rb_first(&a->alloced_buddies)) != NULL) {
		bud = container_of(node, struct gk20a_buddy, alloced_entry);
		balloc_free_buddy(a, bud->start);
		balloc_blist_add(a, bud);
		balloc_coalesce(a, bud);
	}

	/*
	 * Now clean up the unallocated buddies.
	 */
	for (i = 0; i < GPU_BALLOC_ORDER_LIST_LEN; i++) {
		BUG_ON(a->buddy_list_alloced[i] != 0);

		while (!list_empty(balloc_get_order_list(a, i))) {
			bud = list_first_entry(balloc_get_order_list(a, i),
					       struct gk20a_buddy, buddy_entry);
			balloc_blist_rem(a, bud);
			kmem_cache_free(buddy_cache, bud);
		}

		if (a->buddy_list_len[i] != 0) {
			pr_info("Excess buddies!!! (%d: %llu)\n",
				i, a->buddy_list_len[i]);
			BUG();
		}
		if (a->buddy_list_split[i] != 0) {
			pr_info("Excess split nodes!!! (%d: %llu)\n",
				i, a->buddy_list_split[i]);
			BUG();
		}
		if (a->buddy_list_alloced[i] != 0) {
			pr_info("Excess alloced nodes!!! (%d: %llu)\n",
				i, a->buddy_list_alloced[i]);
			BUG();
		}
	}

	a->init = 0;

	balloc_unlock(a);

	/*
	 * We cant unlock an allocator after memsetting it. That wipes the
	 * state of the mutex. Hopefully no one uses the allocator after
	 * destroying it...
	 */
	memset(a, 0, sizeof(struct gk20a_allocator));
}

/*
 * Combine the passed buddy if possible. The pointer in @b may not be valid
 * after this as the buddy may be freed.
 *
 * @a must be locked.
 */
static void balloc_coalesce(struct gk20a_allocator *a, struct gk20a_buddy *b)
{
	struct gk20a_buddy *parent;

	if (buddy_is_alloced(b) || buddy_is_split(b))
		return;

	/*
	 * If both our buddy and I are both not allocated and not split then
	 * we can coalesce ourselves.
	 */
	if (!b->buddy)
		return;
	if (buddy_is_alloced(b->buddy) || buddy_is_split(b->buddy))
		return;

	parent = b->parent;

	balloc_blist_rem(a, b);
	balloc_blist_rem(a, b->buddy);

	buddy_clr_split(parent);
	a->buddy_list_split[parent->order]--;
	balloc_blist_add(a, parent);

	/*
	 * Recursively coalesce as far as we can go.
	 */
	balloc_coalesce(a, parent);

	/* Clean up the remains. */
	kmem_cache_free(buddy_cache, b->buddy);
	kmem_cache_free(buddy_cache, b);
}

/*
 * Split a buddy into two new buddies who are 1/2 the size of the parent buddy.
 *
 * @a must be locked.
 */
static int balloc_split_buddy(struct gk20a_allocator *a, struct gk20a_buddy *b,
			      int pte_size)
{
	struct gk20a_buddy *left, *right;
	u64 half;

	left = balloc_new_buddy(a, b, b->start, b->order - 1);
	if (!left)
		return -ENOMEM;

	half = (b->end - b->start) / 2;

	right = balloc_new_buddy(a, b, b->start + half, b->order - 1);
	if (!right) {
		kmem_cache_free(buddy_cache, left);
		return -ENOMEM;
	}

	buddy_set_split(b);
	a->buddy_list_split[b->order]++;

	b->left = left;
	b->right = right;
	left->buddy = right;
	right->buddy = left;
	left->parent = b;
	right->parent = b;

	/* PTE considerations. */
	if (a->flags & GPU_BALLOC_GVA_SPACE &&
	    left->order <= a->pte_blk_order) {
			left->pte_size = pte_size;
			right->pte_size = pte_size;
	}

	balloc_blist_rem(a, b);
	balloc_blist_add(a, left);
	balloc_blist_add(a, right);

	return 0;
}

/*
 * Place the passed buddy into the RB tree for allocated buddies. Never fails
 * unless the passed entry is a duplicate which is a bug.
 *
 * @a must be locked.
 */
static void balloc_alloc_buddy(struct gk20a_allocator *a, struct gk20a_buddy *b)
{
	struct rb_node **new = &(a->alloced_buddies.rb_node);
	struct rb_node *parent = NULL;

	while (*new) {
		struct gk20a_buddy *bud = container_of(*new, struct gk20a_buddy,
						       alloced_entry);

		parent = *new;
		if (b->start < bud->start)
			new = &((*new)->rb_left);
		else if (b->start > bud->start)
			new = &((*new)->rb_right);
		else
			BUG_ON("Duplicate entries in allocated list!\n");
	}

	rb_link_node(&b->alloced_entry, parent, new);
	rb_insert_color(&b->alloced_entry, &a->alloced_buddies);

	buddy_set_alloced(b);
	a->buddy_list_alloced[b->order]++;
}

/*
 * Remove the passed buddy from the allocated buddy RB tree. Returns the
 * deallocated buddy for further processing.
 *
 * @a must be locked.
 */
static struct gk20a_buddy *balloc_free_buddy(struct gk20a_allocator *a,
					     u64 addr)
{
	struct rb_node *node = a->alloced_buddies.rb_node;
	struct gk20a_buddy *bud;

	while (node) {
		bud = container_of(node, struct gk20a_buddy, alloced_entry);

		if (addr < bud->start)
			node = node->rb_left;
		else if (addr > bud->start)
			node = node->rb_right;
		else
			break;
	}

	if (!node)
		return NULL;

	rb_erase(node, &a->alloced_buddies);
	buddy_clr_alloced(bud);
	a->buddy_list_alloced[bud->order]--;

	return bud;
}

/*
 * Find a suitable buddy for the given order and PTE type (big or little).
 */
static struct gk20a_buddy *__balloc_find_buddy(struct gk20a_allocator *a,
					       u64 order, int pte_size)
{
	struct gk20a_buddy *bud;

	if (order > a->max_order ||
	    list_empty(balloc_get_order_list(a, order)))
		return NULL;

	if (a->flags & GPU_BALLOC_GVA_SPACE &&
	    pte_size == BALLOC_PTE_SIZE_BIG)
		bud = list_last_entry(balloc_get_order_list(a, order),
				      struct gk20a_buddy, buddy_entry);
	else
		bud = list_first_entry(balloc_get_order_list(a, order),
				       struct gk20a_buddy, buddy_entry);

	if (bud->pte_size != BALLOC_PTE_SIZE_ANY &&
	    bud->pte_size != pte_size)
		return NULL;

	return bud;
}

/*
 * Allocate a suitably sized buddy. If no suitable buddy exists split higher
 * order buddies until we have a suitable buddy to allocate.
 *
 * For PDE grouping add an extra check to see if a buddy is suitable: that the
 * buddy exists in a PDE who's PTE size is reasonable
 *
 * @a must be locked.
 */
static u64 __balloc_do_alloc(struct gk20a_allocator *a, u64 order, int pte_size)
{
	u64 split_order;
	struct gk20a_buddy *bud = NULL;

	split_order = order;
	while (split_order <= a->max_order &&
	       !(bud = __balloc_find_buddy(a, split_order, pte_size)))
		split_order++;

	/* Out of memory! */
	if (!bud)
		return 0;

	while (bud->order != order) {
		if (balloc_split_buddy(a, bud, pte_size))
			return 0; /* No mem... */
		bud = bud->left;
	}

	balloc_blist_rem(a, bud);
	balloc_alloc_buddy(a, bud);

	return bud->start;
}

/*
 * Allocate memory from the passed allocator.
 */
u64 gk20a_balloc(struct gk20a_allocator *a, u64 len)
{
	u64 order, addr;
	int pte_size;

	balloc_trace_func();

	balloc_lock(a);

	order = balloc_get_order(a, len);

	if (order > a->max_order) {
		balloc_unlock(a);
		balloc_dbg(a, "Alloc fail\n");
		balloc_trace_func_done();
		return 0;
	}

	/*
	 * For now pass the base address of the allocator's region to
	 * __get_pte_size(). This ensures we get the right page size for
	 * the alloc but we don't have to know what the real address is
	 * going to be quite yet.
	 *
	 * TODO: once userspace supports a unified address space pass 0 for
	 * the base. This will make only 'len' affect the PTE size.
	 */
	if (a->flags & GPU_BALLOC_GVA_SPACE)
		pte_size = __get_pte_size(a->vm, a->base, len);
	else
		pte_size = BALLOC_PTE_SIZE_ANY;

	addr = __balloc_do_alloc(a, order, pte_size);

	if (addr) {
		a->bytes_alloced += len;
		a->bytes_alloced_real += balloc_order_to_len(a, order);
		balloc_dbg(a, "Alloc 0x%-10llx %3lld:0x%-10llx pte_size=%s\n",
			   addr, order, len,
			   pte_size == gmmu_page_size_big   ? "big" :
			   pte_size == gmmu_page_size_small ? "small" :
			   "NA/any");
	} else {
		balloc_dbg(a, "Alloc failed: no mem!\n");
	}

	balloc_unlock(a);

	balloc_trace_func_done();
	return addr;
}

/*
 * See if the passed range is actually available for allocation. If so, then
 * return 1, otherwise return 0.
 *
 * TODO: Right now this uses the unoptimal approach of going through all
 * outstanding allocations and checking their base/ends. This could be better.
 */
static int balloc_is_range_free(struct gk20a_allocator *a, u64 base, u64 end)
{
	struct rb_node *node;
	struct gk20a_buddy *bud;

	node = rb_first(&a->alloced_buddies);
	if (!node)
		return 1; /* No allocs yet. */

	bud = container_of(node, struct gk20a_buddy, alloced_entry);

	while (bud->start < end) {
		if ((bud->start > base && bud->start < end) ||
		    (bud->end   > base && bud->end   < end))
			return 0;

		node = rb_next(node);
		if (!node)
			break;
		bud = container_of(node, struct gk20a_buddy, alloced_entry);
	}

	return 1;
}

static void balloc_alloc_fixed(struct gk20a_allocator *a,
			       struct gk20a_fixed_alloc *f)
{
	struct rb_node **new = &(a->fixed_allocs.rb_node);
	struct rb_node *parent = NULL;

	while (*new) {
		struct gk20a_fixed_alloc *falloc =
			container_of(*new, struct gk20a_fixed_alloc,
				     alloced_entry);

		parent = *new;
		if (f->start < falloc->start)
			new = &((*new)->rb_left);
		else if (f->start > falloc->start)
			new = &((*new)->rb_right);
		else
			BUG_ON("Duplicate entries in allocated list!\n");
	}

	rb_link_node(&f->alloced_entry, parent, new);
	rb_insert_color(&f->alloced_entry, &a->fixed_allocs);
}

/*
 * Remove the passed buddy from the allocated buddy RB tree. Returns the
 * deallocated buddy for further processing.
 *
 * @a must be locked.
 */
static struct gk20a_fixed_alloc *balloc_free_fixed(struct gk20a_allocator *a,
						   u64 addr)
{
	struct rb_node *node = a->fixed_allocs.rb_node;
	struct gk20a_fixed_alloc *falloc;

	while (node) {
		falloc = container_of(node,
				      struct gk20a_fixed_alloc, alloced_entry);

		if (addr < falloc->start)
			node = node->rb_left;
		else if (addr > falloc->start)
			node = node->rb_right;
		else
			break;
	}

	if (!node)
		return NULL;

	rb_erase(node, &a->fixed_allocs);

	return falloc;
}

/*
 * Find the parent range - doesn't necessarily need the parent to actually exist
 * as a buddy. Finding an existing parent comes later...
 */
static void __balloc_get_parent_range(struct gk20a_allocator *a,
				      u64 base, u64 order,
				      u64 *pbase, u64 *porder)
{
	u64 base_mask;
	u64 shifted_base = balloc_base_shift(a, base);

	order++;
	base_mask = ~((a->blk_size << order) - 1);

	shifted_base &= base_mask;

	*pbase = balloc_base_unshift(a, shifted_base);
	*porder = order;
}

/*
 * Makes a buddy at the passed address. This will make all parent buddies
 * necessary for this buddy to exist as well.
 */
static struct gk20a_buddy *__balloc_make_fixed_buddy(struct gk20a_allocator *a,
						     u64 base, u64 order)
{
	struct gk20a_buddy *bud = NULL;
	struct list_head *order_list;
	u64 cur_order = order, cur_base = base;

	/*
	 * Algo:
	 *  1. Keep jumping up a buddy order until we find the real buddy that
	 *     this buddy exists in.
	 *  2. Then work our way down through the buddy tree until we hit a dead
	 *     end.
	 *  3. Start splitting buddies until we split to the one we need to
	 *     make.
	 */
	while (cur_order <= a->max_order) {
		int found = 0;

		order_list = balloc_get_order_list(a, cur_order);
		list_for_each_entry(bud, order_list, buddy_entry) {
			if (bud->start == cur_base) {
				found = 1;
				break;
			}
		}

		if (found)
			break;

		__balloc_get_parent_range(a, cur_base, cur_order,
					  &cur_base, &cur_order);
	}

	if (cur_order > a->max_order) {
		balloc_dbg(a, "No buddy for range ???\n");
		return NULL;
	}

	/* Split this buddy as necessary until we get the target buddy. */
	while (bud->start != base || bud->order != order) {
		if (balloc_split_buddy(a, bud, BALLOC_PTE_SIZE_ANY)) {
			balloc_coalesce(a, bud);
			return NULL;
		}

		if (base < bud->right->start)
			bud = bud->left;
		else
			bud = bud->right;

	}

	return bud;
}

static u64 __balloc_do_alloc_fixed(struct gk20a_allocator *a,
				   struct gk20a_fixed_alloc *falloc,
				   u64 base, u64 len)
{
	u64 shifted_base, inc_base;
	u64 align_order;

	shifted_base = balloc_base_shift(a, base);
	if (shifted_base == 0)
		align_order = __fls(len >> a->blk_shift);
	else
		align_order = min_t(u64,
				    __ffs(shifted_base >> a->blk_shift),
				    __fls(len >> a->blk_shift));

	if (align_order > a->max_order) {
		balloc_dbg(a, "Align order too big: %llu > %llu\n",
			   align_order, a->max_order);
		return 0;
	}

	/*
	 * Generate a list of buddies that satisfy this allocation.
	 */
	inc_base = shifted_base;
	while (inc_base < (shifted_base + len)) {
		u64 order_len = balloc_order_to_len(a, align_order);
		u64 remaining;
		struct gk20a_buddy *bud;

		bud = __balloc_make_fixed_buddy(a,
					balloc_base_unshift(a, inc_base),
					align_order);
		if (!bud) {
			balloc_dbg(a, "Fixed buddy failed: {0x%llx, %llu}!\n",
				   balloc_base_unshift(a, inc_base),
				   align_order);
			goto err_and_cleanup;
		}

		balloc_blist_rem(a, bud);
		balloc_alloc_buddy(a, bud);
		__balloc_buddy_list_add(a, bud, &falloc->buddies);

		/* Book keeping. */
		inc_base += order_len;
		remaining = (shifted_base + len) - inc_base;
		align_order = __ffs(inc_base >> a->blk_shift);

		/* If we don't have much left - trim down align_order. */
		if (balloc_order_to_len(a, align_order) > remaining)
			align_order = __balloc_max_order_in(a, inc_base,
						inc_base + remaining);
	}

	return base;

err_and_cleanup:
	while (!list_empty(&falloc->buddies)) {
		struct gk20a_buddy *bud = list_first_entry(&falloc->buddies,
							   struct gk20a_buddy,
							   buddy_entry);

		__balloc_buddy_list_rem(a, bud);
		balloc_free_buddy(a, bud->start);
		kmem_cache_free(buddy_cache, bud);
	}

	return 0;
}

/*
 * Allocate a fixed address allocation. The address of the allocation is @base
 * and the length is @len. This is not a typical buddy allocator operation and
 * as such has a high posibility of failure if the address space is heavily in
 * use.
 *
 * Please do not use this function unless _absolutely_ necessary.
 */
u64 gk20a_balloc_fixed(struct gk20a_allocator *a, u64 base, u64 len)
{
	struct gk20a_fixed_alloc *falloc = NULL;
	struct gk20a_buddy *bud;
	u64 ret, real_bytes = 0;

	balloc_trace_func();

	/* If base isn't aligned to an order 0 block, fail. */
	if (base & (a->blk_size - 1))
		goto fail;

	if (len == 0)
		goto fail;

	falloc = kmalloc(sizeof(*falloc), GFP_KERNEL);
	if (!falloc)
		goto fail;

	INIT_LIST_HEAD(&falloc->buddies);
	falloc->start = base;
	falloc->end = base + len;

	balloc_lock(a);
	if (!balloc_is_range_free(a, base, base + len)) {
		balloc_dbg(a, "Range not free: 0x%llx -> 0x%llx\n",
			   base, base + len);
		goto fail_unlock;
	}

	ret = __balloc_do_alloc_fixed(a, falloc, base, len);
	if (!ret) {
		balloc_dbg(a, "Alloc-fixed failed ?? 0x%llx -> 0x%llx\n",
			   base, base + len);
		goto fail_unlock;
	}

	balloc_alloc_fixed(a, falloc);

	list_for_each_entry(bud, &falloc->buddies, buddy_entry)
		real_bytes += (bud->end - bud->start);

	a->bytes_alloced += len;
	a->bytes_alloced_real += real_bytes;

	balloc_unlock(a);
	balloc_dbg(a, "Alloc (fixed) 0x%llx\n", base);

	balloc_trace_func_done();
	return base;

fail_unlock:
	balloc_unlock(a);
fail:
	kfree(falloc);
	balloc_trace_func_done();
	return 0;
}

static void __balloc_do_free_fixed(struct gk20a_allocator *a,
				   struct gk20a_fixed_alloc *falloc)
{
	struct gk20a_buddy *bud;

	while (!list_empty(&falloc->buddies)) {
		bud = list_first_entry(&falloc->buddies,
				       struct gk20a_buddy,
				       buddy_entry);
		__balloc_buddy_list_rem(a, bud);

		balloc_free_buddy(a, bud->start);
		balloc_blist_add(a, bud);
		a->bytes_freed += balloc_order_to_len(a, bud->order);

		/*
		 * Attemp to defrag the allocation.
		 */
		balloc_coalesce(a, bud);
	}

	kfree(falloc);
}

/*
 * Free the passed allocation.
 */
void gk20a_bfree(struct gk20a_allocator *a, u64 addr)
{
	struct gk20a_buddy *bud;
	struct gk20a_fixed_alloc *falloc;

	balloc_trace_func();

	if (!addr) {
		balloc_trace_func_done();
		return;
	}

	balloc_lock(a);

	/*
	 * First see if this is a fixed alloc. If not fall back to a regular
	 * buddy.
	 */
	falloc = balloc_free_fixed(a, addr);
	if (falloc) {
		__balloc_do_free_fixed(a, falloc);
		goto done;
	}

	bud = balloc_free_buddy(a, addr);
	if (!bud)
		goto done;

	balloc_blist_add(a, bud);
	a->bytes_freed += balloc_order_to_len(a, bud->order);

	/*
	 * Attemp to defrag the allocation.
	 */
	balloc_coalesce(a, bud);

done:
	balloc_unlock(a);
	balloc_dbg(a, "Free 0x%llx\n", addr);
	balloc_trace_func_done();
	return;
}

/*
 * Print the buddy allocator top level stats. If you pass @s as NULL then the
 * stats are printed to the kernel log. This lets this code be used for
 * debugging purposes internal to the allocator.
 */
static void balloc_print_stats(struct gk20a_allocator *a, struct seq_file *s,
			       int lock)
{
#define __balloc_pstat(s, fmt, arg...)			\
	do {						\
		if (s)					\
			seq_printf(s, fmt, ##arg);	\
		else					\
			balloc_dbg(a, fmt, ##arg);	\
	} while (0)

	int i;
	struct rb_node *node;
	struct gk20a_fixed_alloc *falloc;

	__balloc_pstat(s, "base = %llu, limit = %llu, blk_size = %llu\n",
		   a->base, a->length, a->blk_size);
	__balloc_pstat(s, "Internal params:\n");
	__balloc_pstat(s, "  start = 0x%llx\n", a->start);
	__balloc_pstat(s, "  end   = 0x%llx\n", a->end);
	__balloc_pstat(s, "  count = 0x%llx\n", a->count);
	__balloc_pstat(s, "  blks  = 0x%llx\n", a->blks);
	__balloc_pstat(s, "  max_order = %llu\n", a->max_order);

	__balloc_pstat(s, "Buddy blocks:\n");
	__balloc_pstat(s, "  Order   Free    Alloced   Split\n");
	__balloc_pstat(s, "  -----   ----    -------   -----\n");

	if (lock)
		balloc_lock(a);
	for (i = a->max_order; i >= 0; i--) {
		if (a->buddy_list_len[i] == 0 &&
		    a->buddy_list_alloced[i] == 0 &&
		    a->buddy_list_split[i] == 0)
			continue;

		__balloc_pstat(s, "  %3d     %-7llu %-9llu %llu\n", i,
			       a->buddy_list_len[i],
			       a->buddy_list_alloced[i],
			       a->buddy_list_split[i]);
	}

	__balloc_pstat(s, "\n");

	for (node = rb_first(&a->fixed_allocs), i = 1;
	     node != NULL;
	     node = rb_next(node)) {
		falloc = container_of(node,
				      struct gk20a_fixed_alloc, alloced_entry);

		__balloc_pstat(s, "Fixed alloc (%d): [0x%llx -> 0x%llx]\n",
				i, falloc->start, falloc->end);
	}

	__balloc_pstat(s, "\n");
	__balloc_pstat(s, "Bytes allocated:        %llu\n", a->bytes_alloced);
	__balloc_pstat(s, "Bytes allocated (real): %llu\n",
		       a->bytes_alloced_real);
	__balloc_pstat(s, "Bytes freed:            %llu\n", a->bytes_freed);

	if (lock)
		balloc_unlock(a);

#undef __balloc_pstats
}

static int __alloc_show(struct seq_file *s, void *unused)
{
	struct gk20a_allocator *a = s->private;

	balloc_print_stats(a, s, 1);

	return 0;
}

static int __alloc_open(struct inode *inode, struct file *file)
{
	return single_open(file, __alloc_show, inode->i_private);
}

static const struct file_operations __alloc_fops = {
	.open = __alloc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void balloc_init_alloc_debug(struct gk20a_allocator *a)
{
	if (!balloc_debugfs_root)
		return;

	a->debugfs_entry = debugfs_create_file(a->name, S_IRUGO,
					       balloc_debugfs_root,
					       a, &__alloc_fops);
}

void gk20a_alloc_debugfs_init(struct platform_device *pdev)
{
	struct gk20a_platform *platform = platform_get_drvdata(pdev);
	struct dentry *gpu_root = platform->debugfs;

	balloc_debugfs_root = debugfs_create_dir("allocators", gpu_root);
	if (IS_ERR_OR_NULL(balloc_debugfs_root))
		return;

	debugfs_create_u32("tracing", 0664, balloc_debugfs_root,
			   &balloc_tracing_on);
}
