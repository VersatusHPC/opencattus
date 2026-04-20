#include <opencattus/functions.h>
#include <opencattus/models/os.h>
#include <opencattus/services/ansible/roles/slurm.h>
#include <opencattus/services/log.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/optional.h>

#include <string_view>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

namespace {
std::string buildNodeDeclaration(std::string_view nodeName,
    std::string_view nodeAddress, std::string_view cpusPerNode,
    std::string_view sockets, std::string_view realMemory,
    std::string_view coresPerSocket, std::string_view threadsPerCore)
{
    return fmt::format(
        "NodeName={} NodeAddr={} NodeHostName={} CPUs={} Sockets={} "
        "RealMemory={} CoresPerSocket={} ThreadsPerCore={} State=UNKNOWN",
        nodeName, nodeAddress, nodeName, cpusPerNode, sockets, realMemory,
        coresPerSocket, threadsPerCore);
}

std::string buildControllerStartupScript()
{
    return R"(
systemctl enable munge slurmctld slurmdbd mariadb
systemctl start munge mariadb
)";
}

std::string buildControllerActivationScript()
{
    return R"slurm(
# slurmctld refuses to start if clustername has changed,
# remove this file mitigate it
rm -rf /var/spool/slurmctld/clustername

systemctl restart slurmdbd
for attempt in $(seq 1 20); do
    if systemctl is-active --quiet slurmdbd && bash -c 'exec 3<>/dev/tcp/127.0.0.1/6819' 2>/dev/null; then
        exec 3>&-
        exec 3<&-
        break
    fi
    sleep 2
done
systemctl is-active --quiet slurmdbd

systemctl restart slurmctld
for attempt in $(seq 1 20); do
    if systemctl is-active --quiet slurmctld; then
        break
    fi
    sleep 2
done
systemctl is-active --quiet slurmctld

echo "slurmctld is $(systemctl is-active slurmctld)"
echo "slurmdbd is $(systemctl is-active slurmdbd)"

# Wait for slurmdbd to accept accounting requests
for attempt in $(seq 1 20); do
    if sacct >/dev/null 2>&1; then
        break
    fi
    sleep 2
done

sacct
)slurm";
}

std::string buildSlurmServerInstallCommand(const opencattus::models::OS& osinfo)
{
    switch (osinfo.getPackageType()) {
        case opencattus::models::OS::PackageType::RPM:
            return "dnf -y install ohpc-slurm-server mariadb-server mariadb";
        case opencattus::models::OS::PackageType::DEB:
            return "DEBIAN_FRONTEND=noninteractive apt-get install -y "
                   "ohpc-slurm-server mariadb-server mariadb-client";
    }

    std::unreachable();
}

std::string buildSlurmConfigSeedScript()
{
    return R"slurm(
if test -f /etc/slurm/slurm.conf.ohpc; then
    \cp /etc/slurm/slurm.conf.ohpc "$slurm_conf"
elif test -f /etc/slurm/slurm.conf.example; then
    \cp /etc/slurm/slurm.conf.example "$slurm_conf"
else
    cat > "$slurm_conf" <<'EOF'
ClusterName=cluster
SlurmctldHost=localhost
MpiDefault=none
ProctrackType=proctrack/cgroup
ReturnToService=1
SlurmctldPidFile=/var/run/slurmctld.pid
SlurmctldPort=6817
SlurmdPidFile=/var/run/slurmd.pid
SlurmdPort=6818
SlurmdSpoolDir=/var/spool/slurmd
SlurmUser=slurm
StateSaveLocation=/var/spool/slurmctld
SwitchType=switch/none
SchedulerType=sched/backfill
SelectType=select/cons_tres
SelectTypeParameters=CR_Core_Memory
SlurmctldLogFile=/var/log/slurmctld.log
SlurmdLogFile=/var/log/slurmd.log
EOF
fi
)slurm";
}
} // namespace

