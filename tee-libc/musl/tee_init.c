/*
 * musl state for an OP-TEE TA. There is no __libc_start_main(): no kernel stack with
 * argv/envp/auxv, and OP-TEE allocates (TLS) and runs C++ static constructors before the TA's
 * first entry point, so musl must be usable from the first instruction. Everything is therefore
 * initialized statically: the libc state (build-musl.sh points src/internal/libc.c at tee_auxv and
 * a 4 KiB page size, and src/env/__environ.c at tee_envp) and the one thread. Built inside the
 * musl tree (src/tee/), so it sees musl's internals.
 *
 * The thread pointer belongs to OP-TEE, which lays out the TA's TLS itself; musl's per-thread
 * state (errno, locale) lives in __tee_main_thread, since a TA instance runs one thread.
 */
#include "libc.h"
#include "pthread_impl.h"

size_t tee_auxv[2];	/* just AT_NULL */
char *tee_envp[1];

struct pthread __tee_main_thread = {
	.self = &__tee_main_thread,
	.tid = 1,
	.detach_state = DT_JOINABLE,
	.locale = &libc.global_locale,
	.robust_list.head = &__tee_main_thread.robust_list.head,
};

extern uintptr_t __stack_chk_guard;

/*
 * Whether mallocng may put new objects in a group at p (see build-musl.sh). Every group, unless the
 * TA's shim says otherwise (shim/tee_mt.c: a main instance sharing memory wants only pool groups).
 */
weak int __tee_malloc_mem_ok(const void *p)	/* musl's weak: __attribute__((__weak__)) */
{
	(void)p;
	return 1;
}

/*
 * No __init_ssp(): it would replace __stack_chk_guard while the TA's entry functions, built with
 * the stack protector, are on the stack holding the old value, and their checks would fail on
 * return. The thread just copies the current guard.
 */
void __tee_libc_init(void)
{
	__progname = __progname_full = "ta";
	__tee_main_thread.canary = __stack_chk_guard;
}
