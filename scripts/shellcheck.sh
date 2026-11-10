#!/usr/bin/env bash
set -euo pipefail

# Runs shellcheck over the repository's shell, locally and in CI.
#
#   scripts/shellcheck.sh
#
# Covers every tracked *.sh file and the `run:` steps of the composite actions
# under .github/actions. Workflow `run:` steps are covered by
# scripts/actionlint.sh with the same pinned shellcheck.
#
# Each composite-action step is checked as a separate bash script (the steps
# declare `shell: bash`), prefixed so findings name the action and step.
# Extraction needs a YAML parser; pyyaml is fetched by uv, pinned here.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
# shellcheck source=scripts/lib/fetch-tool.sh
source scripts/lib/fetch-tool.sh
# shellcheck source=scripts/lib/shellcheck-bin.sh
source scripts/lib/shellcheck-bin.sh
fetch_shellcheck

status=0

mapfile -t FILES < <(git ls-files --cached --others --exclude-standard '*.sh')
"$SHELLCHECK" --external-sources "${FILES[@]}" || status=1
echo "shellcheck ${SHELLCHECK_VERSION}: ${#FILES[@]} scripts checked"

mapfile -t ACTIONS < <(git ls-files --cached --others --exclude-standard '.github/actions/*/action.yml')
if ! command -v uv >/dev/null 2>&1; then
    echo "uv is required to extract composite action steps" >&2
    exit 1
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
for action in "${ACTIONS[@]}"; do
    uv run --quiet --no-project --with 'pyyaml==6.0.3' python - "$action" "$TMP" <<'EOF'
import pathlib, re, sys, yaml
action, out = sys.argv[1], pathlib.Path(sys.argv[2])
name = pathlib.Path(action).parent.name
steps = yaml.safe_load(open(action))["runs"]["steps"]
for i, step in enumerate(steps):
    if "run" not in step:
        continue
    shell = step.get("shell", "bash")
    if shell != "bash":
        sys.exit(f"{action}: step {i} uses shell '{shell}'; only bash is handled")
    label = re.sub(r"[^A-Za-z0-9._-]+", "_", step.get("name", f"step{i}"))
    (out / f"{name}__{label}.sh").write_text(
        "#!/usr/bin/env bash\n"
        "# Actions run bash with: bash --noprofile --norc -eo pipefail\n"
        "set -eo pipefail\n" + step["run"])
EOF
done
mapfile -t STEPS < <(find "$TMP" -name '*.sh' | sort)
if [[ ${#STEPS[@]} -gt 0 ]]; then
    # GITHUB_* and RUNNER_* are provided by the runner, not assigned here.
    (cd "$TMP" && "$SHELLCHECK" --shell=bash --exclude=SC2154 "${STEPS[@]##*/}") || status=1
fi
echo "shellcheck ${SHELLCHECK_VERSION}: ${#STEPS[@]} composite action steps checked"

exit $status
