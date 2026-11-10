/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <llama_ta.h>

#define TA_UUID			TA_LLAMA_UUID
/* not single-instance: each session is an instance, and worker sessions lend their cores */
#define TA_FLAGS		TA_FLAG_EXEC_DDR
#define TA_STACK_SIZE		(256 * 1024)
#define TA_DATA_SIZE		(LLAMA_TA_HEAP_MB * 1024 * 1024)

#define TA_VERSION		"0.1"
#define TA_DESCRIPTION		"llama.cpp in the secure world"

#endif /* USER_TA_HEADER_DEFINES_H */
