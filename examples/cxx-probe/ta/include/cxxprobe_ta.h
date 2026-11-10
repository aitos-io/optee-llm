/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef CXXPROBE_TA_H
#define CXXPROBE_TA_H

#define TA_CXXPROBE_UUID \
	{ 0xcf398d28, 0x8a9a, 0x4766, \
		{ 0x8e, 0xbd, 0x5f, 0x31, 0x7c, 0xa7, 0xfc, 0x19 } }

/* RUN: value output a = bitmask of the checks that passed. */
#define TA_CXXPROBE_CMD_RUN 0
/* BENCH: value output a = microseconds for the allocation benchmark (lib/bench.cpp). */
#define TA_CXXPROBE_CMD_BENCH 1

#define CXXPROBE_STATIC_INIT	(1u << 0)
#define CXXPROBE_CONTAINERS	(1u << 1)
#define CXXPROBE_EXCEPTIONS	(1u << 2)
#define CXXPROBE_MUTEX		(1u << 3)
#define CXXPROBE_STREAMS	(1u << 4)
#define CXXPROBE_REGEX		(1u << 5)
#define CXXPROBE_ALL		0x3fu

#endif /* CXXPROBE_TA_H */
