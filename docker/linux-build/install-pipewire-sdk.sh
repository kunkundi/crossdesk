#!/usr/bin/env bash
set -euo pipefail

PIPEWIRE_VERSION="0.3.48"
PIPEWIRE_ARCHIVE_SHA256="0d218be86b3d2b548c06259c47ad8d110ee1c09f071e17c4393eeef5c880fa6f"
INSTALL_PREFIX="${1:-/opt/crossdesk-pipewire-sdk}"
IFS='.' read -r PIPEWIRE_VERSION_MAJOR PIPEWIRE_VERSION_MINOR \
    PIPEWIRE_VERSION_MICRO <<< "${PIPEWIRE_VERSION}"

if [[ "${INSTALL_PREFIX}" != /* ]]; then
    echo "Install prefix must be an absolute path: ${INSTALL_PREFIX}" >&2
    exit 2
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/crossdesk-pipewire-sdk.XXXXXX")"
trap 'rm -rf "${WORK_DIR}"' EXIT

ARCHIVE_PATH="${WORK_DIR}/pipewire-${PIPEWIRE_VERSION}.tar.gz"
SOURCE_DIR="${WORK_DIR}/pipewire-${PIPEWIRE_VERSION}"
ARCHIVE_URL="https://gitlab.freedesktop.org/pipewire/pipewire/-/archive/${PIPEWIRE_VERSION}/pipewire-${PIPEWIRE_VERSION}.tar.gz"

curl --proto '=https' --tlsv1.2 --fail --silent --show-error --location \
    --retry 3 "${ARCHIVE_URL}" -o "${ARCHIVE_PATH}"
echo "${PIPEWIRE_ARCHIVE_SHA256}  ${ARCHIVE_PATH}" | sha256sum --check -
tar -xzf "${ARCHIVE_PATH}" -C "${WORK_DIR}"

PIPEWIRE_INCLUDE_DIR="${INSTALL_PREFIX}/include/pipewire-0.3/pipewire"
SPA_INCLUDE_DIR="${INSTALL_PREFIX}/include/spa-0.2/spa"

while IFS= read -r -d '' header_path; do
    relative_path="${header_path#${SOURCE_DIR}/src/pipewire/}"
    install -D -m 0644 "${header_path}" \
        "${PIPEWIRE_INCLUDE_DIR}/${relative_path}"
done < <(find "${SOURCE_DIR}/src/pipewire" -type f -name '*.h' -print0)

while IFS= read -r -d '' header_path; do
    relative_path="${header_path#${SOURCE_DIR}/spa/include/spa/}"
    install -D -m 0644 "${header_path}" \
        "${SPA_INCLUDE_DIR}/${relative_path}"
done < <(find "${SOURCE_DIR}/spa/include/spa" -type f -name '*.h' -print0)

sed \
    -e "s/@PIPEWIRE_VERSION_MAJOR@/${PIPEWIRE_VERSION_MAJOR}/g" \
    -e "s/@PIPEWIRE_VERSION_MINOR@/${PIPEWIRE_VERSION_MINOR}/g" \
    -e "s/@PIPEWIRE_VERSION_MICRO@/${PIPEWIRE_VERSION_MICRO}/g" \
    -e 's/@PIPEWIRE_API_VERSION@/"0.3"/g' \
    "${SOURCE_DIR}/src/pipewire/version.h.in" \
    > "${PIPEWIRE_INCLUDE_DIR}/version.h"
chmod 0644 "${PIPEWIRE_INCLUDE_DIR}/version.h"

test -f "${PIPEWIRE_INCLUDE_DIR}/pipewire.h"
test -f "${PIPEWIRE_INCLUDE_DIR}/version.h"
test -f "${SPA_INCLUDE_DIR}/param/video/format-utils.h"

echo "Installed PipeWire ${PIPEWIRE_VERSION} header SDK under ${INSTALL_PREFIX}"
