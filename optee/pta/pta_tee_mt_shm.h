/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Shared secure memory for the instances of one TA (pseudo-TA in the OP-TEE
 * core, see tee_mt_shm.c), used by tee-libc's threads (shim/tee_mt.c). One
 * instance owns a pool that it allocates from; other instances of the same TA
 * map the same pool at the same address and run threads for it on other
 * cores. Only TAs may open sessions; the normal world may not. The pool
 * belongs to the TA (UUID) that first creates it, until reboot.
 */
#ifndef PTA_TEE_MT_SHM_H
#define PTA_TEE_MT_SHM_H

/* b2748942-9b2a-45fd-9864-3895448e603a */
#define PTA_TEE_MT_SHM_UUID \
	{ 0xb2748942, 0x9b2a, 0x45fd, \
		{ 0x98, 0x64, 0x38, 0x95, 0x44, 0x8e, 0x60, 0x3a } }

#define PTA_TEE_MT_SHM_ROLE_OWNER	0
#define PTA_TEE_MT_SHM_ROLE_WORKER	1

/*
 * Map the pool into the calling instance, at the same address in every
 * instance. The first owner call allocates it (zeroed); its size stays. One
 * owner at a time: another live owner makes it TEE_ERROR_BUSY. A worker
 * call before the pool exists is TEE_ERROR_BAD_STATE. A TA other than the
 * one the pool was created for gets TEE_ERROR_ACCESS_DENIED.
 * [in]  value[0].a  size in bytes (owner; page multiple)   .b  role
 * [out] value[1].a  address, low 32 bits   .b  high 32 bits
 * [out] value[2].a  size in bytes          .b  1 if just allocated
 */
#define PTA_TEE_MT_SHM_CMD_MAP		0

#endif /* PTA_TEE_MT_SHM_H */
