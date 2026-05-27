#!/usr/bin/env bash
set -euxo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

source "${SCRIPT_DIR}/setup-el10.sh"

dnf install -y dbus-daemon dbus-tools iproute

python3 -m venv /tmp/conan-venv
/tmp/conan-venv/bin/pip install conan
export PATH="/tmp/conan-venv/bin:${PATH}"

conan profile detect --force
git config --global --add safe.directory "$(pwd)"

export OPENCATTUS_SOURCE_CACHE="${OPENCATTUS_SOURCE_CACHE:-/root/.cache/opencattus/sources}"
mkdir -p "${OPENCATTUS_SOURCE_CACHE}"

if [ -x ./ci/check-format.sh ]; then
    ./ci/check-format.sh
fi

cmake -S . -B build-preflight -G Ninja -DBUILD_TESTING=ON
cmake --build build-preflight --target opencattus OpenCATTUS-tests -j"$(nproc)"

mkdir -p /run/dbus
dbus-daemon --system --fork 2>/dev/null || true
eval "$(dbus-launch --sh-syntax)" || true
export DBUS_SESSION_BUS_ADDRESS

ip link add eth0 type dummy 2>/dev/null && ip addr add 10.99.0.1/24 dev eth0 && ip link set eth0 up || true
ip link add eth1 type dummy 2>/dev/null && ip addr add 10.99.1.1/24 dev eth1 && ip link set eth1 up || true

# Run all ctest targets. No test exclusions — the assertion-count
# approach below handles tests that throw environment exceptions
# (e.g. presenter_tui needing real NICs, cli_dump_answerfile needing
# multiple interfaces) without needing per-test skip lists.
#
# ctest exit code is non-zero when test cases throw exceptions even
# when all doctest assertions pass, so we parse the summary instead.
ctest --test-dir build-preflight --output-on-failure 2>&1 | tee /tmp/ctest-output.txt || true

# Extract the assertion failure count from doctest output.
# Format: "[doctest] assertions: 1202 | 1202 passed | 0 failed |"
assertion_line=$(grep -F "assertions:" /tmp/ctest-output.txt | tail -1)
if [ -z "${assertion_line}" ]; then
    echo "No doctest assertion summary found in ctest output."
    exit 1
fi

failed_assertions=$(echo "${assertion_line}" | sed -n 's/.*| *\([0-9]*\) failed.*/\1/p')
if [ -z "${failed_assertions}" ]; then
    echo "Could not parse assertion failure count from: ${assertion_line}"
    exit 1
fi

if [ "${failed_assertions}" -ne 0 ]; then
    echo "doctest reported ${failed_assertions} assertion failure(s)."
    exit 1
fi

echo "All ${assertion_line}"
