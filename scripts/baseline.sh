#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'EOF'
Usage: scripts/baseline.sh [-c] [RUNS]

Options:
  -c            Clear benchmarks/baseline.log before writing
  -h, --help    Show this help

Arguments:
  RUNS          Number of benchmark runs (default: 3)

Examples:
  scripts/baseline.sh
  scripts/baseline.sh 5
  scripts/baseline.sh -c
  scripts/baseline.sh -c 5
EOF
  exit 0
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd "${script_dir}/.." && pwd)"
out_dir="${root_dir}/benchmarks"
out_file="${out_dir}/baseline.log"
bin="${root_dir}/build/hft"

mkdir -p "${out_dir}"

if [[ "${1:-}" == "-c" ]]; then
    : > "${out_file}"
    shift
fi

runs=3
if [[ -n "${1:-}" ]]; then
    runs="$1"
fi

{
  echo "$(date -Iseconds)"
  echo "git_commit=$(git -C "${root_dir}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "kernel=$(uname -sr)"
  echo
} >> "${out_file}"

for i in $(seq 1 "${runs}"); do
{
  echo "[baseline] run ${i}/${runs}..."
  {
      echo "=== RUN ${i} $(date -Iseconds) ==="
      /usr/bin/time -v "${bin}" --source replay --bench 2>&1
      echo
  } >> "${out_file}"
}
done

echo "Saved ${runs} runs to ${out_file}"
