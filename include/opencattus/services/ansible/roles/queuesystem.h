#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_QUEUESYSTEM_H
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_QUEUESYSTEM_H

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::queuesystem {

void run(const Role& role);
};

#endif
