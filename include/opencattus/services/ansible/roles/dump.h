#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_DUMP_H
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_DUMP_H

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::dump {

void run(const Role& role);

}

#endif
