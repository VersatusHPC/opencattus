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
sudo dnf install -y cloud-utils libvirt-client libvirt-daemon-driver-network libvirt-daemon-kvm qemu-img qemu-kvm rsync virt-install
sudo systemctl enable --now libvirtd
```

You also need:

- An EL9 qcow2 cloud image for the headnode.
- An EL9 DVD ISO for the compute image creation step.
- A locally built `opencattus` binary produced on EL9.

Keep the cloud image and ISO under `/var/lib/libvirt/images` unless you have already labeled another path for libvirt access. The harness stores the headnode qcow2 overlay and cloud-init seed ISO there by default to avoid SELinux denials on enforcing EL9 hosts.

## Quick start

1. Copy `testing/libvirt/config/rocky9-xcat.env.example` and set the three required paths: `BASE_IMAGE`, `CLUSTER_ISO`, and `OPENCATTUS_BINARY`.
2. Run the full lab:

```bash
testing/libvirt/opencattus-el9-lab.sh -c /path/to/rocky9-xcat.env run
```

3. Inspect logs under `/var/tmp/opencattus-lab/<lab-name>/logs`.

The harness also stores libvirt-owned disk artifacts under `/var/lib/libvirt/images/opencattus-lab/<lab-name>`.

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
- OpenCATTUS can run unattended from an answerfile on EL9.
- xCAT, SLURM, NFS, and the core headnode services are active afterwards.
- The compute VMs can PXE boot on the management network.
- The compute nodes become reachable on the management network and appear in `sinfo`.

The lab does not emulate real BMCs. It programs loopback BMC addresses so the xCAT IPMI phase fails fast, then uses `virsh` to start the compute domains for PXE boot.

## Current limits

- This harness does not claim EL10 readiness.
- This harness does not validate the Confluent path yet.
- Nested virtualization CI is out of scope for GitHub Actions; this is intended for a real EL9 KVM host.
