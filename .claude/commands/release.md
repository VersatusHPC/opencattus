# OpenCATTUS Release (Jenkins pipeline)

Release OpenCATTUS: version tag, Jenkins-built packages, GitHub release, and
publication to the Versatus package repositories.

CI authority is Jenkins (`https://jenkins.local.versatushpc.com.br`, multibranch
job `opencattus`), driven by the `company-ci` shared library from
`VersatusHPC/ci-cd-platform` (local checkout: `~/Development/ci-cd-platform`).
The GitHub Actions workflows are retired manual fallbacks — do NOT wait on them.

## Arguments

$ARGUMENTS — version bump type: `major`, `minor`, or `patch` (default `patch`),
or an explicit version like `1.2.3`.

## Release model (read once)

- `master` builds publish signed packages to the **testing** repo channel
  automatically (`publish:` section in `ci/project.yaml`) and run install
  smoke tests.
- A **tag build** (`vX.Y.Z`) re-runs everything — container builds, Package
  CI, the full libvirt Integration lab (~hours) — and submits a promotion
  manifest to the Jenkins `/promote-rpm-release` job. Nothing reaches the
  **release** channel without that promotion being approved.
- The pipeline does NOT create the GitHub release under this model
  ("Upload to GitHub Release" only fires for `channel: release` projects);
  create it with `gh` (step 6).
- Never hand-transcribe package names/EVRs/manifests, and never call Pulp
  directly — the promote job is the only promotion path
  (ci-cd-platform `AGENTS.md`, "Package pipeline operating contract").

## Steps

### 1. Preflight

- Compute/confirm the new version with the user.
- These three must agree before tagging (Classify errors on tag/version
  mismatch): `CMakeLists.txt` `project(... VERSION X.Y.Z)`, `conanfile.py`
  `version`, and the tag `vX.Y.Z`.
- If not already bumped: update both files, commit `Bump version to X.Y.Z`,
  PR (or push) to `master`.
