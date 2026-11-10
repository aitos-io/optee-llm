// SPDX-License-Identifier: BSD-2-Clause
/*
 * What C++ exceptions and static destructors need from the "dynamic linker" in a TA.
 *
 * The board's GCC builds its unwinder (libgcc_eh.a) against glibc, which finds a PC's unwind
 * table with _dl_find_object() (glibc 2.35). OP-TEE has dl_iterate_phdr() instead, so this answers
 * _dl_find_object() from it: the module whose PT_LOAD segments hold the PC, and its
 * PT_GNU_EH_FRAME (.eh_frame_hdr, from the TA's --eh-frame-hdr link).
 */
#include <compiler.h>
#include <elf.h>
#include <link.h>
#include <stdint.h>
#include <string.h>

#include <tee_libc.h>

/* What crtbegin.o would define: the handle __cxa_atexit() registers static destructors under. */
void *__dso_handle = &__dso_handle;

struct find_ctx {
	uintptr_t pc;
	struct dl_find_object *out;
	int found;
};

static int find_cb(struct dl_phdr_info *info, size_t size __unused, void *data)
{
	struct find_ctx *ctx = data;
	uintptr_t start = UINTPTR_MAX;
	uintptr_t end = 0;
	void *eh_frame = NULL;
	int hit = 0;
	Elf_Half i = 0;

	for (i = 0; i < info->dlpi_phnum; i++) {
		const Elf_Phdr *ph = info->dlpi_phdr + i;
		uintptr_t lo = info->dlpi_addr + ph->p_vaddr;
		uintptr_t hi = lo + ph->p_memsz;

		if (ph->p_type == PT_LOAD) {
			if (lo < start)
				start = lo;
			if (hi > end)
				end = hi;
			if (ctx->pc >= lo && ctx->pc < hi)
				hit = 1;
		} else if (ph->p_type == PT_GNU_EH_FRAME) {
			eh_frame = (void *)lo;
		}
	}
	if (!hit)
		return 0;
	memset(ctx->out, 0, sizeof(*ctx->out));
	ctx->out->dlfo_map_start = (void *)start;
	ctx->out->dlfo_map_end = (void *)end;
	ctx->out->dlfo_eh_frame = eh_frame;
	ctx->found = 1;
	return 1;
}

int _dl_find_object(void *pc, struct dl_find_object *result)
{
	struct find_ctx ctx = { .pc = (uintptr_t)pc, .out = result };

	dl_iterate_phdr(find_cb, &ctx);
	return ctx.found ? 0 : -1;
}
