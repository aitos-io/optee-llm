// SPDX-License-Identifier: BSD-2-Clause
/*
 * Threads for code on tee-libc, across TA instances.
 * A TA instance runs one thread, so a TA that wants several cores runs several instances of the
 * same binary, each entered from its own normal-world thread:
 *
 *   - the main instance maps a pool of secure memory that the pseudo-TA tee_mt_shm maps at the same
 *     address in every instance, and from then on serves all of musl's malloc from it (anonymous
 *     mmap, see tee_syscall.c), so that every pointer the program makes means the same in all
 *     instances;
 *   - worker instances map the same pool and wait in tee_mt_work(); pthread_create() in the main
 *     instance posts the thread function to a reserved worker, pthread_join() waits for it. The
 *     instances run one binary, so a function's address differs only by the load offset.
 *
 * What a thread function touches must live in the pool or be per-instance state that each worker
 * sets up for itself (tee_mt_work's init callback): another instance's static data is not mapped.
 * tee_mt_shm gives the pool to the first TA that creates it, until reboot: one TA per boot uses this.
 */
#include <malloc.h>
#include <stdint.h>
#include <string.h>
#include <tee_internal_api.h>

#include <tee_libc.h>
#include "../../optee/pta/pta_tee_mt_shm.h"

#define MT_MAGIC	0x6d74706cu
#define HDR_SIZE	4096
#define CTX_OFFSET	2048	/* the pool allocator's state, in the header page */
#define IDLE_WAITS	2000	/* 1 ms sleeps before an idle worker returns */
/*
 * No single graph node takes anywhere near this long; a thread waiting longer for another has lost
 * it (a panic in that instance) and would otherwise hold its core and OP-TEE thread forever.
 */
#define STUCK_MS	5000

#define LX_EAGAIN	11
#define LX_EINVAL	22

enum slot_state {
	SLOT_FREE,	/* no worker */
	SLOT_IDLE,	/* a worker waits */
	SLOT_RESERVED,	/* held by the main instance for a request */
	SLOT_POSTED,	/* a thread function to run */
	SLOT_DONE,	/* it returned */
};

struct mt_slot {
	int state;
	uint64_t owner;	/* the worker's token while it holds the slot */
	uintptr_t fn;
	void *arg;
} __aligned(64);

/* The pool's first page; the rest is the main instance's heap. */
struct mt_hdr {
	uint32_t magic;
	uintptr_t anchor;	/* mt_anchor() in the main instance */
	struct mt_slot slot[TEE_MT_SLOTS];
};

static struct mt_hdr *hdr;
static TEE_TASessionHandle pta;
static enum { ROLE_NONE, ROLE_MAIN, ROLE_WORKER } role;
static uint32_t reserved;	/* main: bitmap of reserved slots */
static uint32_t posted;		/* main: of those, running a thread */
static struct malloc_ctx *pool;	/* main: the pool allocator */
static uint8_t *pool_lo, *pool_hi;
static uint64_t token;		/* worker: marks the slot it holds */

/* Its address locates this binary's load offset in each instance. */
static void mt_anchor(void)
{
}

