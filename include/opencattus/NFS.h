/*
 * Copyright 2023 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_NFS_H_
#define OPENCATTUS_NFS_H_

#include <boost/asio.hpp>
#include <opencattus/messagebus.h>
#include <opencattus/services/scriptbuilder.h>
#include <opencattus/services/xcat.h>
#include <string>

using opencattus::models::OS;

namespace opencattus::services {
/**
 * @class NFS
 * @brief A class representing the NFS service configuration.
 *
 * This class provides methods for configuring, enabling, disabling, starting,
 * and stopping the NFS service.
 */
class NFS {
private:
    std::string m_directoryName;
    std::string m_directoryPath;
    std::string m_permissions;
    std::string m_fullPath;
    boost::asio::ip::address m_address;

public:
    /**
     * @brief Constructs an NFS object with the specified parameters.
     *
     * @param directoryName The name of the directory to be shared via NFS.
     * @param directoryPath The path of the directory to be shared via NFS.
     * @param address The IP address of the NFS server.
     * @param permissions The permissions for the NFS share.
     */
    NFS(const std::string& directoryName, const std::string& directoryPath,
        const boost::asio::ip::address& address,
        const std::string& permissions);

    [[nodiscard]] static opencattus::services::ScriptBuilder installScript(
        const OS& osinfo);
    [[nodiscard]] static opencattus::services::ScriptBuilder imageInstallScript(
        const OS& osinfo,
        const opencattus::services::XCAT::ImageInstallArgs& args);

private:
    /**
     * @brief Sets the full path of the NFS share.
     */
    void setFullPath();
};

};

#endif // OPENCATTUS_NFS_H_
