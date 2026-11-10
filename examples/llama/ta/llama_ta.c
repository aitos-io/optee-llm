// SPDX-License-Identifier: BSD-2-Clause
/*
 * llama.cpp in a TA (see llama_ta.h). The model arrives over shared memory in chunks and becomes a
 * tee-libc file, /tee/model.gguf, which llama.cpp opens and mmaps like any file: the upload buffer
 * is the only copy of the weights. Everything llama.cpp allocates comes from the pool that worker
 * instances share, and computations run on a stack in that pool (tee_mt_call), so the workers can
 * lend their cores.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <tee_libc.h>

#include <llama_ta.h>
#include "../engine/engine.h"

#define MODEL_PATH	"/tee/model.gguf"
#define MAX_THREADS	8

int setenv(const char *name, const char *value, int overwrite);

static uint8_t *model_buf;
static size_t model_size;
static size_t model_received;
static bool loaded;

static void drop_model(void)
{
	llm_free();
	loaded = false;
	free(model_buf);
	model_buf = NULL;
	model_size = 0;
	model_received = 0;
}

TEE_Result TA_CreateEntryPoint(void)
{
	tee_libc_init();
	/*
	 * On GGML_ABORT, ggml prints a backtrace by reading /proc and forking gdb: neither exists in a
	 * TA, and the attempt crashes before the abort message is out of the way.
	 */
	setenv("GGML_NO_BACKTRACE", "1", 1);
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
	tee_libc_enter();
	drop_model();
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t types __unused, TEE_Param params[4] __unused,
				    void **ctx __unused)
{
	tee_libc_enter();
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *ctx __unused)
{
	tee_libc_enter();
}

static TEE_Result cmd_load_begin(uint32_t pt, TEE_Param p[4])
{
	TEE_Result r = TEE_SUCCESS;

	if (pt != TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_NONE,
				  TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	drop_model();
	if (!p[0].value.a)
		return TEE_ERROR_BAD_PARAMETERS;
	/*
	 * Become the main instance: from here on everything llama.cpp allocates, the weights
	 * included, comes from the pool that worker instances map at the same address.
	 */
	r = tee_mt_main_attach((size_t)LLAMA_TA_POOL_MB << 20);
	if (r)
		return r;
	/* Page aligned, so that llama.cpp's mmap of the file is the buffer itself. */
	model_buf = aligned_alloc(4096, (p[0].value.a + 4095) & ~(size_t)4095);
	if (!model_buf)
		return TEE_ERROR_OUT_OF_MEMORY;
	model_size = p[0].value.a;
	return TEE_SUCCESS;
}

static TEE_Result cmd_load_chunk(uint32_t pt, TEE_Param p[4])
{
	size_t off = 0, len = 0;

	if (pt != TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_MEMREF_INPUT,
				  TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	off = p[0].value.a;
	len = p[1].memref.size;
	if (!model_buf || off > model_size || len > model_size - off)
		return TEE_ERROR_BAD_PARAMETERS;
	memcpy(model_buf + off, p[1].memref.buffer, len);
	model_received += len;
	return TEE_SUCCESS;
}

static TEE_Result fail_load(TEE_Param *msg, TEE_Result r, const char *what)
{
	snprintf(msg->memref.buffer, msg->memref.size, "%s", what);
	EMSG("model load failed: %s", what);
	drop_model();
	return r;
}

static TEE_Result cmd_load_end(uint32_t pt, TEE_Param p[4])
{
	char cache[32] = { };
	char *cache_v = NULL;
	char name[32] = "";
	int n_ctx = 0;
	int rc = 0;

	if ((pt != TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_MEMREF_OUTPUT,
				   TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_NONE) &&
	     pt != TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_MEMREF_OUTPUT,
				   TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_MEMREF_INPUT)))
		return TEE_ERROR_BAD_PARAMETERS;
	if (!model_buf || model_received != model_size)
		return TEE_ERROR_BAD_STATE;
	if (TEE_PARAM_TYPE_GET(pt, 3) == TEE_PARAM_TYPE_MEMREF_INPUT) {
		if (p[3].memref.size >= sizeof(cache))
			return TEE_ERROR_BAD_PARAMETERS;
		memcpy(cache, p[3].memref.buffer, p[3].memref.size);
		cache_v = strchr(cache, ',');
		if (cache_v)
			*cache_v++ = 0;
	}
	if (tee_vfile_add(MODEL_PATH, model_buf, model_size))
		return fail_load(&p[1], TEE_ERROR_GENERIC, "no room for the model file");
	rc = llm_load(MODEL_PATH, (int)p[0].value.a, 1, cache, cache_v);
	if (rc == -1)
		return fail_load(&p[1], TEE_ERROR_BAD_FORMAT, "llama.cpp could not load the model");
	if (rc == -3)
		return fail_load(&p[1], TEE_ERROR_BAD_PARAMETERS, "unknown KV cache type");
	if (rc)
		return fail_load(&p[1], TEE_ERROR_OUT_OF_MEMORY, "out of memory for the context");
	llm_info(name, sizeof(name), &n_ctx);
	loaded = true;
	p[1].memref.size = 0;
	p[2].value.a = n_ctx;
	IMSG("model loaded: %s, %zu bytes, context %d", name, model_size, n_ctx);
	return TEE_SUCCESS;
}

