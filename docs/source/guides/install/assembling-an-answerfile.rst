.. _assembling-an-answerfile:

========================
Assembling an answerfile
========================

OpenCATTUS uses an INI-based answerfile for unattended CLI installs. The
current parser is stricter than the older documentation implied, so it is best
to start from a known template and then adjust it for your environment.

Recommended starting points:

- ``testing/libvirt/templates/rocky8-confluent.answerfile.ini`` for the
  validated EL8 + Confluent lab path.
- ``testing/libvirt/templates/rocky9-xcat.answerfile.ini`` for the currently
  validated EL9 + xCAT path.
- ``testing/libvirt/templates/rocky9-confluent.answerfile.ini`` for the
  validated EL9 + Confluent lab path, including the optional dedicated
  ``[network_service]`` topology exercised by the libvirt service-network
  config.
- ``testing/libvirt/templates/rocky10-confluent.answerfile.ini`` for the
  Rocky Linux 10 + Confluent bootstrap path. The current validated Rocky 10
  baseline covers both the one-node smoke and the two-node MPI layout in the
  libvirt/KVM lab, plus the optional dedicated ``[network_service]`` path.
- ``test/sample/answerfile/`` for older examples and broader config shapes.

Required sections
~~~~~~~~~~~~~~~~~

The current unattended path expects all of these sections:

- ``[information]``
- ``[time]``
- ``[hostname]``
- ``[network_external]``
- ``[network_management]``
- ``[slurm]``
- ``[system]``
- ``[node]``
- one or more ``[node.N]`` sections

The ``[system]`` section must include ``provisioner=``.

For the current EL10 bootstrap path, use ``distro=rocky`` with the exact
EL10 minor version you are targeting and set ``provisioner=confluent``. The
product now rejects ``provisioner=xcat`` on EL10 explicitly.

Optional sections
~~~~~~~~~~~~~~~~~

These sections are optional and should only be added when you actually need
them:

- ``[network_service]`` for a dedicated service or BMC network
- ``[network_application]`` for an application fabric such as InfiniBand
- ``[ofed]`` to enable non-inbox OFED handling
- ``[postfix]`` and related subsections for mail relay configuration

Information
~~~~~~~~~~~

This section identifies the cluster and the administrator contact.

.. code-block:: ini

    [information]
    cluster_name=opencattus
    company_name=opencattus-enterprises
    administrator_email=foo@example.com

Time
~~~~

This section defines the cluster timezone, timeserver, and locale.

.. code-block:: ini

    [time]
    timezone=America/Sao_Paulo
    timeserver=0.br.pool.ntp.org
    locale=en_US.UTF-8  # Must follow the desired OS supported locales.

Hostname
~~~~~~~~

This section defines the headnode hostname and the cluster domain.

.. code-block:: ini

    [hostname]
    hostname=opencattus
    domain_name=cluster.example.com

Networks
~~~~~~~~

At minimum you need external and management networks. Service and application
networks are optional.

External
~~~~~~~~

This network is the headnode's outside-facing interface.

.. code-block:: ini

    [network_external]
    interface=enp1s0
    domain_name=cluster.external.example.com
    # ip_address, subnet_mask, gateway, nameservers, and mac_address are
    # optional if they can be discovered from the live host interface.

Management
~~~~~~~~~~

This network installs and manages the compute nodes. It is required.

.. code-block:: ini

    [network_management]
    interface=enp8s0
    ip_address=172.26.255.254
    subnet_mask=255.255.0.0
    domain_name=cluster.example.com
    # gateway, nameservers, and mac_address are optional

Application
~~~~~~~~~~~

Use this only when you need a dedicated application fabric.

.. code-block:: ini

    [network_application]
    interface=ib0
    ip_address=172.26.0.254
    subnet_mask=255.255.0.0
    domain_name=cluster.application.example.com

Service
~~~~~~~

Use this when your service or BMC traffic is on a dedicated network.

.. code-block:: ini

    [network_service]
    interface=enp2s0
    ip_address=172.25.0.254
    subnet_mask=255.255.255.0
    domain_name=cluster.service.example.com

System
~~~~~~

This section defines the installation ISO, distro, version, and provisioner.

.. code-block:: ini

    [system]
    disk_image=/root/Rocky-8.10-x86_64-dvd.iso
    distro=rocky
    version=8.10
    provisioner=confluent  # use xcat for the validated EL9 xCAT path
    # kernel is optional and mainly used for specialized flows

Nodes
~~~~~

``[node]`` contains the generic defaults for all nodes. Each ``[node.N]``
section provides the per-node data that cannot be derived generically.

.. code-block:: ini

    [node]
    prefix=n
    padding=2
    node_ip=172.26.0.1
    node_root_password=pwd
    sockets=1
    cores_per_socket=1
    threads_per_core=1
    real_memory=4096
    bmc_username=admin
    bmc_password=admin
    bmc_serialport=0
    bmc_serialspeed=9600

Each concrete node still needs its own section with a hostname or an implied
name, a MAC address, and a BMC address:

.. code-block:: ini

    [node.1]
    hostname=n01
    mac_address=52:54:00:00:20:11
    node_ip=172.26.0.1
    bmc_address=172.25.0.101
