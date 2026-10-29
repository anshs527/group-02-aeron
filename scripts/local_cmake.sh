#!/usr/bin/env bash
# Optional, explicit local tool bootstrap for Ubuntu 24.04's older CMake.
# Downloads only; no sudo, package installation, or machine-wide settings.
set -euo pipefail
cd "$(dirname "$0")/.."
version=3.31.6
case "$(uname -s):$(uname -m)" in
  Linux:x86_64) arch=x86_64 ;;
  Linux:aarch64) arch=aarch64 ;;
  *) echo "Install CMake >=3.30 for this platform yourself" >&2; exit 2 ;;
esac
archive="cmake-$version-linux-$arch.tar.gz"
base="https://github.com/Kitware/CMake/releases/download/v$version"
mkdir -p .tools/downloads
(
  cd .tools/downloads
  curl --fail --location --retry 3 --max-time 180 "$base/$archive" -o "$archive"
  curl --fail --location --retry 3 --max-time 60 "$base/cmake-$version-SHA-256.txt" -o SHA-256.txt
  grep -F "  $archive" SHA-256.txt > selected.sha256
  [[ $(wc -l < selected.sha256) == 1 ]]
  sha256sum --check selected.sha256
  tar -xzf "$archive" -C ..
)
echo "CMake installed locally. Run:"
printf 'export PATH="%s/.tools/cmake-%s-linux-%s/bin:$PATH"\n' "$PWD" "$version" "$arch"
