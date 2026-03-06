#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_SPACK_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_SPACK_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::spack {

/**
 * @brief Generates the install script for the spack role.
 *
 * @param role The Ansible role data.
 * @param osinfo The operating system information.
 * @return ScriptBuilder with the generated commands.
 */
ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo);

} // namespace opencattus::services::ansible::roles::spack

#endif // OPENCATTUS_SERVICES_ANSIBLE_ROLES_SPACK_H_
