#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
git submodule update --init --recursive
actual=$(git -C deps/submodules/minirtc rev-parse HEAD)
[ "$actual" = b9ee1fa7fe038fdf771f5dec1b1d561d78273f15 ] || { echo 'Unexpected MiniRTC revision' >&2; exit 1; }
patch_file="$(pwd)/deps/patches/minirtc-credential-logs.patch"
if git -C deps/submodules/minirtc apply --reverse --check "$patch_file" 2>/dev/null; then
    exit 0
fi
git -C deps/submodules/minirtc apply --check "$patch_file"
git -C deps/submodules/minirtc apply "$patch_file"
