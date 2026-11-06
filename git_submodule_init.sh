#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ ! -f "$root/.gitmodules" ]]; then
    printf 'Render vendor submodules have not been registered.\n' >&2
    printf 'Run: ./tools/bootstrap_vendor_submodules.sh /path/to/AOWIS-SERVER-GUI\n' >&2
    exit 1
fi
deps=(bx bimg bgfx GeographicLib AOWIS-SERVER-MODEL)
paths=()
for dep in "${deps[@]}"; do
    if ! git -C "$root" ls-files --stage -- "external/$dep" | grep -q '^160000 '; then
        printf 'Missing Git submodule gitlink: external/%s\n' "$dep" >&2
        printf 'Register it once with: ./tools/bootstrap_vendor_submodules.sh /path/to/AOWIS-SERVER-GUI\n' >&2
        exit 1
    fi
    paths+=("external/$dep")
done
git -C "$root" submodule sync --recursive -- "${paths[@]}"
git -C "$root" submodule update --init --recursive -- "${paths[@]}"
