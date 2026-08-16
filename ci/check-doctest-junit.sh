#!/usr/bin/env bash
# Gate CI on doctest results through the machine-readable JUnit reporter
# instead of scraping the human-oriented console summary, which is not a
# stable interface across doctest versions (issue #60).
#
# Policy (unchanged from the summary-scraping days): assertion failures fail
# the gate; test cases aborted by environment exceptions (missing D-Bus,
# single-NIC containers, ...) are tolerated and only reported. JUnit keeps
# the two apart natively: assertion failures arrive as <failure> elements,
# exceptions as <error> elements.
#
# Usage: check-doctest-junit.sh <test-binary> <junit-xml-path>
# Must be invoked from the repository root: the test binary expects the
# source tree fixtures (test/sample, repos/) relative to its working
# directory.
set -euo pipefail

test_binary=$1
junit_xml=$2

rm -f "${junit_xml}"
# The exit code reflects exceptions as well as failures, so it is not the
# gate; the parsed report below is.
"${test_binary}" --test-case-exclude='*slow*' \
    --reporters=junit --out="${junit_xml}" || true

python3 - "${junit_xml}" <<'EOF'
import sys
import xml.etree.ElementTree as ET

path = sys.argv[1]
try:
    root = ET.parse(path).getroot()
except (OSError, ET.ParseError) as error:
    print(f"Unable to read doctest JUnit report {path}: {error}")
    sys.exit(1)

suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
tests = sum(int(suite.get("tests", 0)) for suite in suites)
failures = sum(int(suite.get("failures", 0)) for suite in suites)
errors = sum(int(suite.get("errors", 0)) for suite in suites)

print(f"doctest JUnit report: {tests} reported test(s), "
      f"{failures} assertion failure(s), "
      f"{errors} environment error(s) tolerated")

if tests == 0:
    print("No tests recorded; refusing to pass an empty report.")
    sys.exit(1)

sys.exit(1 if failures else 0)
EOF
