#!/usr/bin/env bash
set -euo pipefail

# Git-aware, source-only snapshot. The ZIP namespace is fixed; the actual
# checkout directory may have ANY name or location.
main() {
    local script_dir project_root default_path output_path
    if (( $# > 1 )); then
        printf 'Usage: %s [output.zip]\n' "${0##*/}" >&2
        return 2
    fi
    command -v python3 >/dev/null || { printf 'Missing python3\n' >&2; return 1; }
    command -v git >/dev/null || { printf 'Missing git\n' >&2; return 1; }
    script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
    project_root="$(cd -- "$script_dir/.." && pwd -P)"
    default_path="$(dirname -- "$project_root")/render.zip"
    output_path="${1:-$default_path}"

    python3 - "$project_root" "$output_path" <<'PY'
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile

root = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2]).expanduser().absolute()
namespace = 'AOWIS-GUI-Render'


def run_git(*args):
    return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)


try:
    if pathlib.Path(os.path.realpath(os.fsdecode(run_git('rev-parse', '--show-toplevel').rstrip(b'\n')))) != root:
        raise ValueError(f'Not at the root of a Git checkout: {root}')
    if output.suffix.lower() != '.zip' or not output.parent.is_dir():
        raise ValueError('Output must be a .zip file in an existing directory')
    listing = run_git('ls-files', '-z', '--cached', '--others', '--exclude-standard')
    names = sorted(set(os.fsdecode(item) for item in listing.split(b'\0') if item))
    files = []
    for relative in names:
        parts = pathlib.PurePosixPath(relative).parts
        if not parts or any(part in ('.git', '..') for part in parts):
            continue
        # Exclude submodules, untracked embedded Git repos, symlinks and
        # any paths passing through a symlink.
        path = root
        safe = True
        for part in parts:
            path = path / part
            if path.is_symlink():
                safe = False
                break
        if not safe or not path.is_file() or path.resolve() == output.resolve():
            continue
        if '\n' in relative or '\r' in relative:
            raise ValueError(f'Newline in filename: {relative!r}')
        if any(part.startswith(('build-', 'cmake-build-')) or part in {
                'build', 'CMakeFiles', 'Testing', '_deps', '.git', '.gradle',
                '.idea', '.vscode', '.cache', '__pycache__', 'node_modules',
                '.aowis-render-patch', '.aowis-render-zip'} for part in parts):
            continue
        filename = path.name.lower()
        if (filename in {'.env', '.ds_store'} or filename.startswith('.env.')
                or any(filename.endswith(ext) for ext in (
                    '.pem', '.key', '.p12', '.pfx', '.keystore', '.jks',
                    '.apk', '.aab', '.ipa', '.o', '.obj', '.a', '.so',
                    '.dylib', '.dll', '.wasm', '.pyc', '.log', '.tmp',
                    '.swp', '.swo'))):
            continue
        files.append((relative, path))
    if not files:
        raise ValueError('No source files found for snapshot')

    fd, temp_path = tempfile.mkstemp(prefix='.aowis-render-zip-', suffix='.zip', dir=output.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(temp_path, 'w', compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6, allowZip64=True) as z:
            for rel, source in files:
                z.write(source, arcname=f'{namespace}/{rel}')
        os.replace(temp_path, output)
    finally:
        if os.path.exists(temp_path):
            os.unlink(temp_path)
    print(f'Created {output} ({len(files)} source files)')
except (OSError, ValueError, subprocess.CalledProcessError) as exc:
    print(f'Unable to create render snapshot: {exc}', file=sys.stderr)
    sys.exit(1)
PY
}

main "$@"
