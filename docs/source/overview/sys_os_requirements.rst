.. _sys-os-requirements:

===================
System Requirements
===================

Before you attempt to run OpenCATTUS, make sure your head node matches the
current validated baseline. The project now has validated unattended lab
coverage on EL8, EL9, and EL10 Confluent paths, but the supported scope is
not identical across those releases. AlmaLinux, Oracle Linux, and RHEL EL8/EL9
lanes now have explicit lab scaffolding for validation work, but Rocky remains
the only distro family with completed end-to-end EL8/EL9 recovery runs.

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
   * - Rocky Linux 8.10 + Confluent
     - Validated
     - Verified unattended in the EL8 libvirt/KVM lab with one compute node,
       external plus management networks, and a non-root OpenHPC MPI
       hello-world smoke run on the deployed node.
   * - Rocky Linux 8.10 + xCAT
     - Validated
     - Verified unattended in the EL8 libvirt/KVM lab with one compute node,
       external plus management networks, and a non-root OpenHPC MPI
       hello-world smoke run on the deployed node.
   * - Rocky Linux 9.7 + xCAT
     - Validated
     - Current recovery baseline. Verified unattended in the EL9 libvirt/KVM
       lab with two compute nodes and an OpenHPC MPI hello-world run spanning
       them.
   * - Rocky Linux 9.7 + Confluent
     - Validated
     - Verified unattended in the EL9 libvirt/KVM lab with two compute nodes,
       external plus management networks, and an OpenHPC MPI hello-world run
       spanning them. The dedicated Confluent service-network topology is also
       now validated.
   * - Rocky Linux 10.1 + Confluent
     - Validated
     - First EL10 bootstrap baseline. Verified unattended in the EL10
       libvirt/KVM lab with one and two deployed compute-node layouts, healthy
       ``sinfo``, and OpenHPC MPI hello-world runs in both the one-node and
       two-node cases.
   * - RHEL 10.1 + Confluent
     - Validated
     - Mirror-backed EL10 expansion lane. Verified unattended with a local
       qcow2 headnode image, mirrored repos, healthy ``sinfo``, exported NFS
       paths, and an OpenHPC MPI hello-world run on the deployed compute node.
   * - AlmaLinux 10.1 + Confluent
     - Validated
     - Cloud-image EL10 expansion lane. Verified unattended with the same
       Confluent-only EL10 bootstrap path, healthy ``sinfo``, exported NFS
       paths, and an OpenHPC MPI hello-world run on the deployed compute node.
   * - Oracle Linux 10.1 + Confluent
     - Validated
     - Cloud-image EL10 expansion lane. Verified unattended after forcing the
       installed RHCK as the default boot target, with healthy ``sinfo``,
       exported NFS paths, and an OpenHPC MPI hello-world run on the deployed
       compute node.
   * - AlmaLinux 8/9 + xCAT or Confluent
     - Planned
     - Candidate EL8/EL9 lab configs now exist for both provisioners, but the
       first unattended validation runs are still pending.
   * - Oracle Linux 8/9 + xCAT or Confluent
     - Planned
     - Candidate EL8/EL9 lab configs now exist for both provisioners, but the
       first unattended validation runs are still pending and may require
       distro-specific media tuning.
   * - RHEL 8/9 + xCAT or Confluent
     - Planned
     - Candidate EL8/EL9 lab configs now exist for both provisioners. These
       paths require entitled RHEL media and repository access during
       validation.

Current EL8 Support Matrix
~~~~~~~~~~~~~~~~~~~~~~~~~~

The EL8 and EL9 capability matrices below still reflect the currently
validated Rocky baselines. AlmaLinux, Oracle Linux, and RHEL now have
candidate lab matrix entries, but they remain unvalidated until the first
end-to-end runs complete.

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
     - Verified in the EL8 libvirt/KVM lab.
   * - Headnode verification
     - Validated
     - Validated
     - ``chronyd``, NFS, MariaDB, Munge, SLURM, and provisioner services
       checked after install. The xCAT ``lsdef -t osimage`` probe is
       advisory because it can lag behind an otherwise healthy fresh
       headnode.
   * - Single compute node boot and join
     - Validated
     - Validated
     - ``sinfo -N`` reaches ``idle`` on the deployed node.
   * - OpenHPC MPI hello world
     - Validated
     - Validated
     - Two MPI ranks run through Slurm on the validated single-node EL8 lab.
   * - External + management network topology
     - Validated
     - Validated
     - This is the current EL8 lab topology.
   * - Dedicated service network
     - Not yet validated
     - Not yet validated
     - Still outside the current EL8 baseline.
   * - Dedicated application network / OFED path
     - Not yet validated
     - Not yet validated
     - Still outside the current EL8 baseline.
   * - Multi-node cluster
     - Not yet validated
     - Not yet validated
     - EL8 recovery work has only validated the single-node lab paths so far.
   * - TUI-driven install
     - Not yet validated
     - Not yet validated
     - Recovery work has focused on unattended answerfile installs first.
   * - ``--dump-answerfile`` round-trip
     - Validated
     - Validated
     - Rocky Linux 8.10 now completes a full dump-regenerate-install cycle in
       the EL8 libvirt/KVM lab for both xCAT and Confluent.

EL10 Bootstrap Matrix
~~~~~~~~~~~~~~~~~~~~~

The currently validated EL10 distro lanes are Rocky Linux 10.1, AlmaLinux
10.1, RHEL 10.1, and Oracle Linux 10.1, all on the Confluent provisioner.

.. list-table::
   :widths: 30 18 52
   :header-rows: 1

   * - Capability
     - Confluent
     - Notes
   * - Answerfile-driven unattended install
     - Validated
     - Verified from clean Rocky Linux 10.1, AlmaLinux 10.1, RHEL 10.1, and
       Oracle Linux 10.1 libvirt/KVM runs.
   * - Headnode verification
     - Validated
     - ``chronyd``, NFS, MariaDB, Munge, SLURM, and Confluent services
       checked after install.
   * - Single compute node boot and join
     - Validated
     - ``sinfo -N`` reaches ``idle`` on the deployed node.
   * - OpenHPC MPI hello world
     - Validated
     - Two MPI ranks run through Slurm on all currently validated EL10 distro
       lanes, including both the one-node layout and the two-node layout on
       Rocky Linux 10.1.
   * - External + management network topology
     - Validated
     - This is the current EL10 lab topology.
   * - Multi-node cluster
     - Validated
     - Two compute nodes boot, join the cluster, and complete the MPI smoke
       test across nodes.
   * - Dedicated service network
     - Validated
     - Rocky Linux 10.1 + Confluent completes the unattended install, verify,
       and MPI smoke path with a dedicated headnode service NIC and a
       populated ``[network_service]`` section.
   * - Dedicated application network / OFED path
     - Not yet validated
     - Still outside the initial EL10 baseline.
   * - TUI-driven install
     - Not yet validated
     - EL10 work has focused on unattended answerfile installs first.
   * - ``--dump-answerfile`` round-trip
     - Validated
     - Rocky Linux 10.1 + Confluent now completes a full dump-regenerate-install
       cycle in the EL10 libvirt/KVM lab.

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
     - Validated
     - Rocky Linux 9.7 + Confluent now completes the unattended install,
       verify, and MPI smoke path with a dedicated headnode service NIC and a
       populated ``[network_service]`` section. xCAT service-network coverage
       is still pending.
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
     - Validated
     - Validated
     - Rocky Linux 9.7 now completes a full dump-regenerate-install cycle in
       the EL9 libvirt/KVM lab for both xCAT and Confluent.

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
