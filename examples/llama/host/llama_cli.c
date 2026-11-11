// SPDX-License-Identifier: BSD-2-Clause
// Uploads a GGUF into the llama TA and generates once, printing what engine/ref prints.
// Usage: [LLAMA_THREADS=n] llama_cli <model.gguf> <prompt> [n_predict] [cache_k cache_v]
// LLAMA_THREADS > 1 starts n-1 worker sessions (TA_LLAMA_CMD_WORK), one thread each, that lend
// the TA their cores; the TA then computes on up to n threads.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tee_client_api.h>

#include "../ta/include/llama_ta.h"

#define CHUNK LLAMA_TA_CHUNK_SIZE

static TEEC_Context ctx;
static TEEC_Session sess;
static atomic_int done;

static int invoke(uint32_t cmd, TEEC_Operation *op, const char *what)
{
	uint32_t origin = 0;
	TEEC_Result res = TEEC_InvokeCommand(&sess, cmd, op, &origin);

	if (res != TEEC_SUCCESS)
		fprintf(stderr, "%s: 0x%x (origin %u)\n", what, res, origin);
	return res == TEEC_SUCCESS ? 0 : -1;
}

/* A worker session: serve until the main session is done (WORK returns when idle a while). */
static void *worker(void *arg)
{
	TEEC_UUID uuid = TA_LLAMA_UUID;
	TEEC_Session ws;
	uint32_t origin = 0;

	(void)arg;
	if (TEEC_OpenSession(&ctx, &ws, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin) != TEEC_SUCCESS)
		return NULL;
	while (!atomic_load(&done)) {
		TEEC_Operation op = { .paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE,
								     TEEC_NONE) };
		TEEC_Result res = TEEC_InvokeCommand(&ws, TA_LLAMA_CMD_WORK, &op, &origin);

		if (res == TEEC_ERROR_BAD_STATE) {
			/* no main instance yet: try again shortly */
			struct timespec ts = { 0, 50000000 };

			nanosleep(&ts, NULL);
		} else if (res != TEEC_SUCCESS) {
			fprintf(stderr, "work: 0x%x (origin %u)\n", res, origin);
			break;
		}
	}
	TEEC_CloseSession(&ws);
	return NULL;
}

static void finish(TEEC_SharedMemory *shm, pthread_t *workers, int n_threads)
{
	int i = 0;

	atomic_store(&done, 1);
	for (i = 1; i < n_threads; i++)
		pthread_join(workers[i], NULL);
	TEEC_ReleaseSharedMemory(shm);
	TEEC_CloseSession(&sess);
	TEEC_FinalizeContext(&ctx);
}

int main(int argc, char **argv)
{
	TEEC_UUID uuid = TA_LLAMA_UUID;
	TEEC_SharedMemory shm = { .size = CHUNK, .flags = TEEC_MEM_INPUT };
	TEEC_Operation op = { 0 };
	static int32_t ids[1024];
	static char text[16384];
	char cache[64] = "";
	char err[256] = "";
	pthread_t workers[8];
	int n_threads = getenv("LLAMA_THREADS") ? atoi(getenv("LLAMA_THREADS")) : 1;
	struct timespec t0, t1;
	uint32_t origin = 0;
	TEEC_Result res = 0;
	FILE *f = NULL;
	long size = 0;
	long off = 0;
	size_t i = 0;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <model.gguf> <prompt> [n_predict] [cache_k cache_v]\n", argv[0]);
		return 2;
	}
	if (n_threads < 1 || n_threads > 8)
		n_threads = 1;
	f = fopen(argv[1], "rb");
	if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
		perror(argv[1]);
		return 1;
	}
	res = TEEC_InitializeContext(NULL, &ctx);
	if (!res)
		res = TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
	if (!res)
		res = TEEC_AllocateSharedMemory(&ctx, &shm);
	if (res) {
		fprintf(stderr, "cannot reach the llama TA: 0x%x (origin %u)\n", res, origin);
		return 1;
	}

	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	op.params[0].value.a = size;
	if (invoke(TA_LLAMA_CMD_LOAD_BEGIN, &op, "load begin"))
		return 1;
	/* The main instance (and its pool) exists now: workers can attach. */
	for (i = 1; i < (size_t)n_threads; i++)
		pthread_create(&workers[i], NULL, worker, NULL);
	for (off = 0; off < size; off += CHUNK) {
		size_t n = fread(shm.buffer, 1, CHUNK, f);

		memset(&op, 0, sizeof(op));
		op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_MEMREF_PARTIAL_INPUT, TEEC_NONE,
						 TEEC_NONE);
		op.params[0].value.a = off;
		op.params[1].memref.parent = &shm;
		op.params[1].memref.size = n;
		if (invoke(TA_LLAMA_CMD_LOAD_CHUNK, &op, "load chunk"))
			return 1;
	}
	fclose(f);
	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT,
					 TEEC_NONE);
	op.params[0].value.a = 512;
	op.params[1].tmpref.buffer = err;
	op.params[1].tmpref.size = sizeof(err);
	if (argc > 5) {
		snprintf(cache, sizeof(cache), "%s,%s", argv[4], argv[5]);
		op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_MEMREF_TEMP_OUTPUT,
						 TEEC_VALUE_OUTPUT, TEEC_MEMREF_TEMP_INPUT);
		op.params[3].tmpref.buffer = cache;
		op.params[3].tmpref.size = strlen(cache);
	}
	if (invoke(TA_LLAMA_CMD_LOAD_END, &op, "load end")) {
		fprintf(stderr, "%s\n", err);
		return 1;
	}
	memset(&op, 0, sizeof(op));
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_INPUT,
					 TEEC_MEMREF_TEMP_OUTPUT, TEEC_MEMREF_TEMP_OUTPUT);
	op.params[0].tmpref.buffer = argv[2];
	op.params[0].tmpref.size = strlen(argv[2]);
	op.params[1].value.a = argc > 3 ? atoi(argv[3]) : 32;
	op.params[1].value.b = n_threads;
	op.params[2].tmpref.buffer = ids;
	op.params[2].tmpref.size = sizeof(ids);
	op.params[3].tmpref.buffer = text;
	op.params[3].tmpref.size = sizeof(text) - 1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (invoke(TA_LLAMA_CMD_GENERATE, &op, "generate"))
		return 1;
	clock_gettime(CLOCK_MONOTONIC, &t1);
	fprintf(stderr, "generate: %.2f s for %zu tokens\n",
		(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9,
		op.params[2].tmpref.size / sizeof(int32_t));
	text[op.params[3].tmpref.size] = 0;
	printf("ids:");
	for (i = 0; i < op.params[2].tmpref.size / sizeof(int32_t); i++)
		printf(" %d", ids[i]);
	printf("\ntext: %s\n", text);

	finish(&shm, workers, n_threads);
	return 0;
}