namespace opencattus::services::ansible::roles::slurm {

void run(const Role& role)
{
    using namespace opencattus::utils::singleton;
    namespace optional = opencattus::utils::optional;
    const auto nodesNames = answerfile()->nodes.nodesNames();
    opencattus::functions::abortif(nodesNames.size() < 1,
        "At last one node need to be defined in the answerfile {}",
        answerfile()->path());
    const auto nodesConfig = optional::unwrap(answerfile()->nodes.generic,
        "[node] section not loaded or missing from the answerfile {}",
        answerfile()->path());
    const auto nodesPrefix = optional::unwrap(nodesConfig.prefix,
        "prefix missing in [node] section in the answerfile {}",
        answerfile()->path());
    const auto cpusPerNode = optional::unwrap(nodesConfig.cpus_per_node,
        "cpus_per_node missing in [node] section in the answerfile {}",
        answerfile()->path());
    const auto realMemory = optional::unwrap(nodesConfig.real_memory,
        "real_memory missing in [node] section in the answerfile {}",
        answerfile()->path());
    const auto coresPerSocket = optional::unwrap(nodesConfig.cores_per_socket,
        "cores_per_socket missing in [node] section in the answerfile {}",
        answerfile()->path());
    const auto threadsPerCore = optional::unwrap(nodesConfig.threads_per_core,
        "threads_per_core missing in [node] section in the answerfile {}",
        answerfile()->path());
    const auto sockets = optional::unwrap(nodesConfig.sockets,
        "sockets missing in [node] section in the answerfile {}",
        answerfile()->path());
    std::vector<std::string> nodeDeclarations;
    nodeDeclarations.reserve(answerfile()->nodes.nodes.size());
    std::size_t nodeIndex = 0;

    for (const auto& node : answerfile()->nodes.nodes) {
        nodeIndex++;
        const auto nodeName = optional::unwrap(
            node.hostname, "hostname missing for node {}", nodeIndex);
        const auto nodeAddress = optional::unwrap(
            node.start_ip, "node_ip missing for node {}", nodeName);
        nodeDeclarations.emplace_back(
            buildNodeDeclaration(nodeName, nodeAddress.to_string(), cpusPerNode,
                sockets, realMemory, coresPerSocket, threadsPerCore));
    }

    runner::shell::fmt(R"del(
# SLURM configuration
{slurmServerInstallCommand}
{controllerStartupScript}

# Secure the installation, `mysql -u root` will exit with
# non-zero exit code if this already run before, use `mysql -u root -p`
# to login in the database
#
# If you need to wipeout the database and start over:
# systemctl stop mariadb
# systemctl disable mariadb
# dnf remove -y mariadb mariadb-server mariadb-libs
# rm -rf /var/lib/mysql
# rm -f /var/log/mariadb/*
# rm -f /etc/my.cnf
# rm -rf /etc/my.cnf.d
set +e
MYSQL_ROOT_ARGS=(-u root)
mysql "${{MYSQL_ROOT_ARGS[@]}}" --password='' -e "SELECT 1;" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    MYSQL_ROOT_ARGS=(-u root -p{mariadb_root_pass})
    mysql "${{MYSQL_ROOT_ARGS[@]}}" -e "SELECT 1;" >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "Unable to authenticate to MariaDB as root" >&2
        exit 1
    fi
fi
set -e

mysql "${{MYSQL_ROOT_ARGS[@]}}" 2> /dev/null <<EOF
ALTER USER IF EXISTS 'root'@'localhost' IDENTIFIED BY '{mariadb_root_pass}';

DELETE FROM mysql.user WHERE User='';
DELETE FROM mysql.user WHERE User='root' AND Host NOT IN ('localhost', '127.0.0.1', '::1');

DROP DATABASE IF EXISTS test;
DELETE FROM mysql.db WHERE Db='test' OR Db='test_%';

CREATE DATABASE IF NOT EXISTS slurm_acct_db;

CREATE USER IF NOT EXISTS 'slurm'@'localhost' IDENTIFIED BY '{slurmdb_pass}';
ALTER USER 'slurm'@'localhost' IDENTIFIED BY '{slurmdb_pass}';
GRANT ALL PRIVILEGES ON slurm_acct_db.* TO 'slurm'@'localhost';

FLUSH PRIVILEGES;
EOF

install -d -m 0755 -o slurm -g slurm /var/log/slurm
install -m 0640 -o slurm -g slurm /dev/null /var/log/slurm/slurmdbd.log

cat <<EOF > /etc/slurm/slurmdbd.conf
# Authentication
AuthType=auth/munge

# SLURM DB daemon identity
DbdHost={hostname}
DbdPort=6819
SlurmUser=slurm

# Database storage
StorageType=accounting_storage/mysql
StorageHost=localhost
StorageUser=slurm
StoragePass={slurmdb_pass}
StorageLoc=slurm_acct_db

# Logging & PID
LogFile=/var/log/slurm/slurmdbd.log
PidFile=/run/slurmdbd/slurmdbd.pid
EOF
chown slurm:slurm /etc/slurm/slurmdbd.conf
chmod 600 /etc/slurm/slurmdbd.conf

    )del",
        fmt::arg("controllerStartupScript", buildControllerStartupScript()),
        fmt::arg(
            "slurmServerInstallCommand", buildSlurmServerInstallCommand(os())),
        fmt::arg("hostname", answerfile()->hostname.hostname),
        fmt::arg(
            "mariadb_root_pass", answerfile()->slurm.mariadb_root_password),
        fmt::arg("slurmdb_pass", answerfile()->slurm.slurmdb_password));

