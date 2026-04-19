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
* The xCAT and Confluent provisioner paths.
* The EL9 service-network and DOCA 3.2 LTS Confluent recovery lanes.
* One or more network-booted compute nodes.

This is intentionally narrower than the full product matrix. The current recovery priority is to make one EL9 path trustworthy before attempting EL10 porting work.

Files
-----

The harness lives in ``testing/libvirt``:

* ``testing/libvirt/opencattus-el9-lab.sh`` orchestrates the full libvirt lifecycle.
* ``testing/libvirt/config/rocky9-xcat.env.example`` and ``testing/libvirt/config/rocky9-confluent.env.example`` are the reference configurations.
* ``testing/libvirt/config/rocky9-confluent-service.env.example`` and
  ``testing/libvirt/config/rocky9-confluent-doca-32lts.env.example`` extend the
  EL9 Confluent matrix for service-network and DOCA validation.
* ``testing/libvirt/templates/rocky9-xcat.answerfile.ini`` and ``testing/libvirt/templates/rocky9-confluent.answerfile.ini`` are the answerfile templates.
* ``testing/libvirt/scripts/check-headnode.sh`` validates the headnode services after installation.
* ``testing/libvirt/scripts/check-cluster.sh`` waits for the compute nodes to join the cluster.
* ``testing/libvirt/scripts/check-mpi.sh`` compiles and runs the OpenHPC MPI hello-world smoke test through Slurm.
* ``testing/libvirt/run-el9-regression.sh`` runs multiple prepared EL9 env
  files through the shared harness and prints a consolidated pass/fail summary.

Host requirements
-----------------

Run the lab from a real EL9 KVM host with root or passwordless sudo for libvirt operations.

Required host packages:

.. code-block:: bash

   sudo dnf install -y genisoimage ipmitool libvirt-client libvirt-daemon-driver-network libvirt-daemon-kvm qemu-img qemu-kvm rsync virt-install
   sudo systemctl enable --now libvirtd

If you are validating the xCAT path, also install:

.. code-block:: bash

   sudo python3 -m pip install virtualbmc pyghmi

You also need:

* An EL9 qcow2 cloud image for the headnode VM.
* An EL9 DVD ISO for the compute-image creation step.
* Either a locally built EL9 ``opencattus`` binary or the full source tree so the harness can build inside the headnode.
* The qcow2 image and ISO stored under ``/var/lib/libvirt/images`` or another path already labeled for libvirt access.
* Compute VMs with enough RAM for xCAT stateless boot. The current tested floor is ``8192`` MiB per compute node; ``4096`` MiB failed while unpacking ``rootimg.cpio.gz`` in initramfs.

Quick start
-----------

1. Copy the example config and edit the required paths:

   .. code-block:: bash

      cp testing/libvirt/config/rocky9-xcat.env.example /root/opencattus-rocky9.env
      vim /root/opencattus-rocky9.env

   Or use ``testing/libvirt/config/rocky9-confluent.env.example`` if you are
   validating the Confluent path.

   Set ``BASE_IMAGE``, ``CLUSTER_ISO``, and either ``OPENCATTUS_BINARY`` or
   ``OPENCATTUS_SOURCE_DIR``.

   If you already have a TUI-generated answerfile that matches the lab topology,
   set ``ANSWERFILE_SOURCE_PATH`` as well. The harness will copy that file into
   the lab working directory and rewrite ``[system] disk_image`` to the staged
   ISO path before running the unattended install.

   For the validated two-node MPI path, also set:

   .. code-block:: bash

      COMPUTE_COUNT=2
      MPI_SMOKE_NODES=2
      MPI_SMOKE_TASKS=2

2. Run the full unattended lab:

   .. code-block:: bash

      testing/libvirt/opencattus-el9-lab.sh -c /root/opencattus-rocky9.env run

3. Inspect the collected logs:

   .. code-block:: bash

      ls -R /var/tmp/opencattus-lab

   ``run`` collects logs even if install or verification fails, so you still
   get the failed lab state to inspect.

   The reference config intentionally sets ``NODE_REAL_MEMORY_MB`` lower than
   ``COMPUTE_MEMORY_MB``. xCAT unpacks the stateless root image in memory
   during PXE boot, so the guest needs RAM headroom beyond the SLURM memory
   you advertise to jobs.

   The reference address plan also assumes one active lab at a time. If you
   want concurrent labs on the same host, change the subnet-related variables
   in addition to ``LAB_NAME``.

For the validated parallel EL9 release sweep, run:

.. code-block:: bash

   testing/libvirt/run-el9-regression.sh -j 3 \
     /root/rocky9-confluent.env \
     /root/rocky9-confluent-service.env \
     /root/rocky9-confluent-doca-32lts.env

The wrapper stores per-lane console logs under
``/var/tmp/opencattus-el9-regression/<timestamp>/`` and prints a final summary
when every lane finishes.

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
* The harness can build ``opencattus`` inside the headnode when you provide ``OPENCATTUS_SOURCE_DIR`` instead of a prebuilt binary.
* ``chronyd``, ``mariadb``, ``munge``, ``nfs-server``, ``rpcbind``, ``slurmctld``, and ``slurmdbd`` are active.
* xCAT or Confluent services are active, depending on the selected provisioner.
* ``showmount -e localhost``, ``sacct``, and ``sinfo -N`` succeed.

Cluster verification:

* Each compute VM is reachable on the management network.
* Each compute node appears in ``sinfo -N``.
* Each node reaches a usable SLURM state instead of remaining down.
* On the xCAT path, VirtualBMC exposes each compute node as an IPMI endpoint on the management subnet, and the harness opens the minimal host-side firewalld rule needed for UDP ``623`` from the headnode.
* The harness restarts compute VMs with ``virsh`` so PXE reboots stay deterministic during unattended runs.
* The default compute VM topology is kept consistent with the answerfile values that feed ``slurm.conf``: ``2`` vCPUs exposed as ``1`` socket, ``2`` cores, ``1`` thread.
* The validated EL9 xCAT and Confluent runs can execute an OpenHPC MPI hello-world smoke test across one or two compute nodes.
* The validated EL9 recovery sweep now includes four real lanes on the same
  staged binary: Rocky Linux 9.7 + xCAT, Rocky Linux 9.7 + Confluent, Rocky
  Linux 9.7 + Confluent + service network, and Rocky Linux 9.7 + Confluent +
  DOCA 3.2 LTS.

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
* The currently validated multi-node EL9 topology is two compute nodes on external plus management networks.
* Nested-virtualization CI is not realistic in the current GitHub workflow; this lab is meant for a real EL9 KVM host.
* The DOCA lane is materially slower than the plain EL9 Confluent lanes because
  it builds the Mellanox DKMS modules inside the image path. Treat that longer
  runtime as expected unless the log stops advancing.
