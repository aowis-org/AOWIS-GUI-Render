#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/tools/linux_compile_common.sh"

build_type=Release
case "${1:-}" in
    '') ;;
    --debug) build_type=Debug ;;
    --release) ;;
    --help|-h)
        echo 'Usage: ./compile_linux.sh [--debug | --release]'
        echo 'Build standalone Render with Ninja, without launching an application or running tests.'
        exit 0 ;;
    *) echo "ERROR: Unknown option: $1" >&2; exit 2 ;;
esac
if (( $# > 1 )); then echo 'ERROR: Too many arguments' >&2; exit 2; fi
build_dir="${AOWIS_RENDER_ROOT}/build-linux"
cmake -S "${AOWIS_RENDER_ROOT}" -B "${build_dir}" -G Ninja \
    "-DCMAKE_BUILD_TYPE=${build_type}" \
    "-DCMAKE_EXE_LINKER_FLAGS=${AOWIS_RENDER_LINKER_FLAGS}" \
    -DBUILD_TESTING=OFF -DAOWIS_RENDER_BUILD_GEOMETRY_TESTS=OFF
cmake --build "${build_dir}" --parallel "${AOWIS_BUILD_JOBS}"
echo 'PASS: standalone Render compiled and linked successfully.'