struct gen_call {
	const char *prompt;
	int n_predict;
	int32_t *ids;
	int max_ids;
	int *n_ids;
	char *text;
	size_t text_cap;
	int rc;
};

static void gen_on_pool(void *arg)
{
	struct gen_call *g = arg;

	g->rc = llm_generate(g->prompt, g->n_predict, g->ids, g->max_ids, g->n_ids, g->text,
			     g->text_cap);
}

static TEE_Result cmd_generate(uint32_t pt, TEE_Param p[4])
{
	struct gen_call g = { };
	char *prompt = NULL;
	int n_threads = 0;
	int n_ids = 0;

	if (pt != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_VALUE_INPUT,
				  TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_MEMREF_OUTPUT) ||
	    !p[3].memref.size)
		return TEE_ERROR_BAD_PARAMETERS;
	if (!loaded)
		return TEE_ERROR_BAD_STATE;
	/* In the pool, like everything the computation reads. */
	prompt = malloc(p[0].memref.size + 1);
	if (!prompt)
		return TEE_ERROR_OUT_OF_MEMORY;
	memcpy(prompt, p[0].memref.buffer, p[0].memref.size);
	prompt[p[0].memref.size] = 0;
	g.prompt = prompt;
	g.n_predict = p[1].value.a;
	g.ids = p[2].memref.buffer;
	g.max_ids = p[2].memref.size / sizeof(int32_t);
	g.n_ids = &n_ids;
	g.text = p[3].memref.buffer;
	g.text_cap = p[3].memref.size;
	n_threads = p[1].value.b > MAX_THREADS ? MAX_THREADS : (int)p[1].value.b;
	llm_set_threads(tee_mt_reserve(n_threads ? n_threads : 1));
	tee_mt_call(gen_on_pool, &g);
	tee_mt_release();
	free(prompt);
	if (g.rc) {
		EMSG("generation failed: %d", g.rc);
		return TEE_ERROR_GENERIC;
	}
	p[2].memref.size = n_ids * sizeof(int32_t);
	p[3].memref.size = strlen(p[3].memref.buffer);
	return TEE_SUCCESS;
}

TEE_Result TA_InvokeCommandEntryPoint(void *ctx __unused, uint32_t cmd, uint32_t pt,
				      TEE_Param p[4])
{
	tee_libc_enter();
	if (cmd == TA_LLAMA_CMD_WORK)
		return pt == TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
					     TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE) ?
		       tee_mt_work(llm_worker_init) : TEE_ERROR_BAD_PARAMETERS;
	/* A worker instance takes nothing else. */
	if (tee_mt_is_worker())
		return TEE_ERROR_BAD_STATE;
	switch (cmd) {
	case TA_LLAMA_CMD_LOAD_BEGIN:
		return cmd_load_begin(pt, p);
	case TA_LLAMA_CMD_LOAD_CHUNK:
		return cmd_load_chunk(pt, p);
	case TA_LLAMA_CMD_LOAD_END:
		return cmd_load_end(pt, p);
	case TA_LLAMA_CMD_GENERATE:
		return cmd_generate(pt, p);
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}
