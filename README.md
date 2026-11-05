# AOWIS-GUI-Render

Shared rendering infrastructure for AOWIS desktop and mobile GUIs.

The repository currently contains its initial tooling. Extraction of the
reusable renderer will follow separately; no rendering library is present yet.

## Development tooling

- `./tools/apply_patch.sh /path/to/aowis-gui-render-<description>.zip`
  validates and applies a source-only patch, then refreshes `render.zip`.
- `./tools/zip_render_project.sh` exports a source-only snapshot named
  `render.zip` beside the checkout.
- `./tools/zip_render_project.sh /path/to/output.zip` writes a snapshot to a
  different path.
- `./tests/test_patch_tooling.sh` runs portability and safety tests.

**Local directory names never determine the patch format or ZIP structure.**
Every patch and snapshot uses the logical archive prefix `AOWIS-GUI-Render/`,
regardless of the repository's physical path or checkout directory name.

Patch archives must be named `aowis-gui-render-<description>.zip` and include
only changed or newly added files. To remove obsolete files, include
`AOWIS-GUI-Render/.aowis-patch-delete.txt` in the patch with one repository-
relative file path per line. Deletion manifests are validated, acted upon and
not left in the working tree. Deletion-only patches are supported.

Snapshots include tracked files and non-ignored untracked source files. They
exclude `.git`, nested repository/submodule contents, common build outputs and
signing keys or other common secrets. The scripts require `bash`, `git` and
`python3` (the bootstrap step also uses `unzip`).

### First-time installation into an empty checkout

Because a new repository has no patch application script yet, bootstrap it
once from the tooling patch. From the checkout root:

```bash
PATCH=~/Downloads/aowis-gui-render-initial-tooling.zip
mkdir -p tools
unzip -p "$PATCH" AOWIS-GUI-Render/tools/apply_patch.sh > tools/apply_patch.sh
chmod +x tools/apply_patch.sh
./tools/apply_patch.sh "$PATCH"
```

Future patches need only the normal `./tools/apply_patch.sh` command. Neither
operation requires renaming or moving the local checkout.
