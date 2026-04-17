#!/usr/bin/env bash

set -Eeuo pipefail

binary=${1:?missing OpenCATTUS test binary path}

assert_fails() {
    local output_var=${1}
    shift

    set +e
    local output
    output=$("$@" 2>&1)
    local status=$?
    set -e

    [[ ${status} -ne 0 ]]
    printf -v "${output_var}" '%s' "${output}"
}

assert_contains() {
    local output=${1}
    local needle=${2}

    grep -Fq "${needle}" <<< "${output}"
}

assert_not_contains() {
    local output=${1}
    local needle=${2}

    ! grep -Fq "${needle}" <<< "${output}"
}

assert_fails output "${binary}" --definitely-not-an-option
assert_contains "${output}" "Command line error:"
assert_contains "${output}" "Run with --help"

assert_fails output "${binary}" --cli --dry
assert_contains "${output}" "The command-line questionnaire is not implemented yet."
assert_contains "${output}" "Use --tui"

if [[ $(id -u) -ne 0 ]]; then
    assert_fails output "${binary}" --answerfile test/sample/answerfile/correct.answerfile.ini
    assert_contains "${output}" "OpenCATTUS needs administrator privileges"
    assert_contains "${output}" "sudo"
    assert_not_contains "${output}" "terminate called"
    assert_not_contains "${output}" "std::runtime_error"
fi
