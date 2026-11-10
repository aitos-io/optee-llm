/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef TEE_LIBC_H
#define TEE_LIBC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tee_api_types.h>

/*
 * tee-libc: musl and libstdc++ for OP-TEE TAs. Call tee_libc_init() once, from
 * TA_CreateEntryPoint(), before any C++ or libc code built against tee-libc runs.
 */
void tee_libc_init(void);

/*
 * Call first in every TA entry point after the first (TA_OpenSessionEntryPoint,
 * TA_InvokeCommandEntryPoint, TA_CloseSessionEntryPoint, TA_DestroyEntryPoint): restores the TLS
 * thread pointer, which OP-TEE does not keep from one invocation to the next.
 */
void tee_libc_enter(void);

/* In tee-libc's libc.a (musl, src/tee/tee_init.c). */
void __tee_libc_init(void);

/*
 * Registers a read-only file at path, backed by size bytes at data (shim/tee_vfile.c). The data
 * must outlive every open of it, and should be 4 KiB aligned so that mmap of it works. Returns 0,
 * or -1 when the table is full or the path too long.
 */
int tee_vfile_add(const char *path, const void *data, size_t size);

/*
 * Several cores (shim/tee_mt.c): instances of one TA share a pool mapped at the same address
 * (pseudo-TA tee_mt_shm, optee/pta: one TA per boot, the first that creates the pool).
 *   main:   tee_mt_main_attach() before allocating anything workers must see, then per request
 *           n = tee_mt_reserve(max) threads (the caller + n-1 workers), compute, tee_mt_release();
 *   worker: tee_mt_work(init) from its own session, until it returns; init runs once per instance
 *           to set up the per-instance state thread functions rely on.
 * pthread_create/pthread_join run threads on the reserved workers.
 */
#define TEE_MT_SLOTS	8

TEE_Result tee_mt_main_attach(size_t pool_size);
/*
 * Calls fn(arg) on a stack in the pool (main instance; otherwise just calls it). Computations
 * that start worker threads must run this way: ggml keeps what its threads read (the compute
 * plan) in the caller's stack frame, and the TA's own stack is invisible to worker instances.
 */
void tee_mt_call(void (*fn)(void *), void *arg);
int tee_mt_reserve(int max_threads);
void tee_mt_release(void);
TEE_Result tee_mt_work(void (*init)(void));
bool tee_mt_is_worker(void);
bool tee_mt_is_main(void);
bool tee_mt_is_threaded(void);
void *tee_mt_pool_alloc(size_t len, size_t align);
bool tee_mt_pool_free(void *p);
long tee_mt_futex_wait(volatile int *addr, int val, int64_t timeout_ms);
int pthread_create(unsigned long *thread, const void *attr, void *(*start)(void *), void *arg);
int pthread_join(unsigned long thread, void **retval);

/* shim/tee_syscall.c: every system call musl makes. */
long __tee_syscall(long n, long a, long b, long c, long d, long e, long f);

/* shim/tee_vfile.c: the system calls on registered files; *handled = 0 for anything else. */
long tee_vfile_syscall(long n, long a, long b, long c, long d, long e, long f, int *handled);

/* shim/tee_unwind.c: glibc's struct dl_find_object on aarch64 (no eh_dbase, no eh_count). */
struct dl_find_object {
	unsigned long long dlfo_flags;
	void *dlfo_map_start;
	void *dlfo_map_end;
	void *dlfo_link_map;
	void *dlfo_eh_frame;
	unsigned long long dlfo_reserved[7];
};

int _dl_find_object(void *pc, struct dl_find_object *result);

#endif /* TEE_LIBC_H */
