#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLE_AUDIT_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLE_AUDIT_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::audit {

ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo);

};

#endif
