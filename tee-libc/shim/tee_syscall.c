// SPDX-License-Identifier: BSD-2-Clause
/*
 * The Linux system calls musl makes, answered inside an OP-TEE TA. Built into the TA (it needs the
 * TEE Internal API), linked with tee-libc's libc.a, which calls __tee_syscall() wherever musl would
 * enter the kernel. Numbers and errno values are Linux aarch64's, as musl expects them.
 *
 * A TA has no files, signals or processes, and one thread per instance: anything else answers
 * -ENOSYS once on the log, so a missing piece shows up instead of failing silently.
 */
#include <inttypes.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <trace.h>

#include <tee_libc.h>

#define LX_SYS_ioctl		29
#define LX_SYS_openat		56
#define LX_SYS_close		57
#define LX_SYS_lseek		62
#define LX_SYS_read		63
#define LX_SYS_write		64
#define LX_SYS_readv		65
#define LX_SYS_writev		66
#define LX_SYS_exit		93
#define LX_SYS_exit_group	94
#define LX_SYS_set_tid_address	96
#define LX_SYS_futex		98
#define LX_SYS_nanosleep	101
#define LX_SYS_clock_gettime	113
#define LX_SYS_clock_getres	114
#define LX_SYS_sched_setparam	118
#define LX_SYS_sched_setscheduler 119
#define LX_SYS_sched_getaffinity 123
#define LX_SYS_sched_yield	124
#define LX_SYS_kill		129
#define LX_SYS_tkill		130
#define LX_SYS_tgkill		131
#define LX_SYS_rt_sigaction	134
#define LX_SYS_rt_sigprocmask	135
#define LX_SYS_getpid		172
#define LX_SYS_gettid		178
#define LX_SYS_brk		214
#define LX_SYS_munmap		215
#define LX_SYS_mmap		222
#define LX_SYS_mremap		216
#define LX_SYS_mprotect		226
#define LX_SYS_madvise		233
#define LX_SYS_getrandom	278

#define LX_ENOENT	2
#define LX_EBADF	9
#define LX_ENOMEM	12
#define LX_EINVAL	22
#define LX_ENOTTY	25
#define LX_ESPIPE	29
#define LX_ENOSYS	38

#define LX_CLOCK_REALTIME	0
#define LX_MAP_ANONYMOUS	0x20
#define LX_FUTEX_WAIT		0
#define LX_FUTEX_WAIT_BITSET	9
#define LX_FUTEX_PRIVATE	128

struct lx_iovec {
	void *base;
	size_t len;
};

struct lx_timespec {
	int64_t sec;
	long nsec;
};

/* stdout/stderr: whole lines go to the TA log; a partial line waits for the rest. */
static char line[256];
static size_t line_len;

static void out_bytes(const char *p, size_t n)
{
	size_t i = 0;

	for (i = 0; i < n; i++) {
		if (p[i] == '\n' || line_len == sizeof(line) - 1) {
			line[line_len] = 0;
			IMSG("%s", line);
			line_len = 0;
			if (p[i] == '\n')
				continue;
		}
		line[line_len++] = p[i];
	}
}

static long sys_writev(long fd, const struct lx_iovec *iov, long cnt)
{
	long total = 0;
	long i = 0;

	if (fd != 1 && fd != 2)
		return -LX_EBADF;
	for (i = 0; i < cnt; i++) {
		out_bytes(iov[i].base, iov[i].len);
		total += iov[i].len;
	}
	return total;
}

static long sys_clock_gettime(long clk, struct lx_timespec *ts)
{
	TEE_Time t = { };

	/* REALTIME is the normal world's clock; every other clock counts from TEE boot. */
	if (clk == LX_CLOCK_REALTIME)
		TEE_GetREETime(&t);
	else
		TEE_GetSystemTime(&t);
	ts->sec = t.seconds;
	ts->nsec = (long)t.millis * 1000000;
	return 0;
}

