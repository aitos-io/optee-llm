# Builds a CMake project (llama.cpp, say) as static libraries for an OP-TEE TA on tee-libc: the
# host's AArch64 GCC, but musl's and Alpine libstdc++'s headers instead of glibc's (see flags.mk,
# which this mirrors). Nothing here links an executable, so try_compile checks build static
# libraries. TEE_LIBC in the environment overrides where tee-libc was built.
if(DEFINED ENV{TEE_LIBC})
	set(TEE_LIBC $ENV{TEE_LIBC})
else()
	set(TEE_LIBC ${CMAKE_CURRENT_LIST_DIR}/../build/tee-libc)
endif()
execute_process(COMMAND gcc -print-file-name=include OUTPUT_VARIABLE TEE_LIBC_GCC_INC
	OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_codegen "-fpic -fno-stack-protector -mno-outline-atomics -ffunction-sections -fdata-sections")
set(_c_inc "-isystem ${TEE_LIBC}/out/include -isystem ${TEE_LIBC_GCC_INC}")
set(_cxx_inc "-isystem ${TEE_LIBC}/sysroot/usr/include/c++/14.2.0 -isystem ${TEE_LIBC}/sysroot/usr/include/c++/14.2.0/aarch64-alpine-linux-musl -isystem ${TEE_LIBC}/sysroot/usr/include/c++/14.2.0/backward")
# TEE_EXTRA_FLAGS (-D on the cmake command line) is appended; setting CMAKE_C_FLAGS there instead
# would replace these, header paths included.
set(CMAKE_C_FLAGS_INIT "${_codegen} -nostdinc ${_c_inc} ${TEE_EXTRA_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${_codegen} -nostdinc -nostdinc++ ${_cxx_inc} ${_c_inc} ${TEE_EXTRA_FLAGS}")
