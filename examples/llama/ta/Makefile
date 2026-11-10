CFG_TEE_TA_LOG_LEVEL ?= 3
CPPFLAGS += -DCFG_TEE_TA_LOG_LEVEL=$(CFG_TEE_TA_LOG_LEVEL)

# make LLAMA_POOL_MB=n: another size for the pool the instances share (llama_ta.h)
ifneq ($(LLAMA_POOL_MB),)
CPPFLAGS += -DLLAMA_TA_POOL_MB=$(LLAMA_POOL_MB)
endif

# The UUID of the TA (llama_ta.h); the output file is <UUID>.ta
BINARY = f6af916a-a2b6-4823-87d8-19d3d30d0f90

# engine/ and llama.cpp's static libraries (build-llama.sh), then tee-libc.
include ../../../tee-libc/flags.mk
BUILD ?= $(abspath $(or $(OPTEE_LLM_BUILD),../../../build))
LDADD += --start-group ../engine/libengine-tee.a $(shell find $(BUILD)/llama-tee -name '*.a') \
	--end-group $(TEE_LIBC_LDADD)

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk
