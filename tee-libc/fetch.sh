#!/bin/bash
# Downloads what tee-libc is built from, checks each file against its pinned SHA-256, and unpacks
# Alpine's libstdc++ (static library and C++ headers, built for musl) into the sysroot:
#   - musl 1.2.5, from musl.libc.org
#   - libstdc++-dev 14.2.0-r6 for aarch64, from Alpine v3.22 (its hash was checked against the
#     signed APKINDEX it is listed in when it was pinned)
# Output: $BUILD/dl and $BUILD/tee-libc/sysroot. Usage: ./fetch.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
BUILD=${OPTEE_LLM_BUILD:-$here/../build}
DL=$BUILD/dl
SYSROOT=$BUILD/tee-libc/sysroot

fetch() {
	local url=$1 sha=$2 file
	file=$DL/$(basename "$url")
	if [ ! -f "$file" ]; then
		curl -fL --retry 3 -o "$file.part" "$url"
		mv "$file.part" "$file"
	fi
	echo "$sha  $file" | sha256sum -c --quiet
}

mkdir -p "$DL"
fetch https://musl.libc.org/releases/musl-1.2.5.tar.gz \
	a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
fetch https://dl-cdn.alpinelinux.org/alpine/v3.22/main/aarch64/libstdc++-dev-14.2.0-r6.apk \
	a606b1b4a2cebce71443c3c27876cec4b6d51a5216e362d8674760d6b1586b69

# An .apk is gzip members (signature, control, data) that tar reads as one stream.
rm -rf "$SYSROOT"
mkdir -p "$SYSROOT"
tar -xzf "$DL/libstdc++-dev-14.2.0-r6.apk" -C "$SYSROOT" --warning=no-unknown-keyword usr
ls "$SYSROOT/usr/lib/libstdc++.a" "$SYSROOT/usr/include/c++/14.2.0" >/dev/null
echo "sysroot at $SYSROOT"
