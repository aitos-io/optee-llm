#!/bin/bash
# Adds optee-llm's core pieces to an optee_os source tree (tested with 4.11.0); each step is
# skipped when already done. Then build optee_os for your platform as usual (see README.md).
#   - pta/tee_mt_shm.c: the pool that a TA's instances share, for threads on several cores
#   - patches/condemn.patch: ends the instances left spinning when one sharing the pool panics
#   - patches/user-va-slots.patch (only with --va-slots): CFG_USER_VA_SLOTS, more than 1 GiB of
#     address space per TA (LPAE)
# Usage: optee/install.sh <optee_os> [--va-slots]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
OS=${1:?usage: $0 <optee_os> [--va-slots]}

cp "$here"/pta/tee_mt_shm.c "$here"/pta/pta_tee_mt_shm.h "$OS/core/pta/"
grep -q tee_mt_shm.c "$OS/core/pta/sub.mk" || echo "srcs-y += tee_mt_shm.c" >> "$OS/core/pta/sub.mk"
grep -q thread_user_ctx_condemned "$OS/core/arch/arm/kernel/thread.c" ||
	patch -d "$OS" -p1 < "$here/patches/condemn.patch"
if [ "${2:-}" = --va-slots ]; then
	grep -q CFG_USER_VA_SLOTS "$OS/core/arch/arm/mm/core_mmu_lpae.c" ||
		patch -d "$OS" -p1 < "$here/patches/user-va-slots.patch"
fi
echo "optee-llm added to $OS"
