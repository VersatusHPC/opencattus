.. _ansible:

Ansible
=======

.. note::

   This is the older Vagrant-based harness. Prefer the EL9 libvirt/KVM recovery lab in :ref:`libvirt-kvm-el9` for current validation work.

Configuration
-------------

Please run all commands in this file as root or with elevated privileges.

Dependencies
~~~~~~~~~~~~

* Ansible
* Vagrant
* OpenCATTUS binary
* A ISO for the nodes (WIP)

Example Playbook
~~~~~~~~~~~~~~~~~~

.. literalinclude:: ../../../files/ansible/setup.yml.example
   :language: yaml

Example Vagrantfile
~~~~~~~~~~~~~~~~~~~~

.. literalinclude:: ../../../files/ansible/Vagrantfile.example
   :language: ruby

Running
-------

.. note::

   This should be used inside Ansible (/ansible) directory

Run ``ansible-playbook setup.yml --extra-vars "opencattus_binary_path=/path/to/local/opencattus/binary vagrant_machine_name=machine_name iso_image_path=/path/to/iso/image"`` in the same folder of `"setup.yml"` or the playbook you created.
If you don't want to clean up (remove the virtual machine) after OpenCATTUS ends, set ``"cleanup_needed=false"`` on the --extra-vars.

For the current list of operating systems supported, read section ":doc:`System Requirements <../../../overview/sys_os_requirements>`"
