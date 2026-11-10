#!/bin/bash
# Downloads llama.cpp (pinned release, SHA-256 checked) and builds its CPU engine twice as static
# libraries, with the same CPU code generation so that outputs can be compared bit for bit:
#   $BUILD/llama-tee    for the TA, against tee-libc (../../tee-libc/toolchain.cmake)
#   $BUILD/llama-linux  for the Linux reference program (engine/ref)
# Usage: ./build-llama.sh [-jN]
# LLAMA_ARCH=<march>: build for that -march instead of this machine's CPU (GGML_NATIVE), when the
# build host is not the device, e.g. armv8-a+crc for a Cortex-A53 (no dotprod, no fp16).
set -euo pipefail
cd "$(dirname "$0")"
BUILD=${OPTEE_LLM_BUILD:-$(cd ../.. && pwd)/build}
TAG=b11777
SHA=f44c041622d88093e3f91f0c81281ea4914c32f6eedc5810a22a534f95c9b377
SRC=$BUILD/llama.cpp-$TAG
JOBS=${1:--j$(nproc)}

TARBALL=$BUILD/dl/llama.cpp-$TAG.tar.gz
mkdir -p "$BUILD/dl"
if [ ! -f "$TARBALL" ]; then
	curl -fL --retry 3 -o "$TARBALL.part" "https://github.com/ggml-org/llama.cpp/archive/refs/tags/$TAG.tar.gz"
	mv "$TARBALL.part" "$TARBALL"
fi
echo "$SHA  $TARBALL" | sha256sum -c --quiet
[ -d "$SRC" ] || tar -xzf "$TARBALL" -C "$BUILD"

if [ -n "${LLAMA_ARCH:-}" ]; then
	cpu=(-DGGML_NATIVE=OFF -DGGML_CPU_ARM_ARCH=$LLAMA_ARCH)
else
	cpu=(-DGGML_NATIVE=ON)
fi
common=(-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF "${cpu[@]}" -DGGML_OPENMP=OFF
	-DGGML_BACKEND_DL=OFF -DGGML_CPU_KLEIDIAI=OFF -DGGML_LLAMAFILE=ON -DGGML_CPU_REPACK=ON
	-DGGML_RPC=OFF -DGGML_CCACHE=OFF -DLLAMA_BUILD_COMMON=OFF -DLLAMA_BUILD_TOOLS=OFF
	-DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_APP=OFF -DLLAMA_BUILD_UI=OFF
	-DLLAMA_BUILD_TESTS=OFF -DLLAMA_OPENSSL=OFF)

# GGML_SCHED_MAX_SPLIT_INPUTS: ggml's scheduler mallocs its context for the worst case, one split per
# graph node with up to 30 inputs each: ~180 MiB for Llama 3.2 1B. On Linux those pages are never
# touched and cost nothing; a TA has no demand paging, so it would be real memory next to the
# weights. With one backend (CPU) the graph never splits, so 1 is plenty, and running out would be
# a GGML_ASSERT, not a wrong result. A build setting ggml provides, not a source change.
build() {
	local out=$1
	shift
	rm -rf "$out"
	mkdir -p "$out"
	cmake -S "$SRC" -B "$out" "$@" "${common[@]}" >"$out/configure.log"
	cmake --build "$out" --target llama ggml ggml-base ggml-cpu "$JOBS" 2>&1 | tail -n 20
}
build "$BUILD/llama-tee" -DCMAKE_TOOLCHAIN_FILE="$(cd ../../tee-libc && pwd)/toolchain.cmake" \
	-DTEE_EXTRA_FLAGS=-DGGML_SCHED_MAX_SPLIT_INPUTS=1
build "$BUILD/llama-linux"
find "$BUILD/llama-tee" "$BUILD/llama-linux" -name '*.a' -exec ls -la {} +
