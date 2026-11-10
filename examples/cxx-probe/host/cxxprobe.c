// SPDX-License-Identifier: BSD-2-Clause
// Runs the C++ probe TA once and prints which checks passed.
#include <stdio.h>
#include <stdlib.h>
#include <tee_client_api.h>

#include "../ta/include/cxxprobe_ta.h"

static const char *const names[] = {
	"static init", "containers", "exceptions", "mutex", "streams", "regex",
};

int main(void)
{
	TEEC_Context ctx;
	TEEC_Session sess;
	TEEC_Operation op = { 0 };
	TEEC_UUID uuid = TA_CXXPROBE_UUID;
	uint32_t origin = 0;
	TEEC_Result res;
	unsigned int i;

	if (TEEC_InitializeContext(NULL, &ctx) != TEEC_SUCCESS)
		return 1;
	res = TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
	if (res != TEEC_SUCCESS) {
		printf("open session: 0x%x (origin %u)\n", res, origin);
		return 1;
	}
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	res = TEEC_InvokeCommand(&sess, TA_CXXPROBE_CMD_RUN, &op, &origin);
	if (res != TEEC_SUCCESS) {
		printf("run: 0x%x (origin %u)\n", res, origin);
		return 1;
	}
	if (getenv("BENCH")) {
		TEEC_Operation bench = { 0 };

		bench.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
		if (TEEC_InvokeCommand(&sess, TA_CXXPROBE_CMD_BENCH, &bench, &origin) == TEEC_SUCCESS)
			printf("tee alloc bench: %u us\n", bench.params[0].value.a);
	}
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		printf("%-12s %s\n", names[i], op.params[0].value.a & (1u << i) ? "ok" : "FAILED");
	TEEC_CloseSession(&sess);
	TEEC_FinalizeContext(&ctx);
	return op.params[0].value.a == CXXPROBE_ALL ? 0 : 1;
}
