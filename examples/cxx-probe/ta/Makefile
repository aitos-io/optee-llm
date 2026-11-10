CFG_TEE_TA_LOG_LEVEL ?= 3
CPPFLAGS += -DCFG_TEE_TA_LOG_LEVEL=$(CFG_TEE_TA_LOG_LEVEL)

# The UUID of the TA; the output file is <UUID>.ta
BINARY = cf398d28-8a9a-4766-8ebd-5f317ca7fc19

# The C++ code and the runtime it needs: lib/libprobe.a, then tee-libc.
include ../../../tee-libc/flags.mk
LDADD += ../lib/libprobe.a $(TEE_LIBC_LDADD)

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk
