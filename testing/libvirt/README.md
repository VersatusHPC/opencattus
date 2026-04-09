# Enterprise Linux libvirt/KVM lab harness

This directory contains the recommended end-to-end validation path for
OpenCATTUS on Enterprise Linux.

Scope:

- One EL8 or EL9 cloud-image headnode VM from the supported Enterprise Linux
  family, or the current Rocky Linux 10 cloud image for EL10 bootstrap work.
- One or more PXE-booted compute VMs.
- Unattended answerfile-driven installation.
- Headnode and cluster verification after provisioning.
- Host-side log collection for failed runs.

The currently validated targets are `Rocky Linux 8.10 + xCAT`,
`Rocky Linux 8.10 + Confluent`,
`Rocky Linux 9.7 + xCAT`, `Rocky Linux 9.7 + Confluent`,
`Rocky Linux 10.1 + Confluent`, `AlmaLinux 10.1 + Confluent`,
`RHEL 10.1 + Confluent`, and `Oracle Linux 10.1 + Confluent`.
The current EL10 baseline is still narrower than the full EL9 recovery scope,
and the EL8 baseline is narrower than EL9, but both now have real unattended
lab coverage.

The expansion matrix now also has runnable lab scaffolding for
`AlmaLinux 8/9`, `Oracle Linux 8/9`, and `RHEL 8/9` with both `xCAT` and
`Confluent`. Those extra distro lanes are not yet validated; they exist so we
can drive the next fixing and validation wave without inventing ad hoc lab
configs each time.

## Host requirements

Install the libvirt/KVM toolchain on the host:

```bash
sudo dnf install -y genisoimage ipmitool libvirt-client libvirt-daemon-driver-network libvirt-daemon-kvm qemu-img qemu-kvm rsync virt-install
sudo systemctl enable --now libvirtd
```

If you are validating the xCAT path, also install the VirtualBMC tooling used
by that lab topology:

```bash
sudo python3 -m pip install virtualbmc pyghmi
```

You also need:

- An Enterprise Linux qcow2 cloud image for the headnode that matches the lab
  template you want to run.
- A matching Enterprise Linux DVD ISO for the compute image creation step.
- Either a locally built `opencattus` binary or the full source tree so the harness can build inside the headnode.
- Compute VMs sized realistically for xCAT stateless boot. The current tested floor is `8192` MiB per compute node; `4096` MiB failed during initramfs expansion of `rootimg.cpio.gz`.

Keep the cloud image and ISO under `/var/lib/libvirt/images`, ideally in a dedicated asset directory such as `/var/lib/libvirt/images/opencattus-assets`, unless you have already labeled another path for libvirt access. The harness stores the headnode qcow2 overlay and cloud-init seed ISO there by default to avoid SELinux denials on enforcing Enterprise Linux hosts.

## Quick start

1. Copy one of these environment templates, then set `BASE_IMAGE`,
   `CLUSTER_ISO`, and either `OPENCATTUS_BINARY` or `OPENCATTUS_SOURCE_DIR`:

   - validated Rocky baselines:
     `testing/libvirt/config/rocky8-xcat.env.example`,
     `testing/libvirt/config/rocky8-confluent.env.example`,
     `testing/libvirt/config/rocky9-xcat.env.example`,
     `testing/libvirt/config/rocky9-confluent.env.example`,
     `testing/libvirt/config/rocky9-confluent-service.env.example`,
     `testing/libvirt/config/rocky10-confluent.env.example`,
      `testing/libvirt/config/rocky10-confluent-service.env.example`
   - validated EL10 Confluent lanes:
     `testing/libvirt/config/alma10-confluent.env.example`,
     `testing/libvirt/config/ol10-confluent.env.example`,
     `testing/libvirt/config/rhel10-confluent.env.example`
   - candidate EL8/EL9 distro expansion lanes:
     `testing/libvirt/config/{alma,ol,rhel}{8,9}-{xcat,confluent}.env.example`
2. The Rocky 10 wrapper defaults to a one-node, two-rank MPI smoke. If you
   want the validated two-node MPI path on EL9 or EL10, also set:

```bash
COMPUTE_COUNT=2
MPI_SMOKE_NODES=2
MPI_SMOKE_TASKS=2
```

If you want the harness to validate ``--dump-answerfile`` end to end, also set:

```bash
ANSWERFILE_ROUNDTRIP=1
```

That makes the harness dump a fresh answerfile from the rendered lab input,
copy it back out for inspection, and then rerun the unattended install from
the dumped file instead of the original template.

3. Run the full lab:

```bash
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-xcat.env run
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-confluent.env run
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env run
```

For the EL10 bootstrap path, use:

