#!/bin/bash
# Makes tee-libc's copy of the TA dev kit: OP-TEE's export-ta_arm64 with the two changes below.
# Usage: ./make-devkit.sh <optee_os>/out/<platform>/export-ta_arm64
# Output: $BUILD/tee-libc/devkit, the TA_DEV_KIT_DIR for TAs on tee-libc.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
SRC=${1:?usage: $0 <optee_os out dir>/export-ta_arm64}
DST=${OPTEE_LLM_BUILD:-$here/../build}/tee-libc/devkit

rm -rf "$DST"
cp -a "$SRC" "$DST"
# libstdc++ has STB_GNU_UNIQUE symbols (static members of templates), so ld marks the TA's
# EI_OSABI as GNU, and ldelf accepts only ELFOSABI_NONE. Unique binding only matters for
# de-duplicating across shared objects, and a TA is one static module: reset the byte after linking.
LINK=$DST/mk/link.mk
sed -i 's|^\t$(q)$(LD$(sm)) $(ldargs-$(user-ta-uuid).elf) -o $$@$|&\n\t$(q)printf "\\\\000" \| dd of=$$@ bs=1 seek=7 conv=notrunc status=none|' "$LINK"
grep -q 'seek=7 conv=notrunc' "$LINK"

# One allocator for the whole TA: musl's mallocng (tee-libc). libutils' bget allocator suits a TA's
# few large blocks, but a tokenizer makes a million small ones, and bget took minutes for what
# takes a fraction of a second on Linux. Weakened here, libutils' entry points yield to musl's, so
# libutee's TEE_Malloc/TEE_Free (calloc/free) and C++ new end up in the same allocator. bget stays
# underneath: the shim takes mallocng's pages from it with malloc_flags().
objcopy --weaken-symbol=malloc --weaken-symbol=calloc --weaken-symbol=realloc --weaken-symbol=free \
	--weaken-symbol=memalign --weaken-symbol=aligned_alloc "$DST/lib/libutils.a"
[ "$(nm "$DST/lib/libutils.a" 2>/dev/null | grep -cE ' W (malloc|calloc|realloc|free|memalign|aligned_alloc)$')" = 6 ]
echo "dev kit at $DST (from $SRC)"
