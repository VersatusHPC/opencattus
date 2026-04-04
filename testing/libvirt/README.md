# EL9 libvirt/KVM recovery harness

This directory contains the recommended end-to-end validation path for OpenCATTUS on Enterprise Linux 9.

Scope:

- One EL9 cloud-image headnode VM.
- One or more PXE-booted compute VMs.
- Unattended answerfile-driven installation.
- Headnode and cluster verification after provisioning.
- Host-side log collection for failed runs.

The first supported target is `Rocky Linux 9 + xCAT`. That is intentional. The current codebase is not in a state where it makes sense to pretend EL10 or the Confluent path are already covered.

## Host requirements

Install the libvirt/KVM toolchain on the EL9 host:

```bash
sudo dnf install -y genisoimage ipmitool libvirt-client libvirt-daemon-driver-network libvirt-daemon-kvm qemu-img qemu-kvm rsync virt-install
sudo systemctl enable --now libvirtd
sudo python3 -m pip install virtualbmc pyghmi
```

You also need:

- An EL9 qcow2 cloud image for the headnode.
- An EL9 DVD ISO for the compute image creation step.
- Either a locally built EL9 `opencattus` binary or the full source tree so the harness can build inside the headnode.
- Compute VMs sized realistically for xCAT stateless boot. The current tested floor is `8192` MiB per compute node; `4096` MiB failed during initramfs expansion of `rootimg.cpio.gz`.

Keep the cloud image and ISO under `/var/lib/libvirt/images` unless you have already labeled another path for libvirt access. The harness stores the headnode qcow2 overlay and cloud-init seed ISO there by default to avoid SELinux denials on enforcing EL9 hosts.

## Quick start

1. Copy `testing/libvirt/config/rocky9-xcat.env.example` and set `BASE_IMAGE`, `CLUSTER_ISO`, and either `OPENCATTUS_BINARY` or `OPENCATTUS_SOURCE_DIR`.
2. Run the full lab:

```bash
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env run
```

3. Inspect logs under `/var/tmp/opencattus-lab/<lab-name>/logs`.

The harness also stores libvirt-owned disk artifacts under `/var/lib/libvirt/images/opencattus-lab/<lab-name>`.
The `run` command collects logs even when install or verification fails so the failed lab is still debuggable.

The example config deliberately keeps `NODE_REAL_MEMORY_MB` below the VM's assigned RAM. That is not a typo. xCAT unpacks the stateless image in memory during PXE boot, so the guest needs headroom beyond the SLURM `RealMemory` value you advertise to jobs.

The default config also assumes a single active lab on the host. If you want multiple labs at once, override the external and management subnet settings in addition to changing `LAB_NAME`.

## Useful commands

```bash
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
- OpenCATTUS can run unattended from an answerfile on EL9.
- xCAT, SLURM, NFS, and the core headnode services are active afterwards.
- VirtualBMC exposes simulated BMC endpoints on the management network and the harness opens the minimal host-side firewalld rule needed for UDP `623`.
- The harness restarts compute VMs with `virsh` for deterministic PXE reboots during lab runs.
- The compute VMs can PXE boot on the management network.
- The compute nodes become reachable on the management network and appear in `sinfo`.
- The default compute VM topology now matches the answerfile's SLURM declaration: `2` vCPUs presented as `1` socket, `2` cores, `1` thread.

## Current limits

- This harness does not claim EL10 readiness.
- This harness does not validate the Confluent path yet.
- Nested virtualization CI is out of scope for GitHub Actions; this is intended for a real EL9 KVM host.