```bash
testing/libvirt/opencattus-el10-lab.sh -c /path/to/rocky10-confluent.env run
```

For a mirror-backed `RHEL 10 + Confluent` attempt, use:

```bash
testing/libvirt/opencattus-el10-lab.sh -c /path/to/rhel10-confluent.env run
```

For an `AlmaLinux 10 + Confluent` attempt, use:

```bash
testing/libvirt/opencattus-el10-lab.sh -c /path/to/alma10-confluent.env run
```

For an `Oracle Linux 10 + Confluent` attempt, use:

```bash
testing/libvirt/opencattus-el10-lab.sh -c /path/to/ol10-confluent.env run
```

4. Inspect logs under `/var/tmp/opencattus-lab/<lab-name>/logs`.

The harness also stores libvirt-owned disk artifacts under `/var/lib/libvirt/images/opencattus-lab/<lab-name>`.
The `run` command collects logs even when install or verification fails so the failed lab is still debuggable.

The example config deliberately keeps `NODE_REAL_MEMORY_MB` below the VM's assigned RAM. That is not a typo. xCAT unpacks the stateless image in memory during PXE boot, so the guest needs headroom beyond the SLURM `RealMemory` value you advertise to jobs.

The default config also assumes a single active lab on the host. If you want multiple labs at once, override the external and management subnet settings in addition to changing `LAB_NAME`.

The harness accepts these `DISTRO_ID` tokens:

- `rocky`
- `alma`
- `ol`
- `rhel`

The rendered answerfile uses the product-facing distro spelling automatically,
so the lab token does not need to match the answerfile enum name exactly.
If there is no distro-specific answerfile template for a lane yet, the harness
reuses the matching Rocky template and rewrites the distro, version, and
provisioner fields during render.

If you set `OPENCATTUS_MIRROR_URL`, the harness passes that mirror into the
installer and also uses it to bootstrap unentitled `RHEL` headnodes during lab
setup.

## EL-family expansion matrix

| Target | xCAT | Confluent | Notes |
| --- | --- | --- | --- |
| Rocky Linux 8.10 | Validated | Validated | Current EL8 baseline. |
| AlmaLinux 8.10 | Planned | Planned | Candidate lab configs now exist; first unattended runs still pending. |
| Oracle Linux 8.10 | Planned | Planned | Candidate lab configs now exist; expect media-specific tuning during first runs. |
| RHEL 8.10 | Planned | Planned | Candidate lab configs now exist; requires entitled media and repo access. |
| Rocky Linux 9.7 | Validated | Validated | Current EL9 baseline. |
| AlmaLinux 9.7 | Planned | Planned | Candidate lab configs now exist; first unattended runs still pending. |
| Oracle Linux 9 | Planned | Planned | Candidate config is scaffolded with a 9.4 example; adjust `DISTRO_VERSION` to match local media. |
| RHEL 9.7 | Planned | Planned | Candidate lab configs now exist; requires entitled media and repo access. |
| Rocky Linux 10.1 | Out of scope | Validated | Confluent-only EL10 bootstrap path. |
| AlmaLinux 10.1 | Out of scope | Validated | Confluent-only EL10 cloud-image lane. Verified with the same headnode, deploy, and MPI smoke flow as Rocky Linux 10. |
| RHEL 10.1 | Out of scope | Validated | Confluent-only EL10 mirror-backed lane. Uses a qcow2 headnode image plus local mirrored repos. |
| Oracle Linux 10.1 | Out of scope | Validated | Confluent-only EL10 lane. Uses Oracle cloud media and forces the installed RHCK as the default kernel before reboot. |

## EL8 support matrix

These capability tables still describe the currently validated Rocky baselines.
The AlmaLinux, Oracle Linux, and RHEL EL8/EL9 lanes are scaffolded for
validation, but they have not completed these checks yet.

| Capability | xCAT | Confluent | Notes |
| --- | --- | --- | --- |
| Answerfile-driven unattended install | Validated | Validated | Verified in the EL8 libvirt/KVM lab. |
| Headnode verification | Validated | Validated | `chronyd`, NFS, MariaDB, Munge, SLURM, and provisioner services checked after install. The xCAT `lsdef -t osimage` probe is advisory because it can lag behind an otherwise healthy fresh headnode. |
| Single compute node boot and join | Validated | Validated | `sinfo -N` reaches `idle` on the deployed node. |
| OpenHPC MPI hello world | Validated | Validated | Two MPI ranks run through Slurm on the validated single-node EL8 lab. |
| External + management network topology | Validated | Validated | This is the current EL8 lab topology. |
| Dedicated service network | Not yet validated | Not yet validated | Still outside the current EL8 baseline. |
| Dedicated application network / OFED path | Not yet validated | Not yet validated | Still outside the current EL8 baseline. |
| Multi-node cluster | Not yet validated | Not yet validated | EL8 recovery work has only validated the single-node lab paths so far. |
| TUI-driven install | Not yet validated | Not yet validated | Recovery work has focused on unattended answerfile installs first. |
| `--dump-answerfile` round-trip | Validated | Validated | Rocky Linux 8.10 now completes a full dump-regenerate-install cycle in the EL8 libvirt/KVM lab for both xCAT and Confluent. |

