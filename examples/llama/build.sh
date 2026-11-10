#!/bin/bash
# Builds the llama TA (after build-llama.sh), the host client and the Linux reference program.
# Usage: TA_DEV_KIT_DIR=<tee-libc dev kit> ./build.sh   (default: the one make-devkit.sh made)
# Then install ta/f6af916a-a2b6-4823-87d8-19d3d30d0f90.ta where tee-supplicant looks for TAs
# (usually /lib/optee_armtz) and run host/llama_cli.
set -euo pipefail
cd "$(dirname "$0")"
DEVKIT=${TA_DEV_KIT_DIR:-${OPTEE_LLM_BUILD:-$(cd ../.. && pwd)/build}/tee-libc/devkit}

make -C engine clean >/dev/null && make -C engine
make -C ta CROSS_COMPILE= TA_DEV_KIT_DIR="$DEVKIT" clean >/dev/null
make -C ta CROSS_COMPILE= TA_DEV_KIT_DIR="$DEVKIT" -j"$(nproc)" "$@"
make -C host
