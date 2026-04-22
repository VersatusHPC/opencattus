# RPM Publishing

OpenCATTUS publishes Release RPMs for EL8, EL9, and EL10. The RPM
repository is generated with `createrepo_c`, so clients can install with
`dnf` after adding the matching `.repo` file.

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

## Remote Publishing

The default remote target is:

```text
reposync@172.21.1.40:/mnt/pool1/repos/opencattus
```

The workflow uses `lftp` over SFTP with mirror mode:

```text
mirror --reverse --delete
```

That means `/mnt/pool1/repos/opencattus` is treated as disposable and owned by
the OpenCATTUS RPM repository. Do not place unrelated repositories under that
directory.

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
CI still stages the repository and runs `createrepo_c`, but it skips the remote
sync with a notice.

## Manual Publishing

To stage and publish from a machine that already has RPMs under `out/rpm`:

```bash
scripts/publish-rpms.sh --source-dir out/rpm
```

To stage the repo and generate metadata without syncing:

```bash
scripts/publish-rpms.sh --source-dir out/rpm --skip-sync
```

To preview the SFTP mirror operation:

```bash
scripts/publish-rpms.sh --source-dir out/rpm --dry-run
```

The script accepts these environment variables:

```bash
REMOTE_USER=reposync
REMOTE_HOST=172.21.1.40
REMOTE_PATH=/mnt/pool1/repos/opencattus
SSH_KEY=/path/to/private/key
STAGING_DIR=/tmp/opencattus-rpm-repo
```

## GitHub Release Assets

Publishing the `dnf` repository and attaching RPMs to a GitHub release are
separate operations today. The repository publish flow is automated by CI when
`REPOSYNC_SSH_KEY` is configured. GitHub release assets are uploaded manually,
for example:

```bash
gh release upload v1.0.0 out/rpm/EL8/*.rpm out/rpm/EL9/*.rpm out/rpm/EL10/*.rpm \
  --repo VersatusHPC/opencattus --clobber
```

