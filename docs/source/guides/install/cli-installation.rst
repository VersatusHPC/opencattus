.. _cli-installation:

=============================================
Installation Guide for Command Line Interface
=============================================

Use the CLI mode when you already know the target network layout and want an
unattended install driven by an answerfile.

Before you run the installer:

1. Read :doc:`System Requirements <../../overview/sys_os_requirements>`.
2. Prepare the answerfile described in :ref:`assembling-an-answerfile`.
3. Copy the ISO and the ``opencattus`` binary to the head node.

The current validated baselines are Rocky Linux 8.10, Rocky Linux 9.7, and
Rocky Linux 10.1, each with both the xCAT and Confluent paths tested
end-to-end in the libvirt/KVM lab. See
:doc:`System Requirements <../../overview/sys_os_requirements>` for the full
per-release matrices. Treat other combinations as unvalidated until they are
explicitly covered by the recovery lab or release notes.

Show the full CLI help with:

.. code-block:: bash

   ./opencattus -h

Run an unattended install from an answerfile with:

.. code-block:: bash

   sudo ./opencattus -u -a /path/to/answerfile.ini

Useful options during recovery work:

.. code-block:: bash

   sudo ./opencattus -d -u -a /path/to/answerfile.ini
   sudo ./opencattus --dump-answerfile /path/to/output.ini

``-d`` performs a dry run and is useful for parser and workflow validation
without modifying the node.

If you are validating the project itself rather than operating a real cluster,
prefer the EL9 libvirt/KVM harness documented in
``testing/libvirt/README.md``. It creates the headnode and compute VMs,
generates a lab-specific answerfile, runs OpenCATTUS unattended, and verifies
the resulting cluster state.
