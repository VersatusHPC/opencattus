#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_SSHD_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_SSHD_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::sshd {

ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo);

} // namespace opencattus::services::ansible::roles::sshd

#endif
