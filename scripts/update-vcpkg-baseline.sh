#!/usr/bin/env bash
set -euo pipefail

# Bumps the pinned vcpkg baseline, which Dependabot cannot update.
#
#   scripts/update-vcpkg-baseline.sh              latest vcpkg release tag
#   scripts/update-vcpkg-baseline.sh 2026.10.16   a specific release tag
#
# Updates vcpkg.json and the VCPKG_COMMIT defaults in the Dockerfile, which
# must agree. Review and test the result: a baseline bump moves every C++
# dependency at once.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

API="https://api.github.com/repos/microsoft/vcpkg"

TAG="${1:-}"
if [[ -z "$TAG" ]]; then
    TAG=$(curl -fsSL "$API/releases/latest" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])')
    echo "Latest vcpkg release: $TAG"
fi

SHA=$(curl -fsSL "$API/commits/$TAG" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["sha"])')
[[ "$SHA" =~ ^[0-9a-f]{40}$ ]] || { echo "Could not resolve tag '$TAG' to a commit" >&2; exit 1; }

OLD=$(python3 -I -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')
if [[ "$OLD" == "$SHA" ]]; then
    echo "Already at $TAG ($SHA); nothing to do."
    exit 0
fi

echo "Updating baseline:"
echo "  from $OLD"
echo "  to   $SHA  ($TAG)"

# Rewrite just the one value, preserving file formatting and key order.
python3 -I - "$SHA" <<'PY'
import re, sys
sha = sys.argv[1]
p = "vcpkg.json"
s = open(p).read()
s2, n = re.subn(r'("builtin-baseline"\s*:\s*")[0-9a-f]{40}(")', r"\g<1>" + sha + r"\g<2>", s)
assert n == 1, f"expected exactly one builtin-baseline, found {n}"
open(p, "w").write(s2)
PY

# Keep the Dockerfile's VCPKG_COMMIT defaults in sync (the deps stage verifies
# they match vcpkg.json at build time).
sed -i "s/^ARG VCPKG_COMMIT=.*/ARG VCPKG_COMMIT=${SHA}/" Dockerfile

changed=$(grep -c "^ARG VCPKG_COMMIT=${SHA}$" Dockerfile || true)
echo "Dockerfile VCPKG_COMMIT occurrences updated: $changed"

cat <<EOF

Done. Next:
  1. Review:  git diff vcpkg.json Dockerfile
  2. Rebuild: VCPKG_ROOT=... scripts/dev.sh debug --test
  3. Commit with the release tag ($TAG) in the message.
EOF
