/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LLM_ENGINE_H
#define LLM_ENGINE_H

/*
 * A minimal text generator on llama.cpp's API, one model and one context at a time. The same
 * source builds into the TA (against tee-libc) and into a Linux reference program (against a
 * normal llama.cpp build), so both run identical llama.cpp code and their outputs can be compared.
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Loads the GGUF at path (mmap'd) and makes a context of n_ctx tokens (0: as large as fits, up to
 * 4096); n_threads 1 = everything on this thread. cache_k/cache_v name the KV cache types as ggml
 * does ("f16", "q8_0", ...); NULL or "" keeps the default. A quantized V cache turns flash
 * attention on, as llama.cpp requires. Returns 0; -1 bad model, -2 out of memory, -3 bad cache type.
 */
int llm_load(const char *path, int n_ctx, int n_threads, const char *cache_k, const char *cache_v);

/*
 * Greedy generation from prompt (tokenized with BOS and special tokens), up to n_predict tokens or
 * end of generation. Writes the generated ids to ids (*n_ids of them, at most max_ids) and their
 * text, NUL-terminated, to text. Returns 0, or a negative error.
 */
int llm_generate(const char *prompt, int n_predict, int32_t *ids, int max_ids, int *n_ids,
		 char *text, size_t text_cap);

void llm_free(void);

/* The loaded model's general.architecture and context size; -1 when none is loaded. */
int llm_info(char *arch, size_t arch_cap, int *n_ctx);

/* Threads for the next llm_generate calls (the caller's thread and n_threads - 1 more). */
void llm_set_threads(int n_threads);

/*
 * Per-instance setup a TA worker instance needs before running llama.cpp's compute threads:
 * ggml's CPU tables live in each instance's own memory.
 */
void llm_worker_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LLM_ENGINE_H */
