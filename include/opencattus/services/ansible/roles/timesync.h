#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_TIMESYNC_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_TIMESYNC_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::timesync {

/**
 * @brief Generates the install script for the timesync role.
 *
 * @param role The Ansible role data.
 * @param osinfo The operating system information.
 * @return ScriptBuilder with the generated commands.
 */
ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo);

} // namespace opencattus::services::ansible::roles::timesync

#endif // OPENCATTUS_SERVICES_ANSIBLE_ROLES_TIMESYNC_H_
