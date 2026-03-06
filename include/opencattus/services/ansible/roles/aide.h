#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_AIDE_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_AIDE_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::aide {

ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo);

} // namespace opencattus::services::ansible::roles::aide

#endif // OPENCATTUS_SERVICES_ANSIBLE_ROLES_AIDE_H_
