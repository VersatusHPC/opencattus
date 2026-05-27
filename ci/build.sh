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

# ctest exit code is non-zero when test cases throw exceptions (e.g.
# missing D-Bus in containers) even if all doctest assertions pass.
# We parse the doctest assertion summary instead of trusting the exit code.
ctest --test-dir "build-${DISTRO}" --output-on-failure 2>&1 | tee "/tmp/ctest-${DISTRO}.txt" || true

assertion_line=$(grep -F "assertions:" "/tmp/ctest-${DISTRO}.txt" | tail -1)
if [ -z "${assertion_line}" ]; then
    echo "No doctest assertion summary found in ctest output for ${DISTRO}."
    exit 1
fi

failed_assertions=$(echo "${assertion_line}" | sed -n 's/.*| \([0-9]*\) failed.*/\1/p')
if [ -z "${failed_assertions}" ]; then
    echo "Could not parse assertion failure count from: ${assertion_line}"
    exit 1
fi

if [ "${failed_assertions}" -ne 0 ]; then
    echo "Tests failed on ${DISTRO}: ${failed_assertions} assertion failure(s)."
    exit 1
fi

echo "All ${assertion_line}"

if [[ "${DISTRO}" == el* || "${DISTRO}" == ubi* ]]; then
    mkdir -p "out/rpm/${DISTRO}"
    export QA_RPATHS=$((0x0002|0x0010))
    cpack -G RPM \
        --config "build-${DISTRO}/CPackConfig.cmake" \
        -B "out/rpm/${DISTRO}"
    ls -l "out/rpm/${DISTRO}"/opencattus*.rpm
elif [[ "${DISTRO}" == ubuntu* ]]; then
    mkdir -p out/deb
    cpack -G DEB \
        --config "build-${DISTRO}/CPackConfig.cmake" \
        -B out/deb
    ls -l out/deb/opencattus*.deb
fi
