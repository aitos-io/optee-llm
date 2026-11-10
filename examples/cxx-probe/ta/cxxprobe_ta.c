// SPDX-License-Identifier: BSD-2-Clause
/*
 * The C side of the probe TA: TEE entry points, tee-libc setup, and calls into the C++ checks
 * and the allocation benchmark in lib/libprobe.a.
 */
#include <tee_internal_api.h>
#include <tee_libc.h>

#include <cxxprobe_ta.h>

uint32_t cxxprobe_run(void);
uint64_t cxxprobe_alloc_bench(void);

TEE_Result TA_CreateEntryPoint(void)
{
	tee_libc_init();
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t types __unused, TEE_Param params[4] __unused,
				    void **ctx __unused)
{
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *ctx __unused)
{
}

TEE_Result TA_InvokeCommandEntryPoint(void *ctx __unused, uint32_t cmd, uint32_t types,
				      TEE_Param params[4])
{
	tee_libc_enter();
	if (types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_NONE,
				     TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	switch (cmd) {
	case TA_CXXPROBE_CMD_RUN:
		params[0].value.a = cxxprobe_run();
		return TEE_SUCCESS;
	case TA_CXXPROBE_CMD_BENCH:
		params[0].value.a = (uint32_t)cxxprobe_alloc_bench();
		return TEE_SUCCESS;
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
}
