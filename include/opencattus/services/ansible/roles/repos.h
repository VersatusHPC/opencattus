#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_REPOS_H
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_REPOS_H

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::repos {

void run(const Role& role);

}

#endif
