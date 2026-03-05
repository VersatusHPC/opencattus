#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_CHECK_H
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_CHECK_H

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::check {

void run(const Role& role);

};

#endif
