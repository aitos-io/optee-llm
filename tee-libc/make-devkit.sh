#!/bin/bash
# Makes tee-libc's copy of the TA dev kit: the same as OP-TEE's export-ta_arm64 except for the link
# script. OP-TEE 4.11's ta.ld.S gives .rela.got its own output section after the page break, in the
# RW segment, while .rela.dyn lands in the RX segment. DT_RELASZ then covers both, a range that
# crosses the gap between segments, and ldelf rejects the TA (TEE_ERROR_BAD_FORMAT). Plain C TAs
# have hardly any GOT relocations; musl and libstdc++, built -fPIC, have many. The copy keeps all
# of .rela.got inside .rela.dyn, before the page break.
# Usage: ./make-devkit.sh <optee_os>/out/<platform>/export-ta_arm64
# Output: $BUILD/tee-libc/devkit, the TA_DEV_KIT_DIR for TAs on tee-libc.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
SRC=${1:?usage: $0 <optee_os out dir>/export-ta_arm64}
DST=${OPTEE_LLM_BUILD:-$here/../build}/tee-libc/devkit

rm -rf "$DST"
cp -a "$SRC" "$DST"
LDS=$DST/src/ta.ld.S
sed -i '/^\t\.rela\.got : { \*(\.rela\.got) }$/d' "$LDS"
sed -i 's|^\t/\* Page align to allow dropping execute bit for RW data \*/$|\t.rela.dyn : { *(.rela.dyn) *(.rela.got) }\n\n&|' "$LDS"
grep -q '\.rela\.dyn : { \*(\.rela\.dyn) \*(\.rela\.got) }' "$LDS"
! grep -q '^\t\.rela\.got :' "$LDS"

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
