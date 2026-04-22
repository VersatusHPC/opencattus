# Package Publishing

OpenCATTUS publishes Release RPMs for EL8, EL9, and EL10, plus a DEB package
for Ubuntu 24.04. RPM repositories are generated with `createrepo_c`, and the
Ubuntu repository is a flat APT repository generated with `dpkg-scanpackages`.

## What CI Does

The self-hosted GitHub Actions workflow builds RPMs with CPack in three
containers:

- `EL8`
- `EL9`
- `EL10`

Each build uploads an RPM artifact named `opencattus-rpm-EL*`. The publish
job downloads those artifacts into `out/rpm-publish-input` and runs:

```bash
scripts/publish-rpms.sh --source-dir out/rpm-publish-input
```

The script stages the repository like this:

```text
el8/
  versatushpc-opencattus.repo
  x86_64/*.rpm
  repodata/repomd.xml
el9/
  versatushpc-opencattus.repo
  x86_64/*.rpm
  repodata/repomd.xml
el10/
  versatushpc-opencattus.repo
  x86_64/*.rpm
  repodata/repomd.xml
```

For each EL generation, the script runs:

```bash
createrepo_c --update "${staged_el_directory}"
```

That creates the `repodata/` directory required by `dnf`.

The workflow also builds a DEB package in an Ubuntu 24.04 container with
CPack. It uploads an artifact named `opencattus-deb-ubuntu24`. The publish job
downloads it into `out/deb-publish-input` and runs:

```bash
scripts/publish-debs.sh --source-dir out/deb-publish-input
```

The script stages the APT repository like this:

```text
ubuntu24/
  versatushpc-opencattus.list
  *.deb
  Packages
  Packages.gz
```

## Remote Publishing

The default remote target is:

```text
reposync@172.21.1.40:/mnt/pool1/repos/opencattus
```

The workflow uses `lftp` over SFTP with mirror mode:

```text
mirror --reverse --delete
```

Each publisher mirrors only the subtree it owns:

- `scripts/publish-rpms.sh` owns `el8/`, `el9/`, and `el10/`.
- `scripts/publish-debs.sh` owns `ubuntu24/`.

Do not place unrelated files inside those subdirectories.

## SSH Key

Automatic remote publishing requires the GitHub Actions secret:

```text
REPOSYNC_SSH_KEY
```

This secret must contain the private key used by CI to log in as `reposync` on
the repository server. The matching public key must be in:

```text
/home/reposync/.ssh/authorized_keys
```

The GitHub-hosted service does not log in to the repository server. The job
runs on the self-hosted runner in our infrastructure, starts a Podman
container, writes the private key into the workflow workspace, and the
container uses that key for SFTP.

If `REPOSYNC_SSH_KEY` is not configured and the runner has no local SSH key,
CI still stages the repository and generates metadata, but it skips the remote
sync with a notice.

## Manual Publishing

To stage and publish from a machine that already has RPMs under `out/rpm`:

```bash
scripts/publish-rpms.sh --source-dir out/rpm
```

To stage and publish from a machine that already has DEBs under `out/deb`:

```bash
scripts/publish-debs.sh --source-dir out/deb
```

To stage the repo and generate metadata without syncing:

```bash
scripts/publish-rpms.sh --source-dir out/rpm --skip-sync
scripts/publish-debs.sh --source-dir out/deb --skip-sync
```

To preview the SFTP mirror operation:

```bash
scripts/publish-rpms.sh --source-dir out/rpm --dry-run
scripts/publish-debs.sh --source-dir out/deb --dry-run
```

The script accepts these environment variables:

```bash
REMOTE_USER=reposync
REMOTE_HOST=172.21.1.40
REMOTE_PATH=/mnt/pool1/repos/opencattus
SSH_KEY=/path/to/private/key
STAGING_DIR=/tmp/opencattus-rpm-repo
```

For DEB publishing, `STAGING_DIR` defaults to `/tmp/opencattus-deb-repo`.

## GitHub Release Assets

Publishing the package repositories and attaching packages to a GitHub release
are separate operations today. The repository publish flow is automated by CI
when `REPOSYNC_SSH_KEY` is configured. GitHub release assets are uploaded
manually, for example:

```bash
gh release upload v1.1.0 out/rpm/EL8/*.rpm out/rpm/EL9/*.rpm out/rpm/EL10/*.rpm out/deb/*.deb \
  --repo VersatusHPC/opencattus --clobber
```
