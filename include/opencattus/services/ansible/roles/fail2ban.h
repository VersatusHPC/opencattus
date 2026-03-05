// include/opencattus/services/ansible/roles/fail2ban.h

#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_FAIL2BAN_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_FAIL2BAN_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::fail2ban {

/**
 * @brief Generates the install script for the fail2ban role.
 *
 * @param role The Ansible role data.
 * @param osinfo The operating system information.
 * @return ScriptBuilder with the generated commands.
 */
ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo);

} // namespace opencattus::services::ansible::roles::fail2ban

#endif // OPENCATTUS_SERVICES_ANSIBLE_ROLES_FAIL2BAN_H_
