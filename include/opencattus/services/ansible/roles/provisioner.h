#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_PROVISIONER_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_PROVISIONER_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::provisioner {

void run(const Role& role);

};

#endif
