#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <version>" >&2
    exit 2
fi

exec bash "$SCRIPT_DIR/package_deb.sh" amd64 x86_64 "$1" nvidia-cuda-toolkit
