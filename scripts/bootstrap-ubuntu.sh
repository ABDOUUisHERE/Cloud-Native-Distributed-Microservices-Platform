#!/usr/bin/env bash
set -euo pipefail

taskrelay_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -gt 1 || ( $# -eq 1 && "$1" != "--install-tools" ) ]]; then
  echo "Usage: bash scripts/bootstrap-ubuntu.sh [--install-tools]" >&2
  exit 2
fi
# This is a system-owned file, not project input.
source /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 24.04 || "$(dpkg --print-architecture)" != amd64 ]]; then
  echo "The locked package profile requires Ubuntu 24.04 amd64." >&2
  exit 1
fi

taskrelay_packages=()
while IFS= read -r taskrelay_line; do
  [[ -z "$taskrelay_line" || "$taskrelay_line" == \#* ]] && continue
  if [[ ! "$taskrelay_line" =~ ^[a-z0-9.+-]+=[a-zA-Z0-9.+:~_-]+$ ]]; then
    echo "Invalid package lock entry: $taskrelay_line" >&2
    exit 1
  fi
  taskrelay_packages+=("$taskrelay_line")
done < "$taskrelay_root/scripts/dependencies-ubuntu2404.lock"

if [[ "${1:-}" == "--install-tools" ]]; then
  taskrelay_sudo=()
  if [[ "$(id -u)" != 0 ]]; then
    taskrelay_sudo=(sudo)
  fi
  "${taskrelay_sudo[@]}" apt-get update
  "${taskrelay_sudo[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake ninja-build pkg-config python3 clang-format-18 clang-tidy-18 \
    "${taskrelay_packages[@]}"
fi

for taskrelay_spec in "${taskrelay_packages[@]}"; do
  taskrelay_name="${taskrelay_spec%%=*}"
  taskrelay_expected="${taskrelay_spec#*=}"
  taskrelay_actual="$(dpkg-query -W -f='${Version}' "$taskrelay_name" 2>/dev/null || true)"
  if [[ "$taskrelay_actual" != "$taskrelay_expected" ]]; then
    echo "Package mismatch: $taskrelay_name; expected $taskrelay_expected, installed ${taskrelay_actual:-none}." >&2
    echo "Run with --install-tools, or review the lock if the pinned package is no longer available." >&2
    exit 1
  fi
done
for taskrelay_tool in cmake ninja g++ python3 clang-format-18 clang-tidy-18 protoc grpc_cpp_plugin; do
  if ! command -v "$taskrelay_tool" >/dev/null 2>&1; then
    echo "Missing tool: $taskrelay_tool. Run with --install-tools." >&2
    exit 1
  fi
done
printf 'Locked Ubuntu dependencies are ready. Configure with: cmake --preset linux-analysis\n'
