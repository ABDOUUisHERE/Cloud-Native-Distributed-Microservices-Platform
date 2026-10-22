#!/usr/bin/env bash
set -euo pipefail

taskrelay_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" == "--install-tools" ]]; then
  taskrelay_sudo=()
  if [[ "$(id -u)" != 0 ]]; then
    taskrelay_sudo=(sudo)
  fi
  "${taskrelay_sudo[@]}" apt-get update
  "${taskrelay_sudo[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake ninja-build pkg-config curl zip unzip tar git python3 \
    autoconf automake libtool clang-format-18 clang-tidy-18
elif [[ $# -ne 0 ]]; then
  echo "Usage: bash scripts/bootstrap-wsl.sh [--install-tools]" >&2
  exit 2
fi

for taskrelay_tool in cmake ninja g++ git curl zip unzip tar python3 pkg-config autoconf automake libtoolize; do
  if ! command -v "$taskrelay_tool" >/dev/null 2>&1; then
    echo "Missing tool: $taskrelay_tool. On Ubuntu 24.04, run this script with --install-tools." >&2
    exit 1
  fi
done

taskrelay_revision="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["builtin-baseline"])' "$taskrelay_root/vcpkg.json")"
if [[ ! "$taskrelay_revision" =~ ^[0-9a-f]{40}$ ]]; then
  echo "The manifest must contain a full vcpkg baseline SHA." >&2
  exit 1
fi
taskrelay_vcpkg="${VCPKG_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/taskrelay/vcpkg/$taskrelay_revision}"
if [[ ! -e "$taskrelay_vcpkg" ]]; then
  mkdir -p "$taskrelay_vcpkg"
  git -C "$taskrelay_vcpkg" init --quiet
  git -C "$taskrelay_vcpkg" remote add origin https://github.com/microsoft/vcpkg.git
  git -C "$taskrelay_vcpkg" fetch --depth=1 origin "$taskrelay_revision"
  git -C "$taskrelay_vcpkg" checkout --detach --quiet FETCH_HEAD
fi
if [[ "$(git -C "$taskrelay_vcpkg" rev-parse HEAD)" != "$taskrelay_revision" ]]; then
  echo "VCPKG_ROOT does not match the pinned manifest revision; refusing to modify an existing checkout." >&2
  exit 1
fi
if [[ ! -x "$taskrelay_vcpkg/vcpkg" ]]; then
  bash "$taskrelay_vcpkg/bootstrap-vcpkg.sh" -disableMetrics
fi
printf '\nPinned vcpkg is ready. Set this variable in the current shell:\n'
printf 'export VCPKG_ROOT=%q\n' "$taskrelay_vcpkg"
printf '\nThen configure with: cmake --preset linux-debug\n'