static int load(int *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static void store(int *p, int v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

static bool cas(int *p, int from, int to)
{
	return __atomic_compare_exchange_n(p, &from, to, false, __ATOMIC_ACQ_REL,
					   __ATOMIC_ACQUIRE);
}

/* Panics this instance once now - *since exceeds STUCK_MS (*since 0: starts the clock). */
static void check_stuck(uint32_t *since)
{
	TEE_Time t = { };
	uint32_t ms = 0;

	TEE_GetSystemTime(&t);
	ms = t.seconds * 1000 + t.millis;
	if (!*since)
		*since = ms;
	else if (ms - *since > STUCK_MS)
		TEE_Panic(TEE_ERROR_TIMEOUT);
}

static TEE_Result map_pool(uint32_t pool_role, size_t size, size_t *got)
{
	const TEE_UUID uuid = PTA_TEE_MT_SHM_UUID;
	TEE_Param p[TEE_NUM_PARAMS] = { };
	uint32_t origin = 0;
	TEE_Result res = TEE_SUCCESS;

	if (!pta) {
		res = TEE_OpenTASession(&uuid, TEE_TIMEOUT_INFINITE, 0, NULL, &pta, &origin);
		if (res)
			return res;
	}
	p[0].value.a = size;
	p[0].value.b = pool_role;
	res = TEE_InvokeTACommand(pta, TEE_TIMEOUT_INFINITE, PTA_TEE_MT_SHM_CMD_MAP,
				  TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						  TEE_PARAM_TYPE_VALUE_OUTPUT,
						  TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_NONE),
				  p, &origin);
	if (res)
		return res;
	hdr = (struct mt_hdr *)(uintptr_t)((uint64_t)p[1].value.b << 32 | p[1].value.a);
	*got = p[2].value.a;
	return TEE_SUCCESS;
}

TEE_Result tee_mt_main_attach(size_t pool_size)
{
	size_t got = 0;
	TEE_Result res = TEE_SUCCESS;
	int i = 0;

	if (role == ROLE_MAIN)
		return TEE_SUCCESS;
	if (role == ROLE_WORKER)
		return TEE_ERROR_BAD_STATE;
	res = map_pool(PTA_TEE_MT_SHM_ROLE_OWNER, pool_size, &got);
	if (res)
		return res;
	if (raw_malloc_get_ctx_size() > HDR_SIZE - CTX_OFFSET || sizeof(*hdr) > CTX_OFFSET)
		return TEE_ERROR_GENERIC;
	hdr->magic = 0;
	pool = (struct malloc_ctx *)((uint8_t *)hdr + CTX_OFFSET);
	raw_malloc_init_ctx(pool);
	pool_lo = (uint8_t *)hdr + HDR_SIZE;
	pool_hi = (uint8_t *)hdr + got;
	raw_malloc_add_pool(pool, pool_lo, pool_hi - pool_lo);
	/*
	 * Every slot starts over, which also frees those of workers that died with a previous main
	 * instance. Live workers see their slot go and claim one anew; owner tells a worker whether
	 * the slot it watches is still its own once another has claimed it.
	 */
	for (i = 0; i < TEE_MT_SLOTS; i++) {
		__atomic_store_n(&hdr->slot[i].owner, 0, __ATOMIC_RELEASE);
		store(&hdr->slot[i].state, SLOT_FREE);
	}
	hdr->anchor = (uintptr_t)mt_anchor;
	hdr->magic = MT_MAGIC;
	role = ROLE_MAIN;
	return TEE_SUCCESS;
}

void *tee_mt_pool_alloc(size_t len, size_t align)
{
	void *p = NULL;

	if (!pool)
		return NULL;
	p = raw_memalign(0, 0, align, len, pool);
	if (p)
		memset(p, 0, len);	/* anonymous memory starts zeroed */
	return p;
}

/*
 * musl's mallocng asks this before taking a slot in an existing group (build-musl.sh). In a main
 * instance, groups from before tee_mt_main_attach() sit in the private heap: new objects there
 * would be invisible to workers, so only pool groups qualify.
 */
int __tee_malloc_mem_ok(const void *p)
{
	return role != ROLE_MAIN ||
	       ((const uint8_t *)p >= (const uint8_t *)hdr && (const uint8_t *)p < pool_hi);
}

bool tee_mt_pool_free(void *p)
{
	if (!pool || (uint8_t *)p < pool_lo || (uint8_t *)p >= pool_hi)
		return false;
	raw_free(p, pool, false);
	return true;
}

void tee_mt_call_on_stack(void (*fn)(void *), void *arg, void *stack_top);

#define POOL_STACK_SIZE	(1024 * 1024)

void tee_mt_call(void (*fn)(void *), void *arg)
{
	static uint8_t *stack;

	if (role != ROLE_MAIN) {
		fn(arg);
		return;
	}
	if (!stack)
		stack = tee_mt_pool_alloc(POOL_STACK_SIZE, 4096);
	if (!stack) {
		EMSG("tee-libc: no pool stack, computing on the TA stack");
		fn(arg);
		return;
	}
	tee_mt_call_on_stack(fn, arg, stack + POOL_STACK_SIZE);
}

bool tee_mt_is_worker(void)
{
	return role == ROLE_WORKER;
}

bool tee_mt_is_main(void)
{
	return role == ROLE_MAIN;
}

bool tee_mt_is_threaded(void)
{
	return role != ROLE_NONE;
}

int tee_mt_reserve(int max_threads)
{
	int i = 0;
	int n = 1;

	if (role != ROLE_MAIN)
		return 1;
	for (i = 0; i < TEE_MT_SLOTS && n < max_threads; i++) {
		if (cas(&hdr->slot[i].state, SLOT_IDLE, SLOT_RESERVED)) {
			reserved |= 1u << i;
			n++;
		}
	}
	return n;
}

void tee_mt_release(void)
{
	int i = 0;

	for (i = 0; i < TEE_MT_SLOTS; i++)
		if (reserved & (1u << i))
			store(&hdr->slot[i].state, SLOT_IDLE);
	reserved = 0;
	posted = 0;
}

/* musl's pthread_create, removed from tee-libc's libc.a: threads run on reserved workers. */
int pthread_create(unsigned long *thread, const void *attr, void *(*start)(void *), void *arg)
{
	int i = 0;

	(void)attr;
	for (i = 0; i < TEE_MT_SLOTS; i++) {
		if ((reserved & ~posted) & (1u << i)) {
			hdr->slot[i].fn = (uintptr_t)start;
			hdr->slot[i].arg = arg;
			posted |= 1u << i;
			store(&hdr->slot[i].state, SLOT_POSTED);
			*thread = i + 1;
			return 0;
		}
	}
	EMSG("tee-libc: pthread_create with no reserved worker left");
	return LX_EAGAIN;
}

int pthread_join(unsigned long thread, void **retval)
{
	struct mt_slot *s = NULL;
	uint32_t since = 0;
	unsigned int n = 0;

	if (thread < 1 || thread > TEE_MT_SLOTS || !(posted & (1u << (thread - 1))))
		return LX_EINVAL;
	s = &hdr->slot[thread - 1];
	while (load(&s->state) != SLOT_DONE) {
		__asm__ volatile("yield");
		if (++n % 4096 == 0) {
			TEE_Wait(0);
			check_stuck(&since);
		}
	}
	store(&s->state, SLOT_RESERVED);
	posted &= ~(1u << (thread - 1));
	if (retval)
		*retval = NULL;
	return 0;
}

/* musl's pthread_detach (kept) joins through this name, which lived in the removed pthread_join.lo. */
int __pthread_join(unsigned long thread, void **retval) __attribute__((alias("pthread_join")));

static uint32_t now_ms(void)
{
	TEE_Time t = { };

	TEE_GetSystemTime(&t);
	return t.seconds * 1000 + t.millis;
}

/*
 * futex(FUTEX_WAIT) when several instances share memory: wait while *addr is val, for at most
 * timeout_ms (negative: no limit). FUTEX_WAKE is then a no-op: waiters see the change themselves.
 */
long tee_mt_futex_wait(volatile int *addr, int val, int64_t timeout_ms)
{
	const uint32_t start = now_ms();
	bool reported = false;
	unsigned int n = 0;

	while (__atomic_load_n(addr, __ATOMIC_ACQUIRE) == val) {
		__asm__ volatile("yield");
		if (++n % 4096 == 0) {
			TEE_Wait(0);
			if (timeout_ms >= 0 && now_ms() - start > timeout_ms)
				return -110;	/* ETIMEDOUT */
			/* A computation never waits this long: say where, once. */
			if (!reported && now_ms() - start > STUCK_MS) {
				EMSG("tee-libc: %s waiting on %p (value %d) for %u ms",
				     role == ROLE_MAIN ? "main" : "worker", (void *)addr, val,
				     now_ms() - start);
				reported = true;
			}
		}
	}
	return 0;
}

TEE_Result tee_mt_work(void (*init)(void))
{
	struct mt_slot *s = NULL;
	unsigned int n = 0, waits = 0;
	size_t got = 0;
	TEE_Result res = TEE_SUCCESS;
	int i = 0;

	if (role == ROLE_MAIN)
		return TEE_ERROR_BAD_STATE;
	if (role == ROLE_NONE) {
		/* TEE_ERROR_BAD_STATE until a main instance made the pool */
		res = map_pool(PTA_TEE_MT_SHM_ROLE_WORKER, 0, &got);
		if (res)
			return res;
		if (init)
			init();
		TEE_GenerateRandom(&token, sizeof(token));
		token |= 1;	/* 0 is no owner */
		role = ROLE_WORKER;
	}
	for (i = 0; i < TEE_MT_SLOTS && !s; i++) {
		if (cas(&hdr->slot[i].state, SLOT_FREE, SLOT_IDLE)) {
			s = &hdr->slot[i];
			__atomic_store_n(&s->owner, token, __ATOMIC_RELEASE);
		}
	}
	if (!s)
		return TEE_ERROR_BUSY;

	TEE_UnmaskCancellation();
	for (;;) {
		int st = load(&s->state);

		/*
		 * A new main instance freed the slot and another worker claimed it while this one
		 * slept: it is not this worker's any more, nor are the jobs posted to it.
		 */
		if (__atomic_load_n(&s->owner, __ATOMIC_ACQUIRE) != token)
			return TEE_SUCCESS;
		if (st == SLOT_POSTED) {
			void *(*fn)(void *) = (void *(*)(void *))(s->fn - hdr->anchor +
								    (uintptr_t)mt_anchor);
			const uint32_t t0 = now_ms();

			fn(s->arg);
			if (now_ms() - t0 > STUCK_MS)
				EMSG("tee-libc: worker job took %u ms", now_ms() - t0);
			store(&s->state, SLOT_DONE);
			n = waits = 0;
		} else if (st == SLOT_IDLE) {
			/*
			 * Between requests: sleep in the normal world. Return when cancelled, and
			 * after a while anyway, so that a client that went away without cancelling
			 * frees the thread (the client invokes again to keep serving).
			 */
			if (TEE_GetCancellationFlag() && cas(&s->state, SLOT_IDLE, SLOT_FREE))
				return TEE_ERROR_CANCEL;
			if (++n <= 4096) {
				__asm__ volatile("yield");
			} else if (++waits > IDLE_WAITS) {
				if (cas(&s->state, SLOT_IDLE, SLOT_FREE))
					return TEE_SUCCESS;
			} else {
				TEE_Wait(1);
			}
		} else if (st == SLOT_FREE) {
			/* a new main instance took the slot back: claim anew */
			return TEE_SUCCESS;
		} else {
			waits = 0;
			/* reserved for a request: the next graph comes soon */
			__asm__ volatile("yield");
			if (++n % 4096 == 0)
				TEE_Wait(0);
		}
	}
}