- Version contract (settled during 1.2.0): CPack packages the FULL
  `PROJECT_VERSION` (never strip a `.0` patch); the classify stage pins the
  tag to the CMake version; `ciReleaseManifest` accepts plain `v<VERSION>`
  tags by matching EVR prefix `VERSION-` (ci-cd-platform#23). Packaging
  projects like cde tag `v<VERSION>-<RELEASE>` instead.
- `ci/project.yaml` must have the `publish:` section and arch-level artifact
  globs (`out/rpm/<distro>/<arch>/*.rpm`, `out/deb/<distro>/amd64/*.deb`,
  Ubuntu entry `format: deb`). Present since 1.2.0; the reference layout is
  the cde canary (`~/Development/cde/.versatushpc/project.yaml`).
- `ci/build.sh` must recreate its per-distro output dirs before `cpack`
  (present since 1.2.0): agent workspaces persist, and stale packages from a
  previous version get stashed and published alongside the new ones.

### 2. Master must be green and published to testing

- `gh api repos/VersatusHPC/opencattus/commits/master/status` →
  `continuous-integration/jenkins/branch: success`.
- The master build's "Publish to repo" stage pushes to the testing channel
  and runs install smoke on every distro. Verify the exact version is
  listed, e.g.:
  `https://repos.versatushpc.com.br/versatushpc/rpm/<distro>/x86_64/testing/Packages/o/`
  (Pulp repository naming: `versatushpc-rpm-<distro>-x86_64-testing`,
  DEBs: `versatushpc-deb-ubuntu-testing`). Old versions may remain in the
  mutable testing channel; dnf/apt resolve the newest.

### 3. Create the signed tag

```bash
git tag -s vX.Y.Z -m "OpenCATTUS X.Y.Z"
git push origin vX.Y.Z
```

GPG key is configured in git; tags are annotated + signed.

### 4. Monitor the tag build (long: full integration lab)

- Status: `gh api repos/VersatusHPC/opencattus/commits/vX.Y.Z/status`
  (context `continuous-integration/jenkins/branch`).
- Poll in a background watcher; the lab stages alone take hours.
- Console/log access if something fails (root SSH):
  `ssh root@172.21.1.13 'tail -100 /var/lib/jenkins/jobs/opencattus/branches/<job>/builds/<n>/log'`
  where `<job>` is `master`, `PR-<n>`, or the tag name.
- On success the build submits the promotion manifest to
  `/promote-rpm-release` (log line: "Release promotion request ... submitted").

### 5. Collect release notes

- Draft from `git log v<prev>..vX.Y.Z --format='%s'`, then confirm with the
  user. User-facing features and fixes only — no CI, infra, or tooling items.
- Format: `## What's Changed` with bullet points.

### 6. GitHub release

- Artifacts: pull the tag build's archived packages from the controller:
  `ssh root@172.21.1.13 'ls /var/lib/jenkins/jobs/opencattus/branches/vX.Y.Z/builds/<n>/archive/out/'`
  (rpm/<distro>/<arch>/*.rpm and deb/<distro>/amd64/*.deb), scp them locally.
- `gh release create vX.Y.Z --title "OpenCATTUS X.Y.Z" --latest --notes ...`
  with all packages attached.
- Verify: `gh release list` shows the release as Latest, not draft.

### 7. Promote to the release repos

From `~/Development/ci-cd-platform` (auth: `release-automation` Jenkins
identity; token in `~/.jenkins-release-automation-token` or OpenBao
`kv/ci-cd/jenkins/release-automation`):

```bash
scripts/promote-rpm-release.py --manifest-from-job opencattus \
    --release-tag vX.Y.Z --dry-run --wait
```

Review the dry-run plan, present it to the user, and only rerun without
`--dry-run` when the maintainer explicitly confirms. Jenkins' input gate is
the default approval boundary — approvers: ferrao, daniel, dieguez
@versatushpc.com.br.

Use the script's `--manifest-from-job` flow, NOT the tag build's
auto-submitted request, for the actual promotion: DEB promotion is
digest-bound (`--expect-sha256`), DEB builds are not bit-reproducible, and
the tag build's manifest carries its own rebuilt DEB's sha — which can never
match the master-published content in testing. The master build's manifest
is the one whose digests equal the testing-channel content (v1.2.0: the
tag-submitted promote build failed exactly this way; `--manifest-from-job
opencattus` passed and promoted). The tag-submitted request still serves as
the audit record that the tagged tree passed full validation.

### 8. Verify and report

- Release channel repos updated (`versatushpc-rpm-<distro>-release`).
- GitHub release live with all assets.
- Report URLs for both.

## Troubleshooting

- GitHub status `error — "This commit cannot be built"` = pipeline exception
  (not a test failure). Read the controller log; grep past the CPS `ha:////`
  noise: `grep -v 'ha:////' log | grep -iE 'error|denied|fail'`.
- `PermissionError ... /root/.conan2/...` in container stages = poisoned
  shared cache on the agent (`/var/lib/jenkins-agent/.cache/opencattus`).
  Legit entries are owned by `jenkins-agent` or subuids (>=100000); files
  owned by real root (or plain host uids like 1000) are leftovers from the
  rootful-podman era. Fix: wipe the cache dir as root and recreate it owned
  by `jenkins-agent` (it repopulates on the next build; first build is slow).
- Agents: rome01/rome02/rome03 (x86_64) + power (ppc64le), root SSH at
  `<host>.local.versatushpc.com.br`; controller at 172.21.1.13. rome02/03
  also run the root libvirt labs.
- A skipped "Publish to repo"/"Request release promotion" stage usually means
  `publish.enabled` is missing on the ref being built, or the artifact globs
  in `ci/project.yaml` don't match what `ci/build.sh` produced.
- A `HTTP 500` from `POST /content/deb/package_release_components/` was the
  pulp_deb duplicate-association crash on republish — fixed by lookup-first
  in `versatus-pulp-publish-debs.py` (ci-cd-platform#24). If it reappears,
  read the traceback on the pulp host: `ssh root@pulp.local.versatushpc.com.br
  journalctl -g Traceback`.
- Two different versions in one publish call means stale artifacts leaked
  from a previous build — check that `ci/build.sh` still recreates its
  output dirs and that the artifact globs match what it writes.
- To re-trigger CI without a content change: `git commit --amend --no-edit
  --reset-author && git push --force-with-lease` (PR branches only, never
  master/tags).
- Re-pushing an identical tag on an identical commit does NOT re-trigger the
  multibranch tag build — Jenkins skips already-built revisions. Either the
  tag must move to a new commit (merge something first) or start the tag job
  manually in Jenkins.
- When watching a running Pipeline build from the controller filesystem, the
  terminal signal is `<completed>true</completed>` in `build.xml` —
  `<result>` is pre-written as SUCCESS by the durability checkpoint while
  the build is still running. GitHub commit statuses are also ambiguous when
  a tag and master point at the same commit (same context, last writer wins).
- Parallel-stage output is interleaved in the consolidated build log; for lab
  lane failures read the per-lane logs on the lab host instead. The lab state
  dir lives inside the jenkins-agent service's PrivateTmp:
  `/var/tmp/systemd-private-*-jenkins-agent.service-*/tmp/opencattus-lab/<LAB_NAME>/logs/`.
- UEFI compute nodes: the harness passes an explicit pflash loader plus an
  `<nvram template=...>` (OVMF_VARS_PATH). Without the template, libvirt only
  consults the empty qemu.conf nvram map and fails with "unable to find any
  master var store". Keep edk2-ovmf reasonably current on lab hosts.
- Lab lanes require host-side assets that CI does not manage: the exact
  `BASE_IMAGE`/`CLUSTER_ISO` files named in `ci/lab/*.env` must exist in
  `/var/lib/libvirt/images/` on every x86 lab runner (rome02, rome03), and
  "latest" media must match the pinned `DISTRO_VERSION` (a moved
  AlmaLinux-9-latest ISO broke the alma9 lanes with a copycds/osimage name
  mismatch). Verify before tagging:
  `for h in rome02 rome03; do ssh root@$h.local.versatushpc.com.br 'ls /var/lib/libvirt/images/' ; done`
  against `grep -h 'BASE_IMAGE\|CLUSTER_ISO' ci/lab/*.env | sort -u`.
