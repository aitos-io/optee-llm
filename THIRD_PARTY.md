# Third-party components

None of these are stored in the repository; the build scripts download them from their official
sources and check each file against a pinned SHA-256.

| Component | Version | Source | License | Used for |
| --- | --- | --- | --- | --- |
| musl | 1.2.5 | musl.libc.org | MIT | C library of tee-libc (`build-musl.sh` patches a few internals; see the script) |
| libstdc++ | 14.2.0 (Alpine `libstdc++-dev` 14.2.0-r6, aarch64) | dl-cdn.alpinelinux.org, v3.22 | GPL-3.0 with the GCC Runtime Library Exception | C++ runtime of tee-libc |
| libgcc_eh | the build host's GCC | — | GPL-3.0 with the GCC Runtime Library Exception | unwinder for C++ exceptions |
| llama.cpp / ggml | b11777 | github.com/ggml-org/llama.cpp | MIT | `examples/llama` |
| OP-TEE | 4.11.0 (tested) | github.com/OP-TEE | BSD-2-Clause | `optee/` patches and pseudo-TA apply to it |

The GCC Runtime Library Exception allows TAs linked with libstdc++ and libgcc_eh to be distributed
under any license, as long as they are compiled with GCC (an eligible compilation process).
`optee/pta/tee_mt_shm.c` and the patches in `optee/patches/` are written for, and become part of,
optee_os (BSD-2-Clause).
