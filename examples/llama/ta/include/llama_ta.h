/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LLAMA_TA_H
#define LLAMA_TA_H

/*
 * llama.cpp in a TA: one GGUF model in secure memory, generating text from a prompt.
 *
 * Several cores: the session that loads the model is the main instance. Other sessions of the same
 * TA, opened by other client threads, invoke TA_LLAMA_CMD_WORK to lend it their thread's core; the
 * main instance then runs llama.cpp's threads on them (tee-libc's tee_mt).
 */

/* f6af916a-a2b6-4823-87d8-19d3d30d0f90 */
#define TA_LLAMA_UUID \
	{ 0xf6af916a, 0xa2b6, 0x4823, \
		{ 0x87, 0xd8, 0x19, 0xd3, 0xd3, 0x0d, 0x0f, 0x90 } }

/* Each instance's private heap; the model and everything llama.cpp allocates live in the pool. */
#define LLAMA_TA_HEAP_MB	6
/*
 * The pool the instances share. It and the TA itself must fit in one TA's address space: 1 GiB in
 * a stock OP-TEE, more with CFG_USER_VA_SLOTS (optee/patches/user-va-slots.patch). A larger pool
 * builds with make LLAMA_POOL_MB=n (ta/Makefile).
 */
#ifndef LLAMA_TA_POOL_MB
#define LLAMA_TA_POOL_MB	900
#endif

/* Largest shared-memory chunk a client should send per LOAD_CHUNK. */
#define LLAMA_TA_CHUNK_SIZE	(8 * 1024 * 1024)

/* [in] value[0].a  size of the GGUF file in bytes */
#define TA_LLAMA_CMD_LOAD_BEGIN		1

/* [in] value[0].a  offset of this chunk   [in] memref[1]  chunk data */
#define TA_LLAMA_CMD_LOAD_CHUNK		2

/*
 * Load the received file.
 * [in]  value[0].a   context size in tokens (0: as large as fits, up to 4096)
 * [out] memref[1]    error message, on failure
 * [out] value[2].a   context size
 * [in]  memref[3]    optional "cache_k,cache_v": KV cache types by ggml's names, e.g. "q8_0,q8_0"
 */
#define TA_LLAMA_CMD_LOAD_END		3

/*
 * Greedy generation (engine/engine.h).
 * [in]  memref[0]    prompt (not NUL-terminated)
 * [in]  value[1].a   tokens to generate at most   .b  threads (0 or 1: this one only)
 * [out] memref[2]    the generated token ids (int32_t)
 * [out] memref[3]    their text
 */
#define TA_LLAMA_CMD_GENERATE		4

/* Worker: lend this session's thread to the main instance until it is idle a while. */
#define TA_LLAMA_CMD_WORK		5

#endif /* LLAMA_TA_H */
