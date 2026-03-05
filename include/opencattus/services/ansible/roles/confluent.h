#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_CONFLUENT_H
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_CONFLUENT_H

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::confluent {

void run(const Role& role);

};

#endif
