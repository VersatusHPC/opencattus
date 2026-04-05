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
       lab with one compute node and an OpenHPC MPI hello-world run.
   * - Rocky Linux 9.7 + Confluent
     - Validated
     - Verified unattended in the EL9 libvirt/KVM lab with one compute node,
       external plus management networks, and an OpenHPC MPI hello-world run.
   * - EL10
     - Not validated
     - Porting work starts only after the EL9 baseline is stable.

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