static long sys_nanosleep(const struct lx_timespec *req)
{
	uint64_t ms = (uint64_t)req->sec * 1000 + req->nsec / 1000000;

	TEE_Wait(ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms);
	return 0;
}

/*
 * Anonymous mmap, for musl's malloc: page-aligned blocks from the TA heap (libutils' bget, which is
 * fine for mallocng's few large requests). A header page in front of each block remembers where
 * the bget block starts, so munmap can give it back.
 */
#define PAGE		4096
#define ANON_MAGIC	0x544545414e4f4eULL	/* "TEEANON" */

struct anon_head {
	uint64_t magic;
	void *block;
	size_t len;
};

static long anon_mmap(size_t len)
{
	uint8_t *block = NULL;
	struct anon_head *h = NULL;

	len = (len + PAGE - 1) & ~(size_t)(PAGE - 1);
	/*
	 * The main instance of a threaded TA allocates from the pool its workers share; anything
	 * else (a worker's private scratch, a TA before it attaches) comes from the instance's heap.
	 */
	if (tee_mt_is_main())
		block = tee_mt_pool_alloc(len + PAGE, PAGE);
	else
		block = malloc_flags(MAF_ZERO_INIT, NULL, PAGE, len + PAGE);
	if (!block) {
		EMSG("tee-libc: out of TA heap mapping %zu bytes", len);
		return -LX_ENOMEM;
	}
	h = (struct anon_head *)(block + PAGE - sizeof(*h));
	h->magic = ANON_MAGIC;
	h->block = block;
	h->len = len;
	return (long)(block + PAGE);
}

static long anon_munmap(uintptr_t addr, size_t len)
{
	struct anon_head *h = (struct anon_head *)(addr - sizeof(*h));

	if (addr & (PAGE - 1))
		return -LX_EINVAL;
	if (h->magic != ANON_MAGIC || h->block != (void *)(addr - PAGE)) {
		EMSG("tee-libc: munmap of %#" PRIxPTR " which mmap did not return", addr);
		return -LX_EINVAL;
	}
	if (((len + PAGE - 1) & ~(size_t)(PAGE - 1)) != h->len) {
		/* mallocng unmaps whole regions; anything else would need splitting */
		EMSG("tee-libc: partial munmap (%zu of %zu bytes)", len, h->len);
		return -LX_EINVAL;
	}
	h->magic = 0;
	if (!tee_mt_pool_free(h->block))
		free_flags(MAF_NULL, h->block);
	return 0;
}

static long unhandled(long n)
{
	static uint64_t logged[5];

	if (n >= 0 && n < 320 && !(logged[n / 64] & (1ULL << (n % 64)))) {
		logged[n / 64] |= 1ULL << (n % 64);
		EMSG("tee-libc: unhandled system call %ld", n);
	}
	return -LX_ENOSYS;
}

