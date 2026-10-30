#!/bin/sh

set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
IOS_DIR="$(dirname "$SCRIPT_DIR")"
REPO_DIR="$(git -C "$IOS_DIR" rev-parse --show-toplevel)"
HOST_TOOLS_DIR="${IOS_DIR}/.xmake/xcode-cloud-host"

if ! command -v brew >/dev/null 2>&1; then
    echo "Xcode Cloud requires Homebrew to install the iOS build tools." >&2
    exit 69
fi

echo "Installing Xcode Cloud native build tools"
brew install xmake cmake python@3.13
BREW_PREFIX="$(brew --prefix)"
PYTHON_PREFIX="$(brew --prefix python@3.13)"

# Match GitHub Actions' host Python dependencies without modifying Homebrew's
# externally managed Python. The Xcode build phase selects this environment
# explicitly because exports here do not persist into later Cloud steps.
"${PYTHON_PREFIX}/bin/python3.13" -m venv "$HOST_TOOLS_DIR"
"${HOST_TOOLS_DIR}/bin/python3" -m pip install --upgrade pip setuptools wheel
for tool in xmake xrepo cmake ctest cpack; do
    ln -sf "${BREW_PREFIX}/bin/${tool}" "${HOST_TOOLS_DIR}/bin/${tool}"
done

"${HOST_TOOLS_DIR}/bin/xmake" --version > "${HOST_TOOLS_DIR}/xmake-version.txt"
"${HOST_TOOLS_DIR}/bin/python3" -B "${IOS_DIR}/scripts/verify_native_build.py" \
    --xmake-version "${HOST_TOOLS_DIR}/xmake-version.txt"

echo "Initializing MiniRTC submodules"
git -C "$REPO_DIR" submodule sync --recursive
git -C "$REPO_DIR" submodule update --init --recursive

echo "Xcode Cloud iOS build tools are ready"
