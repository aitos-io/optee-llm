// SPDX-License-Identifier: BSD-2-Clause
// The generator behind engine.h, on llama.cpp's public API.
#include "engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ggml-cpu.h"
#include "ggml.h"
#include "llama.h"

namespace {

llama_model *g_model;
llama_context *g_ctx;
llama_sampler *g_smpl;

// ggml's type for a name such as "tbq3_0", or -1.
int type_from_name(const char *name)
{
	for (int t = 0; t < GGML_TYPE_COUNT; t++) {
		const char *n = ggml_type_name((ggml_type)t);
		if (n && !strcmp(n, name))
			return t;
	}
	return -1;
}

// n_ctx 0: try these context sizes, halving, keeping kAutoReserve free. Only a failure to allocate
// the KV cache is recoverable: when the KV cache fits but the scheduler's buffers then do not,
// ggml aborts (GGML_ASSERT), which panics the TA. So the first try is modest.
constexpr int kAutoCtxMax = 4096;
constexpr int kAutoCtxMin = 256;
constexpr size_t kAutoReserve = 16u << 20;

}  // namespace

extern "C" int llm_load(const char *path, int n_ctx, int n_threads, const char *cache_k,
			const char *cache_v)
{
	llm_free();
	llama_backend_init();

	llama_model_params mp = llama_model_default_params();
	mp.n_gpu_layers = 0;
	mp.load_mode = LLAMA_LOAD_MODE_MMAP;
	g_model = llama_model_load_from_file(path, mp);
	if (!g_model)
		return -1;

	llama_context_params cp = llama_context_default_params();
	cp.n_ctx = n_ctx;
	cp.n_batch = n_ctx;
	// The compute buffer is sized for a full micro-batch of logits (n_ubatch x vocabulary): 512
	// would take ~260 MiB with Llama 3's 128k vocabulary, more than a TA can spare next to the
	// weights. 64 keeps it small; a prompt still goes in one llama_decode call.
	cp.n_ubatch = 64;
	cp.n_threads = n_threads;
	cp.n_threads_batch = n_threads;
	cp.no_perf = true;
	if (cache_k && *cache_k) {
		const int t = type_from_name(cache_k);
		if (t < 0)
			return -3;
		cp.type_k = (ggml_type)t;
	}
	if (cache_v && *cache_v) {
		const int t = type_from_name(cache_v);
		if (t < 0)
			return -3;
		cp.type_v = (ggml_type)t;
		if (ggml_is_quantized(cp.type_v))
			cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
	}
	if (n_ctx > 0) {
		g_ctx = llama_init_from_model(g_model, cp);
	} else {
		/*
		 * As large as fits, up to the model's training context and kAutoCtxMax, while
		 * kAutoReserve stays free for requests and responses. (Written to, or the
		 * compiler drops the malloc/free pair.)
		 */
		volatile char *reserve = (volatile char *)malloc(kAutoReserve);
		if (!reserve)
			return -2;
		reserve[0] = 0;
		int n = std::min(llama_model_n_ctx_train(g_model), kAutoCtxMax);
		for (; !g_ctx && n >= kAutoCtxMin; n /= 2) {
			cp.n_ctx = cp.n_batch = n;
			g_ctx = llama_init_from_model(g_model, cp);
		}
		free((void *)reserve);
	}
	if (!g_ctx)
		return -2;

	g_smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
	llama_sampler_chain_add(g_smpl, llama_sampler_init_greedy());
	return 0;
}

extern "C" int llm_generate(const char *prompt, int n_predict, int32_t *ids, int max_ids,
			    int *n_ids, char *text, size_t text_cap)
{
	if (!g_ctx)
		return -1;
	const llama_vocab *vocab = llama_model_get_vocab(g_model);

	// A fresh conversation each time.
	llama_memory_clear(llama_get_memory(g_ctx), true);
	llama_sampler_reset(g_smpl);

	const int n = -llama_tokenize(vocab, prompt, (int32_t)strlen(prompt), nullptr, 0, true, true);
	std::vector<llama_token> tokens(n);
	if (llama_tokenize(vocab, prompt, (int32_t)strlen(prompt), tokens.data(), n, true, true) != n)
		return -2;

	std::string out;
	llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
	llama_token next = 0;
	*n_ids = 0;
	for (int i = 0; i < n_predict; i++) {
		if (llama_decode(g_ctx, batch))
			return -3;
		next = llama_sampler_sample(g_smpl, g_ctx, -1);
		if (llama_vocab_is_eog(vocab, next))
			break;
		if (*n_ids < max_ids)
			ids[(*n_ids)++] = next;
		char piece[256];
		const int len = llama_token_to_piece(vocab, next, piece, sizeof(piece), 0, true);
		if (len > 0)
			out.append(piece, len);
		batch = llama_batch_get_one(&next, 1);
	}
	const size_t m = out.size() < text_cap - 1 ? out.size() : text_cap - 1;
	memcpy(text, out.data(), m);
	text[m] = 0;
	return 0;
}

extern "C" void llm_free(void)
{
	if (g_smpl)
		llama_sampler_free(g_smpl);
	if (g_ctx)
		llama_free(g_ctx);
	if (g_model)
		llama_model_free(g_model);
	g_smpl = nullptr;
	g_ctx = nullptr;
	g_model = nullptr;
}

extern "C" void llm_set_threads(int n_threads)
{
	if (g_ctx)
		llama_set_n_threads(g_ctx, n_threads, n_threads);
}

extern "C" void llm_worker_init(void)
{
	llama_backend_init();
	ggml_cpu_init();
}

extern "C" int llm_info(char *arch, size_t arch_cap, int *n_ctx)
{
	if (!g_model) {
		snprintf(arch, arch_cap, "%s", "");
		*n_ctx = 0;
		return -1;
	}
	llama_model_meta_val_str(g_model, "general.architecture", arch, arch_cap);
	*n_ctx = g_ctx ? (int)llama_n_ctx(g_ctx) : 0;
	return 0;
}
