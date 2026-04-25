# GitHub Release

Create a signed GitHub release with CI-built package assets.

## Arguments

$ARGUMENTS — version bump type: `major`, `minor`, or `patch` (default: `patch` if omitted). If a specific version like `1.2.3` is given, use that instead.

## Steps

### 1. Determine new version

- Read current version from `CMakeLists.txt` (the `VERSION` field in the `project()` call) and `conanfile.py`.
- Compute the new version based on the bump type or use the explicit version provided.
- Confirm the new version with the user before proceeding.

### 2. Bump version

- Update `CMakeLists.txt` and `conanfile.py` with the new version.
- Commit: `Bump version to X.Y.Z`
- Push to `master`.

### 3. Wait for CI

- Watch both `Self-Hosted CI` and `Clang Format Check` workflows until completion.
- If any job fails, report the failure and stop. Do NOT tag or release.
- All jobs must pass: Fast Tests, RPM builds (EL8, EL9, EL10), DEB build, and all publish jobs.

### 4. Create signed tag

- Create a GPG-signed annotated tag: `git tag -s vX.Y.Z -m "OpenCATTUS X.Y.Z"`
- Push the tag.

### 5. Download CI artifacts

- Download all artifacts from the successful CI run (RPMs and DEBs).

### 6. Collect release notes

- Ask the user for release notes. Guide them:
  - Only features and bugfixes. No infrastructure, CI, or tooling changes.
  - Keep it user-facing: what changed from a user's perspective.
- Format as a `## What's Changed` section with bullet points.

### 7. Create GitHub release

- Use `gh release create` with:
  - `--title "OpenCATTUS X.Y.Z"`
  - `--latest` flag
  - All downloaded package artifacts attached
  - Release notes from step 6
- Verify the release is not a draft and is marked as Latest.
- Clean up temporary artifact files.

### 8. Verify

- Run `gh release list` and confirm the new release shows as `Latest`.
- Report the release URL to the user.
