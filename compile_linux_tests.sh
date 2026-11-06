#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/tools/linux_compile_common.sh"

build_type=Release
case "${1:-}" in
    '') ;;
    --debug) build_type=Debug ;;
    --release) ;;
    --help|-h)
        echo 'Usage: ./compile_linux_tests.sh [--debug | --release]'
        echo 'Configure and build all Render targets and test executables, then run CTest.'
        echo 'CTest is NEVER invoked if configuration, compilation or linking fails.'
        exit 0 ;;
    *) echo "ERROR: Unknown option: $1" >&2; exit 2 ;;
esac
if (( $# > 1 )); then echo 'ERROR: Too many arguments' >&2; exit 2; fi
build_dir="${AOWIS_RENDER_ROOT}/build-linux-tests"
if ! cmake -S "${AOWIS_RENDER_ROOT}" -B "${build_dir}" -G Ninja \
    "-DCMAKE_BUILD_TYPE=${build_type}" \
    "-DCMAKE_EXE_LINKER_FLAGS=${AOWIS_RENDER_LINKER_FLAGS}" \
    -DBUILD_TESTING=ON -DAOWIS_RENDER_BUILD_GEOMETRY_TESTS=ON; then
    echo 'ERROR: Render configuration failed. No tests were executed.' >&2
    exit 1
fi
if ! cmake --build "${build_dir}" --parallel "${AOWIS_BUILD_JOBS}"; then
    echo 'ERROR: Render compilation or linking failed. No tests were executed.' >&2
    exit 1
fi
# CTest may otherwise execute stale binaries from an earlier successful build.
echo 'PASS: Render targets and test executables built; starting CTest.'
ctest --test-dir "${build_dir}" --output-on-failure