    runner::shell::fmt(R"del(
# Minimal /etc/slurm/slurm.conf
slurm_conf=/etc/slurm/slurm.conf
{slurmConfigSeedScript}

sed -i \
  -e "s/ClusterName=.*/ClusterName={cluster_name}/" \
  -e "s/SlurmctldHost=.*/SlurmctldHost={hostname}/" \
  -e "s|SlurmctldLogFile=.*|SlurmctldLogFile=/var/log/slurm/slurmctld.log|" \
  -e "s|SlurmdLogFile=.*|SlurmdLogFile=/var/log/slurm/slurmd.log|" \
  "$slurm_conf"

sed -i \
    -e '/^NodeName=/d' \
    -e '/^PartitionName=/d' \
    -e '/^AccountingStorageType=/d' \
    -e '/^AccountingStorageHost=/d' \
    -e '/^AccountingStoragePort=/d' \
    -e '/^JobAcctGatherType=/d' \
    "$slurm_conf"

)del",
        fmt::arg("slurmConfigSeedScript", buildSlurmConfigSeedScript()),
        fmt::arg("hostname", answerfile()->hostname.hostname),
        fmt::arg("cluster_name", answerfile()->information.cluster_name));

    runner::shell::fmt(R"del(
slurm_conf=/etc/slurm/slurm.conf
{{
    echo "# START AUTO-GENERATED NODE CONFIG"
    cat <<EOF
AccountingStorageType=accounting_storage/slurmdbd
AccountingStorageHost={hostname}
AccountingStoragePort=6819
JobAcctGatherType=jobacct_gather/cgroup
{node_declarations}
EOF
    echo "PartitionName={partition_name} Nodes={node_prefix}[01-{last_node_num:02d}] Default=YES MaxTime=INFINITE State=UP"
    echo "# END AUTO-GENERATED NODE CONFIG"
}} >> $slurm_conf

{controllerActivationScript}
)del",
        fmt::arg(
            "controllerActivationScript", buildControllerActivationScript()),
        fmt::arg("hostname", answerfile()->hostname.hostname),
        fmt::arg("node_declarations", fmt::join(nodeDeclarations, "\n")),
        fmt::arg("node_prefix", nodesPrefix),
        fmt::arg("last_node_num", nodesNames.size()),
        fmt::arg("partition_name", answerfile()->slurm.partition_name));
}

}

TEST_CASE("buildNodeDeclaration pins the management address")
{
    CHECK(
        buildNodeDeclaration("n01", "192.168.30.1", "1", "1", "4096", "1", "1")
        == "NodeName=n01 NodeAddr=192.168.30.1 NodeHostName=n01 CPUs=1 "
           "Sockets=1 RealMemory=4096 CoresPerSocket=1 ThreadsPerCore=1 "
           "State=UNKNOWN");
}

TEST_CASE("buildControllerStartupScript keeps service ordering explicit")
{
    const auto script = buildControllerStartupScript();

    CHECK(script.contains("systemctl enable munge slurmctld slurmdbd mariadb"));
    CHECK(script.contains("systemctl start munge mariadb"));
    CHECK_FALSE(script.contains(
        "systemctl enable --now munge slurmctld slurmdbd mariadb"));
}

TEST_CASE("buildControllerActivationScript waits for slurmdbd before slurmctld")
{
    const auto script = buildControllerActivationScript();

    CHECK(script.contains("systemctl restart slurmdbd"));
    CHECK(script.contains("/dev/tcp/127.0.0.1/6819"));
    CHECK(script.contains("systemctl restart slurmctld"));
    CHECK(script.find("systemctl restart slurmdbd")
        < script.find("systemctl restart slurmctld"));
    CHECK(script.contains("systemctl is-active --quiet slurmctld"));
    CHECK(script.contains("sacct >/dev/null 2>&1"));
}

TEST_CASE("buildSlurmConfigSeedScript falls back to Ubuntu example config")
{
    const auto script = buildSlurmConfigSeedScript();

    CHECK(script.contains("/etc/slurm/slurm.conf.ohpc"));
    CHECK(script.contains("/etc/slurm/slurm.conf.example"));
    CHECK(script.contains("ClusterName=cluster"));
}
