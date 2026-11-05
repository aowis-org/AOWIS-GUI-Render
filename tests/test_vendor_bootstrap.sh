#!/usr/bin/env bash
set -euo pipefail

script_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT
export GIT_AUTHOR_NAME=Test GIT_AUTHOR_EMAIL=test@example.com
export GIT_COMMITTER_NAME=Test GIT_COMMITTER_EMAIL=test@example.com
mkdir -p "$tmp/upstream" "$tmp/gui" "$tmp/render"

git -C "$tmp/gui" init -q
git -C "$tmp/render" init -q
for dep in bx bimg bgfx; do
    mkdir -p "$tmp/work/$dep"
    git -C "$tmp/work/$dep" init -q
    printf 'pinned-%s\n' "$dep" > "$tmp/work/$dep/version"
    git -C "$tmp/work/$dep" add version
    git -C "$tmp/work/$dep" commit -qm pinned
    git -C "$tmp/work/$dep" branch -M main
    git init --bare -q "$tmp/upstream/$dep.git"
    git -C "$tmp/work/$dep" remote add origin "$tmp/upstream/$dep.git"
    git -C "$tmp/work/$dep" push -q -u origin main
    git -C "$tmp/upstream/$dep.git" symbolic-ref HEAD refs/heads/main
    git -C "$tmp/gui" -c protocol.file.allow=always submodule add -q \
        "file://$tmp/upstream/$dep.git" "external/$dep"
    pin="$(git -C "$tmp/gui/external/$dep" rev-parse HEAD)"
    printf 'newer-%s\n' "$dep" > "$tmp/work/$dep/version"
    git -C "$tmp/work/$dep" commit -qam newer
    git -C "$tmp/work/$dep" push -q
    git -C "$tmp/gui" add .gitmodules "external/$dep"
    printf '%s %s\n' "$dep" "$pin" >> "$tmp/pins"
done
git -C "$tmp/gui" commit -qm 'Pinned vendors'
mkdir -p "$tmp/render/tools"
cp "$script_root/tools/bootstrap_vendor_submodules.sh" "$tmp/render/tools/"
cp "$script_root/git_submodule_init.sh" "$tmp/render/"
git -C "$tmp/render" add tools/ git_submodule_init.sh
git -C "$tmp/render" commit -qm scripts

AOWIS_RENDER_VENDOR_URL_BASE="file://$tmp/upstream" \
    "$tmp/render/tools/bootstrap_vendor_submodules.sh" "$tmp/gui" > "$tmp/bootstrap.log"
while read -r dep pin; do
    actual="$(git -C "$tmp/render/external/$dep" rev-parse HEAD)"
    [[ "$actual" == "$pin" ]] || { printf 'Wrong pin: %s\n' "$dep" >&2; exit 1; }
    git -C "$tmp/render" ls-files --stage "external/$dep" | grep -q "^160000 $pin "
done < "$tmp/pins"

# The bootstrap may be re-run without ever updating an existing gitlink.
AOWIS_RENDER_VENDOR_URL_BASE="file://$tmp/upstream" \
    "$tmp/render/tools/bootstrap_vendor_submodules.sh" "$tmp/gui" > "$tmp/again.log"
while read -r dep pin; do
    [[ "$(git -C "$tmp/render/external/$dep" rev-parse HEAD)" == "$pin" ]]
done < "$tmp/pins"

# Once committed, a fresh Render checkout must restore its own dependencies
# without reference to the GUI checkout.
git -C "$tmp/render" commit -qm 'Pin Render vendors'
git -c protocol.file.allow=always clone -q "$tmp/render" "$tmp/render-clone"
(cd "$tmp/render-clone" && git -c protocol.file.allow=always submodule update --init --recursive -q)
while read -r dep pin; do
    [[ "$(git -C "$tmp/render-clone/external/$dep" rev-parse HEAD)" == "$pin" ]]
done < "$tmp/pins"

printf 'PASS: standalone Render vendors pinned to GUI and cloned independently\n'
