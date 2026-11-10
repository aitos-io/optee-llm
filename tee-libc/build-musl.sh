#!/bin/bash
# Builds musl as the C library for OP-TEE TAs (tee-libc), on an AArch64 Linux host (the device
# itself works), after fetch.sh.
#   - system calls go to __tee_syscall() (musl/syscall_arch.h), implemented by shim/ in the TA
#   - __pthread_self() is one static thread; OP-TEE keeps the thread pointer
#   - malloc is musl's mallocng, on pages the shim carves from the TA heap (anonymous mmap);
#     the dev kit copy weakens libutils' malloc so that OP-TEE's TEE_Malloc uses mallocng too
# Output: $BUILD/tee-libc/out/lib/libc.a and out/include. Usage: ./build-musl.sh
set -euo pipefail
cd "$(dirname "$0")"
BUILD=${OPTEE_LLM_BUILD:-$PWD/../build}
TOP=$BUILD/tee-libc
SRC=$TOP/musl-1.2.5
OUT=$TOP/out

rm -rf "$SRC" "$OUT"
tar -xzf "$BUILD/dl/musl-1.2.5.tar.gz" -C "$TOP"
cp musl/syscall_arch.h "$SRC/arch/aarch64/syscall_arch.h"
sed -i 's|^#define __pthread_self() ((pthread_t)(__get_tp() - sizeof(struct __pthread) - TP_OFFSET))$|extern struct pthread __tee_main_thread;\n#define __pthread_self() (\&__tee_main_thread)|' "$SRC/src/internal/pthread_impl.h"
grep -q '__tee_main_thread' "$SRC/src/internal/pthread_impl.h"
mkdir -p "$SRC/src/tee" && cp musl/tee_init.c "$SRC/src/tee/"
# Static libc state (see tee_init.c): OP-TEE calls malloc before any TA code runs, and mallocng
# reads the page size and auxv on its first call.
sed -i 's|^struct __libc __libc;$|extern size_t tee_auxv[];\nstruct __libc __libc = { .auxv = tee_auxv, .page_size = 4096 };|' "$SRC/src/internal/libc.c"
grep -q 'tee_auxv' "$SRC/src/internal/libc.c"
sed -i 's|^char \*\*__environ = 0;$|extern char *tee_envp[];\nchar **__environ = tee_envp;|' "$SRC/src/env/__environ.c"
grep -q 'tee_envp' "$SRC/src/env/__environ.c"
# mallocng takes new slots only from groups __tee_malloc_mem_ok() accepts (tee_init.c; shim/tee_mt.c):
# once a TA instance shares its memory with workers, groups made before that (in its private
# heap, during static initialization) must not hold new objects, which the workers could not see.
perl -0pi -e 's|(static uint32_t try_avail\(struct meta \*\*pm\)\n\{\n\tstruct meta \*m = \*pm;\n\tuint32_t first;\n)\tif \(!m\) return 0;|$1\twhile (m \&\& !__tee_malloc_mem_ok(m->mem)) {\n\t\tdequeue(pm, m);\n\t\tm = *pm;\n\t}\n\tif (!m) return 0;|' "$SRC/src/malloc/mallocng/malloc.c"
grep -q '__tee_malloc_mem_ok' "$SRC/src/malloc/mallocng/malloc.c"
sed -i 's|^#include "meta.h"$|#include "meta.h"\n\nint __tee_malloc_mem_ok(const void *);|' "$SRC/src/malloc/mallocng/malloc.c"
grep -q '^int __tee_malloc_mem_ok' "$SRC/src/malloc/mallocng/malloc.c"

cd "$SRC"
CC=gcc ./configure --prefix="$OUT" --disable-shared \
	CFLAGS="-fpic -O2 -fno-stack-protector -mno-outline-atomics -ffunction-sections -fdata-sections" >/dev/null
make -j"$(nproc)" lib/libc.a >/dev/null
make install-headers install-libs >/dev/null
# OP-TEE's libutee defines these, and every TA links it: printf/puts/putchar (trace_ext.o, they
# print to the TA log) and __tls_get_addr (tcb.c: OP-TEE lays out the TA's TLS).
ar d "$OUT/lib/libc.a" printf.lo puts.lo putchar.lo __tls_get_addr.lo
# Threads are shim/tee_mt.c's: a TA instance has one, and more cores mean more instances.
ar d "$OUT/lib/libc.a" pthread_create.lo pthread_join.lo
echo "built $OUT/lib/libc.a ($(stat -c %s "$OUT/lib/libc.a") bytes)"
