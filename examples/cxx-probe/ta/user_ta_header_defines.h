/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <cxxprobe_ta.h>

#define TA_UUID			TA_CXXPROBE_UUID
#define TA_FLAGS		TA_FLAG_EXEC_DDR
#define TA_STACK_SIZE		(64 * 1024)
#define TA_DATA_SIZE		(128 * 1024 * 1024)

#define TA_VERSION		"1.0"
#define TA_DESCRIPTION		"C++ runtime checks for tee-libc"

#endif /* USER_TA_HEADER_DEFINES_H */
