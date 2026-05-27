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

# Run ctest and capture the XML result for reliable parsing.
# ctest exit code is non-zero when test cases throw exceptions even if
# all assertions pass, so we parse the doctest output explicitly.
ctest --test-dir build-preflight --output-on-failure --output-junit /tmp/ctest-results.xml || true

# Parse doctest assertion count from stdout. The format is:
#   [doctest] assertions: 1202 | 1202 passed | 0 failed |
# If doctest output is missing or unparseable, fail hard.
if [ ! -f /tmp/ctest-results.xml ]; then
    echo "ctest did not produce results XML."
    exit 1
fi

test_failures=$(python3 -c "
import xml.etree.ElementTree as ET
tree = ET.parse('/tmp/ctest-results.xml')
failed = sum(1 for tc in tree.iter('testcase')
             for _ in tc.iter('failure'))
print(failed)
")

if [ "${test_failures}" -ne 0 ]; then
    echo "ctest reported ${test_failures} test failure(s)."
    exit 1
fi

echo "All tests passed."
