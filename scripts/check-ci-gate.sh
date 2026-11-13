#!/usr/bin/env bash
set -euo pipefail

# Checks that the CI merge gate still gates everything it is meant to.
#
#   scripts/check-ci-gate.sh
#
# Branch protection requires exactly one status check: the ci-success job of
# .github/workflows/ci.yml, which fails unless every other job succeeded. One
# stable context is what lets job names change without stranding open pull
# requests (see "Branch protection" in README.md), but it relocates the risk:
#
#   - a job missing from ci-success's `needs:` is a job the gate ignores;
#   - a ci-success without `if: always()` is skipped when a dependency fails,
#     and a skipped required check does not block a merge;
#   - .github/rulesets/main.json is applied by hand, so a name that does not
#     match the job requires a context no run will ever report.
#
# None of the three fails loudly on its own. This check fails for all of them.
#
# Parsing the workflow needs a YAML parser; pyyaml is fetched by uv, pinned
# here to the same version scripts/shellcheck.sh uses.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v uv >/dev/null 2>&1; then
    echo "uv is required to parse the workflow" >&2
    exit 1
fi

uv run --quiet --no-project --with 'pyyaml==6.0.3' python - <<'EOF'
import json
import pathlib
import sys

import yaml

GATE = "ci-success"
WORKFLOW = pathlib.Path(".github/workflows/ci.yml")
RULESET = pathlib.Path(".github/rulesets/main.json")

jobs = yaml.safe_load(WORKFLOW.read_text())["jobs"]
if GATE not in jobs:
    sys.exit(f"{WORKFLOW}: no '{GATE}' job; branch protection has nothing to require")

gate = jobs[GATE]
errors = []

# Every other job must be gated.
gated = set(jobs) - {GATE}
needs = gate.get("needs", [])
needs = {needs} if isinstance(needs, str) else set(needs)
for job in sorted(gated - needs):
    errors.append(f"job '{job}' is missing from {GATE}'s needs:, so the gate ignores it")
for job in sorted(needs - gated):
    errors.append(f"{GATE} needs '{job}', which is not a job in this workflow")

# The gate must report a conclusion even when a dependency did not succeed.
if "always()" not in str(gate.get("if", "")):
    errors.append(
        f"{GATE} has no `if: always()`; it would be skipped once a job fails, "
        "and a skipped required check does not block a merge"
    )

# The ruleset must require that job, and only that job.
name = gate.get("name", GATE)
rules = json.loads(RULESET.read_text())["rules"]
contexts = [
    check["context"]
    for rule in rules
    if rule["type"] == "required_status_checks"
    for check in rule["parameters"]["required_status_checks"]
]
if contexts != [name]:
    errors.append(
        f"{RULESET} requires {contexts}; expected exactly ['{name}'], the name "
        f"of the {GATE} job"
    )

for error in errors:
    print(f"::error::{error}", file=sys.stderr)
if errors:
    sys.exit(1)
print(f"CI gate: '{name}' gates all {len(gated)} jobs and is the only required check")
EOF
