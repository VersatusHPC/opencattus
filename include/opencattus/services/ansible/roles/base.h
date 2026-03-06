#ifndef OPENCATTUS_SERVICES_ANSIBLE_ROLE_BASE_H_
#define OPENCATTUS_SERVICES_ANSIBLE_ROLE_BASE_H_

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/scriptbuilder.h>

namespace opencattus::services::ansible::roles::base {

ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo);

};

#endif
