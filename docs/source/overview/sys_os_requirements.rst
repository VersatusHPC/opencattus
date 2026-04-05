.. _sys-os-requirements:

===================
System Requirements
===================

Before you attempt to run OpenCATTUS, make sure your head node matches the
current recovery baseline. The project is being stabilized on EL9 first.

Minimum Hardware Requirements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Head node
^^^^^^^^^

- 4 vCPUs minimum for small lab runs. 8 vCPUs is the current practical
  baseline for unattended EL9 recovery work.
- At least 16 GiB of RAM on the head node. 24 GiB was used for the validated
  libvirt/KVM recovery lab.
- At least 200 GiB of disk for headnode image creation, package caches, and
  OpenHPC content.
- Two network interfaces minimum:

  * one external interface
  * one internal management/provisioning interface

- Root access.
- UEFI-capable virtualization or bare metal.

Compute nodes
^^^^^^^^^^^^^

- PXE-bootable VMs or bare-metal nodes.
- 8 GiB of RAM minimum for the current stateless EL9 xCAT path.
- CPU topology in the answerfile must match the virtual or physical node
  topology presented to the OS.

Current Recovery Status
~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :widths: 24 18 28
   :header-rows: 1

   * - Target
     - Status
     - Notes
   * - Rocky Linux 9.7 + xCAT
     - Validated
     - Current recovery baseline. Verified unattended in the EL9 libvirt/KVM
       lab with two compute nodes and an OpenHPC MPI hello-world run spanning
       them.
   * - Rocky Linux 9.7 + Confluent
     - Validated
     - Verified unattended in the EL9 libvirt/KVM lab with two compute nodes,
       external plus management networks, and an OpenHPC MPI hello-world run
       spanning them.
   * - EL10
     - Not validated
     - The first EL10 bootstrap target is Rocky Linux 10 + Confluent. xCAT is
       intentionally out of scope until upstream EL10 support exists.

Current EL9 Support Matrix
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :widths: 30 18 18 34
   :header-rows: 1

   * - Capability
     - xCAT
     - Confluent
     - Notes
   * - Answerfile-driven unattended install
     - Validated
     - Validated
     - Verified in the EL9 libvirt/KVM recovery lab.
   * - Headnode verification
     - Validated
     - Validated
     - ``chronyd``, NFS, MariaDB, Munge, SLURM, and provisioner services
       checked after install.
   * - Single compute node boot and join
     - Validated
     - Validated
     - ``sinfo -N`` reaches a usable node state.
   * - OpenHPC MPI hello world
     - Validated
     - Validated
     - Run through Slurm on the recovered EL9 cluster in both single-node and
       two-node MPI layouts.
   * - External + management network topology
     - Validated
     - Validated
     - This is the currently tested lab topology.
   * - Dedicated service network
     - Not yet validated
     - Not yet validated
     - Parser/model handling was repaired, but there is no end-to-end EL9 lab
       coverage yet.
   * - Dedicated application network / OFED path
     - Not yet validated
     - Not yet validated
     - Still outside the recovered EL9 baseline.
   * - Multi-node cluster
     - Validated
     - Validated
     - Two compute nodes boot, join the cluster, and complete the MPI smoke
       test.
   * - TUI-driven install
     - Not yet validated
     - Not yet validated
     - Recovery work has focused on unattended answerfile installs first.
   * - ``--dump-answerfile`` round-trip
     - Not yet validated
     - Not yet validated
     - Do not treat dumped answerfiles as a recovery baseline yet.

Answerfile Requirements
~~~~~~~~~~~~~~~~~~~~~~~

Unattended installs currently require these sections:

- ``[information]``
- ``[time]``
- ``[hostname]``
- ``[network_external]``
- ``[network_management]``
- ``[slurm]``
- ``[system]`` including ``provisioner=``
- ``[node]``
- one or more ``[node.N]`` sections

Optional sections such as ``[network_service]``, ``[network_application]``,
``[ofed]``, and ``[postfix]`` should be added only when you actually need those
features.
