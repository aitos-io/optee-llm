global-incdirs-y += include
global-incdirs-y += ../../../tee-libc/shim/include

srcs-y += cxxprobe_ta.c
srcs-y += ../../../tee-libc/shim/tee_syscall.c
srcs-y += ../../../tee-libc/shim/tee_unwind.c
srcs-y += ../../../tee-libc/shim/tee_vfile.c
srcs-y += ../../../tee-libc/shim/tee_mt.c
srcs-y += ../../../tee-libc/shim/tee_mt_stack.S
