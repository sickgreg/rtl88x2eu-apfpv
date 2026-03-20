#!/usr/bin/env bash
set -euo pipefail

# Create one single unified patch covering a commit range.
# Usage:
#   ./export_one_big_diff.sh <base_commit> [output_file]
# Example:
#   ./export_one_big_diff.sh db52957 /tmp/rtl88x2eu_apfpv_big.diff

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <base_commit> [output_file]" >&2
  exit 1
fi

BASE="$1"
OUT="${2:-/tmp/rtl88x2eu_apfpv_big.diff}"

git rev-parse --verify "${BASE}^{commit}" >/dev/null
git diff --binary "${BASE}..HEAD" > "${OUT}"

echo "Wrote: ${OUT}"
echo "Range: ${BASE}..HEAD"
echo "Tip: apply with: git apply ${OUT}"