long __tee_syscall(long n, long a, long b, long c, long d, long e, long f)
{
	int handled = 0;
	long ret = tee_vfile_syscall(n, a, b, c, d, e, f, &handled);

	if (handled)
		return ret;

	switch (n) {
	case LX_SYS_write: {
		struct lx_iovec iov = { (void *)b, (size_t)c };

		return sys_writev(a, &iov, 1);
	}
	case LX_SYS_writev:
		return sys_writev(a, (const struct lx_iovec *)b, c);
	case LX_SYS_read:
	case LX_SYS_readv:
		return -LX_EBADF;
	case LX_SYS_ioctl:
		return -LX_ENOTTY;	/* stdout is not a terminal */
	case LX_SYS_openat:
		return -LX_ENOENT;
	case LX_SYS_close:
		return 0;
	case LX_SYS_lseek:
		return -LX_ESPIPE;
	case LX_SYS_clock_gettime:
		return sys_clock_gettime(a, (struct lx_timespec *)b);
	case LX_SYS_clock_getres:
		if (b) {
			((struct lx_timespec *)b)->sec = 0;
			((struct lx_timespec *)b)->nsec = 1000000;
		}
		return 0;
	case LX_SYS_nanosleep:
		return sys_nanosleep((const struct lx_timespec *)a);
	case LX_SYS_getrandom:
		TEE_GenerateRandom((void *)a, (size_t)b);
		return b;
	case LX_SYS_getpid:
	case LX_SYS_gettid:
	case LX_SYS_set_tid_address:
		return 1;
	case LX_SYS_sched_yield:
	case LX_SYS_sched_setparam:
	case LX_SYS_sched_setscheduler:	/* ggml's thread priority: the normal world schedules */
	case LX_SYS_rt_sigaction:
	case LX_SYS_rt_sigprocmask:
		return 0;
	case LX_SYS_sched_getaffinity:
		/* One CPU: the one this instance runs on. */
		if ((size_t)b < sizeof(unsigned long))
			return -LX_EINVAL;
		memset((void *)c, 0, b);
		*(unsigned long *)c = 1;
		return sizeof(unsigned long);
	case LX_SYS_futex:
		/*
		 * One thread: a lock is never contended, so nobody waits and nobody needs waking.
		 * Instances sharing a pool (tee_mt.c) do wait: FUTEX_WAIT spins until the word
		 * changes, which makes FUTEX_WAKE a no-op.
		 */
		if (tee_mt_is_threaded() &&
		    ((b & ~LX_FUTEX_PRIVATE) == LX_FUTEX_WAIT ||
		     (b & ~LX_FUTEX_PRIVATE) == LX_FUTEX_WAIT_BITSET)) {
			const struct lx_timespec *ts = (const struct lx_timespec *)d;

			/* WAIT_BITSET's timeout is absolute: treated as "no limit" here */
			return tee_mt_futex_wait((volatile int *)a, (int)c,
						 ts && (b & ~LX_FUTEX_PRIVATE) == LX_FUTEX_WAIT ?
						 ts->sec * 1000 + ts->nsec / 1000000 : -1);
		}
		return 0;
	case LX_SYS_brk:
		return -LX_ENOMEM;	/* no program break: mallocng falls back to mmap */
	case LX_SYS_mmap:
		/* mmap(addr, len, prot, flags, fd, off); file mappings were tee_vfile_syscall's */
		if (!(d & LX_MAP_ANONYMOUS) || a)
			return -LX_EINVAL;
		return anon_mmap(b);
	case LX_SYS_munmap:
		return anon_munmap(a, b);
	case LX_SYS_mremap:
		return -LX_ENOMEM;	/* mallocng then allocates, copies and frees */
	case LX_SYS_mprotect:
		/* mallocng's guard pages around its metadata: TA memory stays as mapped */
		return 0;
	case LX_SYS_madvise:
		return 0;
	case LX_SYS_kill:
	case LX_SYS_tkill:
	case LX_SYS_tgkill:
		/* abort() raising SIGABRT */
		EMSG("tee-libc: signal %ld raised, panicking", c ? c : b);
		TEE_Panic(TEE_ERROR_GENERIC);
		return 0;
	case LX_SYS_exit:
	case LX_SYS_exit_group:
		EMSG("tee-libc: exit(%ld)", a);
		TEE_Panic(TEE_ERROR_GENERIC);
		return 0;
	default:
		return unhandled(n);
	}
}

/*
 * The TA's thread pointer (OP-TEE's TCB, set up once on the TA's first entry). OP-TEE 4.11 keeps
 * TPIDR_EL0 across system calls within one invocation, but a later invocation enters user mode
 * without it, and libutee sets it only during the first: from the second invocation on, every TLS
 * access (libstdc++'s exception globals, for one) would read through a stale or null pointer.
 */
static uint64_t tcb;

void tee_libc_init(void)
{
	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(tcb));
	__tee_libc_init();
}

void tee_libc_enter(void)
{
	if (tcb)
		__asm__ volatile("msr tpidr_el0, %0" : : "r"(tcb));
}
