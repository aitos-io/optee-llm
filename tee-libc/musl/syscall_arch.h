/*
 * musl for OP-TEE TAs: a TA has no Linux kernel under it, so instead of "svc 0" every system call
 * goes to __tee_syscall(), which the TA links from tee-libc's shim. Replaces
 * arch/aarch64/syscall_arch.h. No vDSO either.
 */
#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

long __tee_syscall(long n, long a, long b, long c, long d, long e, long f);

static inline long __syscall0(long n) { return __tee_syscall(n, 0, 0, 0, 0, 0, 0); }
static inline long __syscall1(long n, long a) { return __tee_syscall(n, a, 0, 0, 0, 0, 0); }
static inline long __syscall2(long n, long a, long b) { return __tee_syscall(n, a, b, 0, 0, 0, 0); }
static inline long __syscall3(long n, long a, long b, long c) { return __tee_syscall(n, a, b, c, 0, 0, 0); }
static inline long __syscall4(long n, long a, long b, long c, long d) { return __tee_syscall(n, a, b, c, d, 0, 0); }
static inline long __syscall5(long n, long a, long b, long c, long d, long e) { return __tee_syscall(n, a, b, c, d, e, 0); }
static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f) { return __tee_syscall(n, a, b, c, d, e, f); }

#define IPC_64 0
