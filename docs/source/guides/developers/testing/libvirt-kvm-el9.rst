.. _libvirt-kvm-el9:

===================================
EL9 libvirt/KVM Recovery Lab
===================================

This is the recommended way to validate OpenCATTUS end-to-end on Enterprise Linux 9.

The goal is not a superficial smoke test. The lab creates a headnode VM, one or more PXE-booted compute VMs, runs OpenCATTUS unattended from an answerfile, then verifies that the headnode services and the cluster state are sane afterwards.

Scope
-----

The harness currently targets:

* EL9 cloud image as the headnode base image.
* EL9 DVD ISO for compute image creation.
* The xCAT provisioner path.
* One or more network-booted compute nodes.

This is intentionally narrower than the full product matrix. The current recovery priority is to make one EL9 path trustworthy before attempting EL10 porting work.

Files
-----

The harness lives in ``testing/libvirt``:

* ``testing/libvirt/opencattus-el9-lab.sh`` orchestrates the full libvirt lifecycle.
* ``testing/libvirt/config/rocky9-xcat.env.example`` is the reference configuration.
* ``testing/libvirt/templates/rocky9-xcat.answerfile.ini`` is the answerfile template.
* ``testing/libvirt/scripts/check-headnode.sh`` validates the headnode services after installation.
* ``testing/libvirt/scripts/check-cluster.sh`` waits for the compute nodes to join the cluster.

Host requirements
-----------------

Run the lab from a real EL9 KVM host with root or passwordless sudo for libvirt operations.

Required host packages:

.. code-block:: bash

   sudo dnf install -y cloud-utils libvirt-client libvirt-daemon-driver-network libvirt-daemon-kvm qemu-img qemu-kvm rsync virt-install
   sudo systemctl enable --now libvirtd

You also need:

* An EL9 qcow2 cloud image for the headnode VM.
* An EL9 DVD ISO for the compute-image creation step.
* A locally built ``opencattus`` binary produced on EL9.
* The qcow2 image and ISO stored under ``/var/lib/libvirt/images`` or another path already labeled for libvirt access.

Quick start
-----------

1. Copy the example config and edit the required paths:

   .. code-block:: bash

      cp testing/libvirt/config/rocky9-xcat.env.example /root/opencattus-rocky9.env
      vim /root/opencattus-rocky9.env

2. Run the full unattended lab:

   .. code-block:: bash

      testing/libvirt/opencattus-el9-lab.sh -c /root/opencattus-rocky9.env run

3. Inspect the collected logs:

   .. code-block:: bash

      ls -R /var/tmp/opencattus-lab

Lifecycle commands
------------------

The harness can also be run step-by-step:

.. code-block:: bash

   testing/libvirt/opencattus-el9-lab.sh -c /root/opencattus-rocky9.env create
   testing/libvirt/opencattus-el9-lab.sh -c /root/opencattus-rocky9.env install
   testing/libvirt/opencattus-el9-lab.sh -c /root/opencattus-rocky9.env boot
   testing/libvirt/opencattus-el9-lab.sh -c /root/opencattus-rocky9.env verify
   testing/libvirt/opencattus-el9-lab.sh -c /root/opencattus-rocky9.env collect
   testing/libvirt/opencattus-el9-lab.sh -c /root/opencattus-rocky9.env destroy

What the harness checks
-----------------------

Headnode verification:

* The headnode updates and reboots into the latest available kernel before OpenCATTUS runs, so the installer does not fail the built-in kernel preflight.
* ``chronyd``, ``mariadb``, ``munge``, ``nfs-server``, ``rpcbind``, ``slurmctld``, and ``slurmdbd`` are active.
* xCAT services are active.
* ``showmount -e localhost``, ``sacct``, and ``sinfo -N`` succeed.

Cluster verification:

* Each compute VM is reachable on the management network.
* Each compute node appears in ``sinfo -N``.
* Each node reaches a usable SLURM state instead of remaining down.
* The lab uses loopback BMC addresses and host-side ``virsh`` starts instead of emulating real IPMI hardware.

Why this replaces the older Vagrant path
----------------------------------------

The repo still contains an older ``ansible/`` harness built around ``vagrant-libvirt``, but it is not sufficient for recovery work:

* It only drove a single VM directly.
* It relied on extra Vagrant plugins and ``vagrant scp``.
* It did not verify PXE-booted compute nodes joining the cluster.
* The documentation still described the ISO support as WIP.

The libvirt/KVM harness keeps the control plane explicit: ``virsh``, ``virt-install``, answerfiles, and guest-side checks.

Known limits
------------

* EL10 is out of scope for this harness. Get the EL9 path stable first.
* Confluent is not yet covered here.
* Nested-virtualization CI is not realistic in the current GitHub workflow; this lab is meant for a real EL9 KVM host.
