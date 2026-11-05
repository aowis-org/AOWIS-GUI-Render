#!/usr/bin/env bash
set -euo pipefail

# AOWIS-GUI-Render patch format is independent of the checkout's directory name.
# ZIP entries live below the fixed namespace AOWIS-GUI-Render/.
main() {
    local script_dir project_root patch_name patch_path
    if (( $# != 1 )); then
        printf 'Usage: %s /path/to/aowis-gui-render-<description>.zip\n' "${0##*/}" >&2
        return 2
    fi
    patch_name="${1##*/}"
    case "$patch_name" in
        aowis-gui-render-?*.zip) ;;
        *) printf 'Expected aowis-gui-render-<description>.zip: %s\n' "$patch_name" >&2; return 2 ;;
    esac
    command -v python3 >/dev/null || { printf 'Missing python3\n' >&2; return 1; }
    command -v git >/dev/null || { printf 'Missing git\n' >&2; return 1; }
    script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
    project_root="$(cd -- "$script_dir/.." && pwd -P)"
    patch_path="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$1")"
    [[ -f "$patch_path" ]] || { printf 'Patch not found: %s\n' "$patch_path" >&2; return 1; }

    # Copy updated files using atomic replacements, so a patch can update this
    # running script without truncating the file that Bash is executing.
    python3 - "$project_root" "$patch_path" <<'PY'
import os
import pathlib
import re
import stat
import sys
import tempfile
import zipfile

root = pathlib.Path(sys.argv[1])
archive = pathlib.Path(sys.argv[2])
namespace = 'AOWIS-GUI-Render'
manifest_name = '.aowis-patch-delete.txt'


def fail(message):
    raise ValueError(message)


def valid_relative(name):
    if (not name or name.startswith('/') or '\\' in name or '\x00' in name
            or '\n' in name or '\r' in name or ':' in name):
        fail(f'Invalid patch path: {name!r}')
    parts = name.split('/')
    if any(p in ('', '.', '..', '.git') for p in parts):
        fail(f'Invalid patch path: {name!r}')
    return parts


def no_symlink_parts(parts):
    target = root
    for part in parts:
        target = target / part
        if target.is_symlink():
            fail(f'Refusing symlink in destination: {target}')
    return target


try:
    with zipfile.ZipFile(archive) as z:
        infos = z.infolist()
        if not infos:
            fail('Empty patch archive')
        entries = {}
        files = {}
        directories = set()
        for info in infos:
            name = info.filename
            if name == namespace + '/':
                continue
            if not name.startswith(namespace + '/'):
                fail(f'Unexpected archive namespace: {name!r}')
            rel = name[len(namespace) + 1:]
            is_dir = info.is_dir()
            if is_dir:
                rel = rel[:-1]
            parts = valid_relative(rel)
            mode = info.external_attr >> 16
            file_type = stat.S_IFMT(mode)
            if file_type not in (0, stat.S_IFDIR if is_dir else stat.S_IFREG):
                fail(f'Unsupported file type in patch: {name!r}')
            if rel in entries:
                fail(f'Duplicate patch entry: {name!r}')
            entries[rel] = info
            if is_dir:
                directories.add(rel)
            else:
                files[rel] = info
        if not files:
            fail('Patch contains no files or deletion manifest')
        for rel in entries:
            parts = valid_relative(rel)
            for index in range(1, len(parts)):
                if '/'.join(parts[:index]) in files:
                    fail(f'File/directory collision in archive: {rel!r}')
            target = no_symlink_parts(parts)
            if rel in files:
                if target.is_dir():
                    fail(f'File would replace a directory: {rel!r}')
            elif target.exists() and not target.is_dir():
                fail(f'Directory would replace a file: {rel!r}')
            for index in range(1, len(parts)):
                ancestor = root.joinpath(*parts[:index])
                if ancestor.exists() and not ancestor.is_dir():
                    fail(f'File blocks destination directory: {ancestor}')

        deletes = []
        if manifest_name in files:
            raw = z.read(files[manifest_name])
            try:
                lines = raw.decode('utf-8').splitlines()
            except UnicodeDecodeError as exc:
                fail(f'Invalid deletion manifest encoding: {exc}')
            for rel in lines:
                if not rel or rel.startswith('#'):
                    continue
                parts = valid_relative(rel)
                if rel in (manifest_name,) or rel in entries:
                    fail(f'Deletion conflicts with patched file: {rel!r}')
                target = no_symlink_parts(parts)
                if target.is_dir():
                    fail(f'Deletion must target a file, not directory: {rel!r}')
                deletes.append(target)
        # Test every CRC before touching the checkout.
        corrupt = z.testzip()
        if corrupt:
            fail(f'Corrupt patch entry: {corrupt}')

        # Write patch data to temporary sibling files, then atomically rename.
        # No permission from the local checkout directory name is required.
        touched_dirs = set()
        for rel, info in files.items():
            if rel == manifest_name:
                continue
            target = root.joinpath(*rel.split('/'))
            target.parent.mkdir(parents=True, exist_ok=True)
            fd, temp_name = tempfile.mkstemp(prefix='.aowis-render-patch-', dir=target.parent)
            try:
                with os.fdopen(fd, 'wb') as output, z.open(info) as source:
                    while True:
                        chunk = source.read(1024 * 1024)
                        if not chunk:
                            break
                        output.write(chunk)
                old_mode = stat.S_IMODE(target.stat().st_mode) if target.is_file() else 0
                zip_mode = stat.S_IMODE(info.external_attr >> 16)
                executable = bool((old_mode | zip_mode) & 0o111) or rel.endswith('.sh')
                os.chmod(temp_name, 0o755 if executable else 0o644)
                os.replace(temp_name, target)
                os.utime(target, None)
                touched_dirs.add(target.parent)
            finally:
                if os.path.exists(temp_name):
                    os.unlink(temp_name)

        for target in deletes:
            if target.exists():
                target.unlink()
            directory = target.parent
            while directory != root:
                try:
                    directory.rmdir()  # only empty parents
                except OSError:
                    break
                directory = directory.parent
        for directory in sorted(touched_dirs, key=lambda p: len(p.parts), reverse=True):
            if directory.is_dir():
                os.utime(directory, None)
        print(f'Applied {archive} ({len(files) - (manifest_name in files)} file(s), '
              f'{len(deletes)} deletion(s)).')
except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as exc:
    print(f'Patch rejected or failed: {exc}', file=sys.stderr)
    sys.exit(1)
PY
    printf 'Refreshing render.zip...\n'
    bash "$project_root/tools/zip_render_project.sh"
}

main "$@"
