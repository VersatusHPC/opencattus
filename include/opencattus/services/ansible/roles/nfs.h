#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLES_NFS_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLES_NFS_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::nfs {

ScriptBuilder installScript(const Role& role, const OS& osinfo);

}

#endif
