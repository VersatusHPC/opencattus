#!/usr/bin/env bash
set -euxo pipefail

DISTRO="${1:?Usage: build.sh <distro> <arch>}"
ARCH="${2:?Usage: build.sh <distro> <arch>}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

source "${SCRIPT_DIR}/setup-${DISTRO}.sh"

export OPENCATTUS_SOURCE_CACHE="${OPENCATTUS_SOURCE_CACHE:-/root/.cache/opencattus/sources}"
mkdir -p "${OPENCATTUS_SOURCE_CACHE}"

fetch_source() {
    local url="$1"
    local path="$2"
    local tmp="${path}.tmp"

    if [ -s "${path}" ] && python3 -m zipfile -t "${path}" >/dev/null 2>&1; then
        return 0
    fi

    rm -f "${path}" "${tmp}"
    for attempt in 1 2 3 4 5; do
        if curl --fail --location --retry 8 --retry-delay 5 \
            --connect-timeout 20 --output "${tmp}" "${url}" &&
            python3 -m zipfile -t "${tmp}" >/dev/null 2>&1; then
            mv "${tmp}" "${path}"
            return 0
        fi

        rm -f "${tmp}"
        sleep 5
    done

    echo "Failed to fetch ${url}" >&2
    return 1
}

export OPENCATTUS_FARGS_ARCHIVE="${OPENCATTUS_SOURCE_CACHE}/cmake-forward-arguments-8c50d1f956172edb34e95efa52a2d5cb1f686ed2.zip"
export OPENCATTUS_YCM_ARCHIVE="${OPENCATTUS_SOURCE_CACHE}/ycm-v0.13.0.zip"

fetch_source \
    "https://github.com/polysquare/cmake-forward-arguments/archive/8c50d1f956172edb34e95efa52a2d5cb1f686ed2.zip" \
    "${OPENCATTUS_FARGS_ARCHIVE}"

fetch_source \
    "https://github.com/robotology/ycm/archive/refs/tags/v0.13.0.zip" \
    "${OPENCATTUS_YCM_ARCHIVE}"

python3 -m venv /tmp/conan-venv
/tmp/conan-venv/bin/pip install conan
export PATH="/tmp/conan-venv/bin:${PATH}"

conan profile detect --force
git config --global --add safe.directory "$(pwd)"

cmake -S . -B "build-${DISTRO}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON

cmake --build "build-${DISTRO}" -j"$(nproc)"

# ctest runs everything for console diagnostics; its exit code is
# informational only because test cases aborted by environment exceptions
# (e.g. missing D-Bus in containers) are tolerated.
ctest --test-dir "build-${DISTRO}" --output-on-failure 2>&1 | tee "/tmp/ctest-${DISTRO}.txt" || true

# Gate on doctest's machine-readable JUnit report instead of scraping the
# console summary (issue #60).
./ci/check-doctest-junit.sh \
    "build-${DISTRO}/test/OpenCATTUS-tests" "/tmp/doctest-${DISTRO}-junit.xml"

if [[ "${DISTRO}" == el* || "${DISTRO}" == ubi* ]]; then
    # Repo publishing (ciPublishRepo) expects out/rpm/<distro>/<arch>/*.rpm.
    # Recreate the directory: the agent workspace persists between builds and
    # stale packages from a previous version would be stashed and published.
    rm -rf "out/rpm/${DISTRO}/${ARCH}"
    mkdir -p "out/rpm/${DISTRO}/${ARCH}"
    export QA_RPATHS=$((0x0002|0x0010))
    cpack -G RPM \
        --config "build-${DISTRO}/CPackConfig.cmake" \
        -B "out/rpm/${DISTRO}/${ARCH}"
    ls -l "out/rpm/${DISTRO}/${ARCH}"/opencattus*.rpm
elif [[ "${DISTRO}" == ubuntu* ]]; then
    # Repo publishing expects out/deb/<distro>/<dpkg arch>/*.deb (amd64, not x86_64).
    deb_arch=$(dpkg --print-architecture)
    rm -rf "out/deb/${DISTRO}/${deb_arch}"
    mkdir -p "out/deb/${DISTRO}/${deb_arch}"
    cpack -G DEB \
        --config "build-${DISTRO}/CPackConfig.cmake" \
        -B "out/deb/${DISTRO}/${deb_arch}"
    ls -l "out/deb/${DISTRO}/${deb_arch}"/opencattus*.deb
fi
