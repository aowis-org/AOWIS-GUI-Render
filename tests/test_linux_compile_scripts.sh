#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/bin"
cat > "$tmp/bin/cmake" <<'MOCK'
#!/usr/bin/env bash
printf 'cmake %s\n' "$*" >> "$AOWIS_TEST_LOG"
if [[ "$*" == *' --build '* || "$1" == '--build' ]]; then
    [[ "${AOWIS_FAIL_PHASE:-}" != 'build' ]]
else
    [[ "${AOWIS_FAIL_PHASE:-}" != 'configure' ]]
fi
MOCK
cat > "$tmp/bin/ctest" <<'MOCK'
#!/usr/bin/env bash
printf 'ctest %s\n' "$*" >> "$AOWIS_TEST_LOG"
[[ "${AOWIS_FAIL_PHASE:-}" != 'ctest' ]]
MOCK
chmod +x "$tmp/bin/cmake" "$tmp/bin/ctest"
export PATH="$tmp/bin:$PATH" AOWIS_TEST_LOG="$tmp/log" AOWIS_LINUX_LINKER=system
for phase in configure build; do
    : > "$AOWIS_TEST_LOG"
    if AOWIS_FAIL_PHASE="$phase" "$root/compile_linux_tests.sh" > "$tmp/output" 2>&1; then
        echo "FAIL: $phase failure went unnoticed" >&2; exit 1
    fi
    if grep -q '^ctest ' "$AOWIS_TEST_LOG"; then
        echo "FAIL: CTest ran after $phase failure" >&2; exit 1
    fi
    grep -q 'No tests were executed' "$tmp/output"
    echo "PASS: $phase failure blocks CTest"
done
: > "$AOWIS_TEST_LOG"
"$root/compile_linux_tests.sh" > "$tmp/output" 2>&1
[[ "$(grep -c '^ctest ' "$AOWIS_TEST_LOG")" == 1 ]]
grep -q 'PASS: Render targets and test executables built' "$tmp/output"
echo 'PASS: successful build invokes exactly one test run'
: > "$AOWIS_TEST_LOG"
if AOWIS_FAIL_PHASE=ctest "$root/compile_linux_tests.sh" >/dev/null 2>&1; then
    echo 'FAIL: CTest failure did not propagate' >&2; exit 1
fi
echo 'PASS: test failure propagates'
: > "$AOWIS_TEST_LOG"
"$root/compile_linux.sh" >/dev/null 2>&1
if grep -q '^ctest ' "$AOWIS_TEST_LOG"; then
    echo 'FAIL: production compile ran CTest' >&2; exit 1
fi
grep -q 'BUILD_TESTING=OFF' "$AOWIS_TEST_LOG"
echo 'PASS: production build does not run tests'
if AOWIS_BUILD_JOBS=broken "$root/compile_linux_tests.sh" > "$tmp/output" 2>&1; then
    echo 'FAIL: invalid jobs accepted' >&2; exit 1
fi
if AOWIS_LINUX_LINKER=broken "$root/compile_linux_tests.sh" > "$tmp/output" 2>&1; then
    echo 'FAIL: invalid linker accepted' >&2; exit 1
fi
echo 'PASS: invalid settings rejected before build'
