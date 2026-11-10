// SPDX-License-Identifier: BSD-2-Clause
// The Linux reference: the same engine on a normal llama.cpp build, printing what the TA prints.
// Usage: ref <model.gguf> <prompt> [n_predict] [n_threads] [cache_k] [cache_v]
#include <stdio.h>
#include <stdlib.h>

#include "engine.h"

int main(int argc, char **argv)
{
	static int32_t ids[1024];
	static char text[16384];
	int n_ids = 0;
	int i = 0;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <model.gguf> <prompt> [n_predict] [n_threads]\n", argv[0]);
		return 2;
	}
	if (llm_load(argv[1], 512, argc > 4 ? atoi(argv[4]) : 1, argc > 5 ? argv[5] : NULL,
		     argc > 6 ? argv[6] : NULL))
		return 1;
	if (llm_generate(argv[2], argc > 3 ? atoi(argv[3]) : 32, ids, 1024, &n_ids, text,
			 sizeof(text)))
		return 1;
	printf("ids:");
	for (i = 0; i < n_ids; i++)
		printf(" %d", ids[i]);
	printf("\ntext: %s\n", text);
	llm_free();
	return 0;
}