## EL9 support matrix

| Capability | xCAT | Confluent | Notes |
| --- | --- | --- | --- |
| Answerfile-driven unattended install | Validated | Validated | Verified in the EL9 libvirt/KVM recovery lab. |
| Headnode verification | Validated | Validated | `chronyd`, NFS, MariaDB, Munge, SLURM, and provisioner services checked after install. |
| Single compute node boot and join | Validated | Validated | `sinfo -N` reaches a usable node state. |
| OpenHPC MPI hello world | Validated | Validated | Run through Slurm on the recovered EL9 cluster in both single-node and two-node MPI layouts. |
| External + management network topology | Validated | Validated | This is the currently tested lab topology. |
| Dedicated service network | Not yet validated | Validated | Rocky Linux 9.7 + Confluent now completes the unattended install, verify, and MPI smoke path with a dedicated `oc-svc0` headnode NIC and a populated `[network_service]` section. xCAT service-network coverage is still pending. |
| Dedicated application network / OFED path | Not yet validated | Not yet validated | Still outside the recovered EL9 baseline. |
| Multi-node cluster | Validated | Validated | Two compute nodes boot, join the cluster, and complete the MPI smoke test. |
| TUI-driven install | Not yet validated | Not yet validated | Recovery work has focused on unattended answerfile installs first. |
| `--dump-answerfile` round-trip | Validated | Validated | Rocky Linux 9.7 now completes a full dump-regenerate-install cycle in the EL9 libvirt/KVM lab for both xCAT and Confluent. |

## EL10 bootstrap target

- First target distro: `Rocky Linux 10`
- First provisioner: `Confluent`
- Install model: fresh install only
- Current validated baseline: headnode install, one or two deployed compute
  nodes, healthy `sinfo`, and MPI smoke tests for both the one-node and
  two-node layouts
- xCAT remains intentionally out of scope for this bootstrap path

## EL10 support matrix

| Capability | Confluent | Notes |
| --- | --- | --- |
| Answerfile-driven unattended install | Validated | Verified from clean Rocky Linux 10.1, AlmaLinux 10.1, RHEL 10.1, and Oracle Linux 10.1 libvirt/KVM runs. |
| Headnode verification | Validated | `chronyd`, NFS, MariaDB, Munge, SLURM, and Confluent services checked after install. |
| Single compute node boot and join | Validated | `sinfo -N` reaches `idle` on the deployed node. |
| OpenHPC MPI hello world | Validated | Two MPI ranks run through Slurm on all currently validated EL10 distro lanes. |
| External + management network topology | Validated | This is the current EL10 lab topology. |
| Multi-node cluster | Validated | Two compute nodes boot, join the cluster, and complete the MPI smoke test across nodes. |
| Dedicated service network | Validated | Rocky 10.1 + Confluent now completes the unattended install, verify, and MPI smoke path with a dedicated `oc-svc0` headnode NIC and a populated `[network_service]` section. |
| Dedicated application network / OFED path | Not yet validated | Still outside the initial EL10 baseline. |
| TUI-driven install | Not yet validated | EL10 work has focused on unattended answerfile installs first. |
| `--dump-answerfile` round-trip | Validated | Rocky Linux 10.1 + Confluent now completes a full dump-regenerate-install cycle in the EL10 libvirt/KVM lab. |

## Release validation workflow

Use this harness as the release gate for Enterprise Linux support instead of
reconstructing ad hoc lab steps.

1. Pick a dedicated lab name and non-overlapping bridges/subnets for each lane
   you want to run in parallel.
2. Copy the closest `testing/libvirt/config/*.env.example` file and set
   `BASE_IMAGE`, `CLUSTER_ISO`, and either `OPENCATTUS_BINARY` or
   `OPENCATTUS_SOURCE_DIR`.
3. Set `OPENCATTUS_MIRROR_URL` whenever you want package resolution to stay on
   the local mirror, especially for `RHEL` lanes.
4. Run `testing/libvirt/opencattus-el8-lab.sh`, `testing/libvirt/opencattus-el9-lab.sh`,
   or `testing/libvirt/opencattus-el10-lab.sh` with the `run` action.
