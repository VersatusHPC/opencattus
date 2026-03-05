#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_FIREWALL_H
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_FIREWALL_H

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::firewall {

void run(const Role& role);

};

#endif
