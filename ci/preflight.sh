#!/usr/bin/env bash
set -euxo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

source "${SCRIPT_DIR}/setup-el10.sh"

dnf install -y dbus-daemon

python3 -m venv /tmp/conan-venv
/tmp/conan-venv/bin/pip install conan
export PATH="/tmp/conan-venv/bin:${PATH}"

conan profile detect --force
git config --global --add safe.directory "$(pwd)"

export OPENCATTUS_SOURCE_CACHE="${OPENCATTUS_SOURCE_CACHE:-/root/.cache/opencattus/sources}"
mkdir -p "${OPENCATTUS_SOURCE_CACHE}"

if [ -x ./ci/check-format.sh ]; then
    ./ci/check-format.sh || echo "WARNING: format check failed (non-fatal during pipeline validation)"
fi

cmake -S . -B build-preflight -G Ninja -DBUILD_TESTING=ON
cmake --build build-preflight --target opencattus OpenCATTUS-tests -j"$(nproc)"

mkdir -p /run/dbus
dbus-daemon --system --fork 2>/dev/null || true
eval "$(dbus-launch --sh-syntax)"
export DBUS_SESSION_BUS_ADDRESS

ctest --test-dir build-preflight --output-on-failure \
    --exclude-regex "cli_dump_answerfile"