5. On success, keep the collected lab directory under
   `/var/tmp/opencattus-lab/<lab-name>/` as the validation record for that
   lane.
6. On failure, inspect `logs/install.log` first, then the per-host logs under
   `logs/headnode/` and the compute-node console captures.

A lane counts as release-valid only when all of these are true:

- the installer reaches `OpenCATTUS has successfully ended`
- the headnode verification phase completes cleanly
- `sinfo -N` shows the deployed compute node or nodes in a usable state
- `showmount -e localhost` exports `/home`, `/opt/ohpc/pub`, and `/opt/spack`
- the non-root MPI smoke passes through Slurm

For long-running investigations, prefer keeping each rerun in a unique
`LAB_NAME` instead of reusing the same one. That preserves the old logs and
makes it obvious which exact artifact set corresponds to the current result.

## Self-hosted GitHub Actions

This repo's CI is intended to run on your own GitHub Actions runner, not on GitHub-hosted machines.

The fast build/test and packaging workflows can run automatically on the self-hosted runner. The EL9 libvirt lab is different on purpose: it is manual-only, so developers choose when to spend the time and host capacity on a full cluster run.

For the EL9 libvirt workflow, set these repository variables to paths that exist on the self-hosted runner:

- `OPENCATTUS_EL9_BASE_IMAGE`
- `OPENCATTUS_EL9_CLUSTER_ISO`

Run the workflow from the Actions tab when you want a full unattended cluster gate. The workflow uses the checked-out source tree as `OPENCATTUS_SOURCE_DIR`, creates an isolated lab named from the GitHub run id, prints the tail of the collected logs on failure, and destroys the lab afterwards by default. Set the `keep_lab` workflow input to keep the failed or successful lab around for manual inspection.

## Useful commands

```bash
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-xcat.env create
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-xcat.env install
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-xcat.env boot
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-xcat.env verify
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-xcat.env collect
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-xcat.env destroy
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-confluent.env create
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-confluent.env install
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-confluent.env boot
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-confluent.env verify
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-confluent.env collect
testing/libvirt/opencattus-el8-lab.sh -c /path/to/rocky8-confluent.env destroy
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env create
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env install
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env boot
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env verify
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env collect
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env destroy
```

## What this harness verifies

- The headnode VM boots with deterministic NIC naming.
- The headnode is normalized before install, including a kernel update and reboot when the cloud image lags the configured repos.
- The harness can build `opencattus` inside the headnode when you provide `OPENCATTUS_SOURCE_DIR` instead of a prebuilt binary.
- OpenCATTUS can run unattended from an answerfile on EL8 and EL9.
- xCAT or Confluent, plus SLURM, NFS, and the core headnode services, are
  active afterwards depending on the selected provisioner.
- The Confluent path can build a diskless image, deploy a node, and reach a
  usable SLURM state.
- The xCAT path can expose simulated BMC endpoints through VirtualBMC, and the
  harness opens the minimal host-side firewalld rule needed for UDP `623`.
- The harness restarts compute VMs with `virsh` for deterministic PXE reboots
  during lab runs.
- The compute VMs can PXE boot on the management network.
- The compute nodes become reachable on the management network and appear in `sinfo`.
- The default compute VM topology now matches the answerfile's SLURM declaration: `2` vCPUs presented as `1` socket, `2` cores, `1` thread.
- The validated EL8 paths can run a non-root OpenHPC MPI hello-world smoke test on the deployed compute node.
- The validated EL9 paths can run an OpenHPC MPI hello-world smoke test across one or two compute nodes.
- The validated EL8 xCAT and Confluent paths and the validated EL9 and EL10
  Confluent paths can dump a fresh answerfile with ``--dump-answerfile`` and
  complete the unattended install from that dumped file.

## Current limits

- This harness now claims an initial Rocky 10.1 + Confluent baseline, not
  broad EL10 readiness.
- The `testing/libvirt/opencattus-el10-lab.sh` wrapper exists so the EL10
  branch can reuse the same host-side lab orchestration while the product port
  is still underway.
- The currently validated multi-node EL9 topology is two compute nodes on
  external plus management networks. EL9 Confluent service-network coverage is
  now also validated, but the xCAT service-network and broader application
  network variants still need coverage.
- The currently validated EL8 topology is a single xCAT-managed or
  Confluent-managed compute node on external plus management networks.
- The currently validated EL10 service-network topology adds a dedicated
  headnode `oc-svc0` interface and `[network_service]`, but the application
  network / OFED path is still outside the EL10 baseline.
- Nested virtualization CI is out of scope for GitHub Actions; this is intended for a real Enterprise Linux KVM host.
