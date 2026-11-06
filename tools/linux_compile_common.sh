#!/usr/bin/env bash
# Shared Linux toolchain settings; sourced by both build entrypoints.
set -euo pipefail
AOWIS_RENDER_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
: "${AOWIS_LINUX_LINKER:=lld}"
: "${AOWIS_BUILD_JOBS:=8}"
case "${AOWIS_LINUX_LINKER}" in
    lld)
        if ! command -v ld.lld >/dev/null 2>&1; then
            echo 'ERROR: ld.lld is required by default; install lld or set AOWIS_LINUX_LINKER=system.' >&2
            exit 1
        fi
        AOWIS_RENDER_LINKER_FLAGS='-fuse-ld=lld'
        ;;
    system)
        AOWIS_RENDER_LINKER_FLAGS=''
        ;;
    *)
        printf 'ERROR: AOWIS_LINUX_LINKER must be lld or system (got %s)\n' "${AOWIS_LINUX_LINKER}" >&2
        exit 2
        ;;
esac
if [[ ! "${AOWIS_BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    printf 'ERROR: AOWIS_BUILD_JOBS must be a positive integer (got %s)\n' "${AOWIS_BUILD_JOBS}" >&2
    exit 2
fi
