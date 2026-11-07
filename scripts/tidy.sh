#!/usr/bin/env bash
set -euo pipefail

# clang-tidy runner, used locally and by CI.
#
#   scripts/tidy.sh                    changed lines vs origin/main
#   scripts/tidy.sh --base <ref>       changed lines vs <ref>
#   scripts/tidy.sh --all              every tracked source file
#
# Diff mode is the default and what CI enforces: the existing code has not been
# audited, so a full-tree gate would be red from day one and get ignored.
# Checking only the lines a change touches keeps the gate meaningful.
#
# Version pinned for the same reason as clang-format: diagnostics differ
# between releases, so CI and local runs must agree.
CLANG_TIDY_VERSION="23.1.4"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODE="diff"
BASE="origin/main"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)  MODE="all"; shift ;;
        --base) BASE="$2"; shift 2 ;;
        -h|--help) sed -n '3,12p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if command -v uvx >/dev/null 2>&1; then
    TIDY=(uvx --from "clang-tidy==${CLANG_TIDY_VERSION}" clang-tidy)
elif command -v clang-tidy >/dev/null 2>&1 \
    && clang-tidy --version | grep -q "${CLANG_TIDY_VERSION}"; then
    TIDY=(clang-tidy)
else
    echo "Need uvx, or clang-tidy ${CLANG_TIDY_VERSION} on PATH" >&2
    exit 1
fi

# clang-tidy needs the compilation database, and the generated protobuf
# headers it references must already exist.
BUILD_DIR="${TIDY_BUILD_DIR:-}"
if [[ -z "$BUILD_DIR" ]]; then
    for d in build/debug build/sys-debug build/*/; do
        if [[ -f "$d/compile_commands.json" ]]; then BUILD_DIR="${d%/}"; break; fi
    done
fi
if [[ -z "$BUILD_DIR" || ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "No compile_commands.json found. Configure a preset first, e.g." >&2
    echo "  cmake --preset debug && cmake --build --preset debug --target hft_proto" >&2
    exit 1
fi
echo "Using compilation database: $BUILD_DIR"

is_source() { [[ "$1" == *.cpp || "$1" == *.hpp || "$1" == *.h ]]; }

if [[ "$MODE" == "all" ]]; then
    mapfile -t FILES < <(git ls-files 'src/*.cpp' 'tests/*.cpp')
    [[ ${#FILES[@]} -eq 0 ]] && { echo "No files to check."; exit 0; }
    "${TIDY[@]}" -p "$BUILD_DIR" --warnings-as-errors='*' "${FILES[@]}"
    exit $?
fi

# --- diff mode ---------------------------------------------------------------
if ! git rev-parse --verify --quiet "$BASE" >/dev/null; then
    echo "Base ref '$BASE' not found. Fetch it first (CI needs fetch-depth: 0)." >&2
    exit 1
fi
MERGE_BASE="$(git merge-base "$BASE" HEAD)"
echo "Comparing against $BASE (merge-base ${MERGE_BASE:0:12})"

# Collect changed files and the added/modified line ranges in each.
declare -A RANGES=()
CUR=""
while IFS= read -r line; do
    if [[ "$line" == +++\ * ]]; then
        f="${line#+++ }"
        f="${f#b/}"
        CUR=""
        if [[ "$f" != "/dev/null" ]] && is_source "$f"; then CUR="$f"; fi
    elif [[ -n "$CUR" && "$line" == @@* ]]; then
        # @@ -old,n +new,m @@
        hunk="${line#*+}"
        hunk="${hunk%% @@*}"
        startline="${hunk%%,*}"
        count="${hunk#*,}"
        [[ "$count" == "$hunk" ]] && count=1
        [[ "$count" -eq 0 ]] && continue   # pure deletion: nothing to check
        end=$((startline + count - 1))
        RANGES["$CUR"]+="[${startline},${end}],"
    fi
done < <(git diff -U0 "$MERGE_BASE" HEAD -- 'src/*' 'include/*' 'tests/*')

if [[ ${#RANGES[@]} -eq 0 ]]; then
    echo "No changed C++ lines to check."
    exit 0
fi

# Only .cpp files are compilable translation units; headers are checked through
# the TU that includes them, governed by HeaderFilterRegex in .clang-tidy.
FILTER="["
TU_FILES=()
for f in "${!RANGES[@]}"; do
    FILTER+="{\"name\":\"${f}\",\"lines\":[${RANGES[$f]%,}]},"
    [[ "$f" == *.cpp ]] && TU_FILES+=("$f")
done
FILTER="${FILTER%,}]"

if [[ ${#TU_FILES[@]} -eq 0 ]]; then
    # Only headers changed; check the TUs that include them.
    mapfile -t TU_FILES < <(git ls-files 'src/*.cpp' 'tests/*.cpp')
fi

echo "Changed files: ${!RANGES[*]}"
"${TIDY[@]}" -p "$BUILD_DIR" \
    --warnings-as-errors='*' \
    --line-filter="$FILTER" \
    "${TU_FILES[@]}"
