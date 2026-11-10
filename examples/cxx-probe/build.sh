#!/bin/bash
# Builds the C++ probe: lib/ (C++ against tee-libc), the TA, and the host client.
# Usage: TA_DEV_KIT_DIR=<tee-libc dev kit> ./build.sh   (default: the one make-devkit.sh made)
# Then install ta/cf398d28-8a9a-4766-8ebd-5f317ca7fc19.ta where tee-supplicant looks for TAs
# (usually /lib/optee_armtz) and run host/cxxprobe.
set -euo pipefail
cd "$(dirname "$0")"
DEVKIT=${TA_DEV_KIT_DIR:-${OPTEE_LLM_BUILD:-$(cd ../.. && pwd)/build}/tee-libc/devkit}

make -C lib clean >/dev/null && make -C lib
make -C ta CROSS_COMPILE= TA_DEV_KIT_DIR="$DEVKIT" clean >/dev/null
make -C ta CROSS_COMPILE= TA_DEV_KIT_DIR="$DEVKIT" -j"$(nproc)" "$@"
make -C host
