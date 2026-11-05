#!/usr/bin/env bash
set -euo pipefail

# One-time registration of Render's own pinned bx/bimg/bgfx gitlinks.
# Copies the exact vendor revisions recorded in an existing GUI checkout;
# subsequent Render builds/clones do not depend on that checkout.
if (( $# != 1 )); then
    printf 'Usage: %s /path/to/AOWIS-SERVER-GUI\n' "${0##*/}" >&2
    exit 2
fi
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
gui="$(cd -- "$1" && pwd -P)"

if [[ "$(git -C "$root" rev-parse --show-toplevel 2>/dev/null)" != "$root" ]]; then
    printf 'Error: run inside a real AOWIS-GUI-Render Git checkout.\n' >&2
    exit 1
fi
if [[ "$(git -C "$gui" rev-parse --show-toplevel 2>/dev/null)" != "$gui" ]]; then
    printf 'Error: expected a GUI Git checkout: %s\n' "$gui" >&2
    exit 1
fi

# Check all vendor gitlinks before modifying anything.
declare -A revisions=()
for dep in bx bimg bgfx; do
    record="$(git -C "$gui" ls-files --stage -- "external/$dep")"
    if [[ ! "$record" =~ ^160000[[:space:]]+([0-9a-f]{40})[[:space:]]+0[[:space:]] ]]; then
        printf 'Error: GUI has no pinned gitlink for external/%s\n' "$dep" >&2
        exit 1
    fi
    revisions["$dep"]="${BASH_REMATCH[1]}"
done

url_base="${AOWIS_RENDER_VENDOR_URL_BASE:-https://github.com/bkaradzic}"
url_base="${url_base%/}"
for dep in bx bimg bgfx; do
    path="external/$dep"
    url="$url_base/$dep.git"
    # Never silently replace an existing submodule. If it is registered,
    # the caller must explicitly reconcile its pin with the GUI checkout.
    if git -C "$root" ls-files --stage -- "$path" | grep -q '^160000 '; then
        printf 'Already registered: %s (left unchanged)\n' "$path"
        continue
    fi
    if [[ -e "$root/$path" ]]; then
        printf 'Error: %s exists but is not a registered gitlink.\n' "$root/$path" >&2
        exit 1
    fi
    # file:// is allowed only for the explicitly supplied local test/mirror URL.
    if [[ "$url_base" == file://* ]]; then
        git -C "$root" -c protocol.file.allow=always submodule add --depth 1 "$url" "$path"
    else
        git -C "$root" submodule add --depth 1 "$url" "$path"
    fi
    # Shallow upstream snapshots might not contain the GUI's pinned revision.
    if ! git -C "$root/$path" cat-file -e "${revisions[$dep]}^{commit}" 2>/dev/null; then
        git -C "$root/$path" fetch --depth 1 origin "${revisions[$dep]}"
    fi
    git -C "$root/$path" checkout --detach "${revisions[$dep]}"
    git -C "$root" add .gitmodules "$path"
    printf 'Pinned %-4s %s\n' "$dep" "${revisions[$dep]}"
done

printf '\nVendor gitlinks staged. Review and commit in Render:\n'
printf '  git diff --cached --submodule=short\n'
printf '  git commit -m "Pin Render bgfx, bx and bimg vendor submodules"\n'
