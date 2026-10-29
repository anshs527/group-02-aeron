#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
formatter=${CLANG_FORMAT:-clang-format-18}
command -v "$formatter" >/dev/null || { echo "Missing clang-format-18 (or set CLANG_FORMAT)" >&2; exit 2; }
version=$("$formatter" --version)
[[ "$version" == *'version 18.'* ]] || { echo "Use clang-format 18 for consistent formatting: $version" >&2; exit 2; }
case "${1:---check}" in
  --check) flags=(--dry-run --Werror) ;;
  --fix) flags=(-i) ;;
  *) echo "Usage: $0 [--check|--fix]" >&2; exit 2 ;;
esac
# No generated headers or third-party code are included.
find cpp tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 |
  xargs -0 -r "$formatter" "${flags[@]}"
