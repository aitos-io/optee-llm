# Compiler and link settings for C/C++ code that runs in a TA on top of tee-libc.
# Include from a Makefile that builds a static library outside the TA dev kit:
#   TEE_LIBC_CFLAGS / TEE_LIBC_CXXFLAGS compile against musl and Alpine's libstdc++ instead of glibc;
#   TEE_LIBC_LDADD is what the TA's Makefile adds to LDADD.
TEE_LIBC_SRC := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
# Where build-musl.sh and fetch.sh put their output
TEE_LIBC ?= $(abspath $(or $(OPTEE_LLM_BUILD),$(TEE_LIBC_SRC)/../build)/tee-libc)
TEE_LIBC_GCC_INC := $(shell gcc -print-file-name=include)

TEE_LIBC_CODEGEN := -fpic -fno-stack-protector -mno-outline-atomics -ffunction-sections -fdata-sections
# Search order matters: libstdc++'s <cstdlib> uses #include_next <stdlib.h>, so the C++ headers
# come first, then musl, then GCC's own (stdarg.h, arm_neon.h).
TEE_LIBC_C_INC := -isystem $(TEE_LIBC)/out/include -isystem $(TEE_LIBC_GCC_INC)
TEE_LIBC_CXX_INC := -isystem $(TEE_LIBC)/sysroot/usr/include/c++/14.2.0 \
	-isystem $(TEE_LIBC)/sysroot/usr/include/c++/14.2.0/aarch64-alpine-linux-musl \
	-isystem $(TEE_LIBC)/sysroot/usr/include/c++/14.2.0/backward

TEE_LIBC_CFLAGS := $(TEE_LIBC_CODEGEN) -nostdinc $(TEE_LIBC_C_INC)
TEE_LIBC_CXXFLAGS := $(TEE_LIBC_CODEGEN) -nostdinc -nostdinc++ $(TEE_LIBC_CXX_INC) $(TEE_LIBC_C_INC)

# Grouped: libgcc_eh's unwinder needs musl's pthread functions, and libstdc++ needs both.
# The -u options pull in all of musl's allocator up front: libutils' weakened copies (see
# make-devkit.sh) would otherwise satisfy whichever of them the TA's own code happens not to
# call, and libutee could then calloc with bget and free with mallocng.
TEE_LIBC_LDADD := -u malloc -u calloc -u realloc -u free -u aligned_alloc -u memalign \
	-u posix_memalign --start-group $(TEE_LIBC)/sysroot/usr/lib/libstdc++.a $(TEE_LIBC)/out/lib/libc.a \
	$(shell gcc -print-file-name=libgcc_eh.a) --end-group --eh-frame-hdr

# TA sources that go with tee-libc (shim/): add to srcs-y in the TA's sub.mk, with shim/include in
# global-incdirs-y.
TEE_LIBC_TA_SRCS := tee_syscall.c tee_unwind.c tee_vfile.c tee_mt.c tee_mt_stack.S
