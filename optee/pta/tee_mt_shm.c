// SPDX-License-Identifier: BSD-2-Clause
/*
 * Shared secure memory for the instances of one TA, see pta_tee_mt_shm.h.
 * The pool is secure TA memory mapped into several instances of the same TA
 * at the same address, so that pointers in it mean the same thing in all of
 * them. Nothing here touches its contents.
 */
#include <kernel/mutex.h>
#include <kernel/pseudo_ta.h>
#include <kernel/user_mode_ctx.h>
#include <kernel/user_ta.h>
#include <mm/core_mmu.h>
#include <mm/fobj.h>
#include <mm/mobj.h>
#include <mm/vm.h>
#include <string.h>
#include <util.h>

#include "pta_tee_mt_shm.h"

#define PTA_NAME "tee_mt_shm.pta"

static struct mutex lock = MUTEX_INITIALIZER;
static struct mobj *pool;
static TEE_UUID pool_uuid;	/* the TA it was created for */
static size_t pool_size;
static struct ts_ctx *owner;

/*
 * The instances that mapped the pool. When one panics, the others lose a thread
 * they may be waiting for in a spin (a graph's barrier) that would never end:
 * they are condemned, and the core ends each at its next return to user mode
 * from a foreign interrupt (optee/patches/condemn.patch). Read without the lock by
 * thread_user_ctx_condemned(), which runs with interrupts masked.
 */
#define MAX_MEMBERS	16	/* instances, not threads: idle sessions count */
static struct ts_ctx *members[MAX_MEMBERS];
static struct ts_ctx *condemned[MAX_MEMBERS];
static unsigned int num_condemned;

static void set_entry(struct ts_ctx **e, struct ts_ctx *ctx)
{
	__atomic_store_n(e, ctx, __ATOMIC_RELEASE);
}

/* Index of ctx in a list (NULL: a free entry), or -1. */
static int find(struct ts_ctx **list, struct ts_ctx *ctx)
{
	int i = 0;

	for (i = 0; i < MAX_MEMBERS; i++)
		if (__atomic_load_n(&list[i], __ATOMIC_ACQUIRE) == ctx)
			return i;
	return -1;
}

bool thread_user_ctx_condemned(struct ts_ctx *ctx)
{
	return __atomic_load_n(&num_condemned, __ATOMIC_ACQUIRE) &&
	       find(condemned, ctx) >= 0;
}

/* With the lock held: every member but dead is to end. */
static void condemn_others(struct ts_ctx *dead)
{
	unsigned int n = 0;
	int i = 0;
	int j = 0;

	for (i = 0; i < MAX_MEMBERS; i++) {
		if (!members[i] || members[i] == dead ||
		    find(condemned, members[i]) >= 0)
			continue;
		j = find(condemned, NULL);
		if (j < 0)
			break;
		set_entry(&condemned[j], members[i]);
		__atomic_add_fetch(&num_condemned, 1, __ATOMIC_RELEASE);
		n++;
	}
	if (n)
		EMSG("An instance sharing the pool panicked: ending %u others", n);
}

/* The caller, if it is a user TA. */
static struct ts_ctx *calling_ta(void)
{
	struct ts_session *s = ts_get_calling_session();

	if (!s || !is_user_ta_ctx(s->ctx))
		return NULL;
	return s->ctx;
}

static TEE_Result open_session(uint32_t types __unused,
			       TEE_Param params[TEE_NUM_PARAMS] __unused,
			       void **sess_ctx)
{
	struct ts_ctx *ctx = calling_ta();

	if (!ctx)
		return TEE_ERROR_ACCESS_DENIED;
	*sess_ctx = ctx;
	return TEE_SUCCESS;
}

/* At the instance's end: after a panic, its sessions close before it is freed. */
static void close_session(void *sess_ctx)
{
	struct ts_ctx *ctx = sess_ctx;
	int i = 0;

	mutex_lock(&lock);
	if (owner == ctx)
		owner = NULL;
	i = find(members, ctx);
	if (i >= 0) {
		set_entry(&members[i], NULL);
		/* one that was ended does not end the others: they may be new */
		if (to_ta_ctx(ctx)->panicked && find(condemned, ctx) < 0)
			condemn_others(ctx);
	}
	i = find(condemned, ctx);
	if (i >= 0) {
		set_entry(&condemned[i], NULL);
		__atomic_sub_fetch(&num_condemned, 1, __ATOMIC_RELEASE);
	}
	mutex_unlock(&lock);
}

/* The same address in every instance: the top of the user address range. */
static vaddr_t pool_va(void)
{
	vaddr_t base = 0;
	size_t size = 0;

	core_mmu_get_user_va_range(&base, &size);
	return ROUNDDOWN(base + size - pool_size - SMALL_PAGE_SIZE,
			 CORE_MMU_PGDIR_SIZE);
}

static TEE_Result map_pool(struct ts_ctx *ctx, uint32_t types,
			   TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t want = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
					      TEE_PARAM_TYPE_VALUE_OUTPUT,
					      TEE_PARAM_TYPE_VALUE_OUTPUT,
					      TEE_PARAM_TYPE_NONE);
	uint32_t role = params[0].value.b;
	size_t size = params[0].value.a;
	bool fresh = false;
	vaddr_t va = 0;
	TEE_Result res = TEE_SUCCESS;

	if (types != want || (role != PTA_TEE_MT_SHM_ROLE_OWNER &&
			      role != PTA_TEE_MT_SHM_ROLE_WORKER))
		return TEE_ERROR_BAD_PARAMETERS;

	mutex_lock(&lock);
	if (pool && memcmp(&ctx->uuid, &pool_uuid, sizeof(pool_uuid))) {
		res = TEE_ERROR_ACCESS_DENIED;
		goto out;
	}
	if (role == PTA_TEE_MT_SHM_ROLE_OWNER) {
		if (owner && owner != ctx) {
			res = TEE_ERROR_BUSY;
			goto out;
		}
		if (!pool) {
			struct fobj *f = NULL;

			if (!size || size & SMALL_PAGE_MASK) {
				res = TEE_ERROR_BAD_PARAMETERS;
				goto out;
			}
			f = fobj_ta_mem_alloc(size / SMALL_PAGE_SIZE);
			if (!f) {
				res = TEE_ERROR_OUT_OF_MEMORY;
				goto out;
			}
			pool = mobj_with_fobj_alloc(f, NULL,
						    TEE_MATTR_MEM_TYPE_TAGGED);
			fobj_put(f);
			if (!pool) {
				res = TEE_ERROR_OUT_OF_MEMORY;
				goto out;
			}
			pool_size = size;
			pool_uuid = ctx->uuid;
			fresh = true;
		}
	} else if (!pool) {
		res = TEE_ERROR_BAD_STATE;
		goto out;
	}

	va = pool_va();
	res = vm_map(to_user_mode_ctx(ctx), &va, pool_size,
		     TEE_MATTR_URW | TEE_MATTR_PRW, 0, pool, 0);
	if (res)
		goto out;
	if (find(members, ctx) < 0) {
		int i = find(members, NULL);

		if (i < 0) {
			vm_unmap(to_user_mode_ctx(ctx), va, pool_size);
			res = TEE_ERROR_OUT_OF_MEMORY;
			goto out;
		}
		set_entry(&members[i], ctx);
	}
	if (role == PTA_TEE_MT_SHM_ROLE_OWNER)
		owner = ctx;
	params[1].value.a = (uint32_t)va;
	params[1].value.b = (uint32_t)((uint64_t)va >> 32);
	params[2].value.a = pool_size;
	params[2].value.b = fresh;
out:
	mutex_unlock(&lock);
	return res;
}

static TEE_Result invoke_command(void *sess_ctx, uint32_t cmd, uint32_t types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	switch (cmd) {
	case PTA_TEE_MT_SHM_CMD_MAP:
		return map_pool(sess_ctx, types, params);
	default:
		return TEE_ERROR_NOT_IMPLEMENTED;
	}
}

pseudo_ta_register(.uuid = PTA_TEE_MT_SHM_UUID, .name = PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS | TA_FLAG_CONCURRENT,
		   .open_session_entry_point = open_session,
		   .close_session_entry_point = close_session,
		   .invoke_command_entry_point = invoke_command);
