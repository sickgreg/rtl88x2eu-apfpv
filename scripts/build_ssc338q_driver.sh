#!/usr/bin/env bash
set -euo pipefail

# This helper builds the rtl88x2eu AP/FPV driver module against the Sigmastar
# ssc338q kernel headers.  It can operate in two modes:
#   1. Run from inside a checked-out driver repository (default).  In this case
#      the script reuses the existing tree and never touches the network.
#   2. Run from outside the tree with REPO_URL (and optionally WORKDIR) set to
#      point at a clone source.  The script will clone or update that repo before
#      building.  This keeps the workflow flexible for developers with private
#      mirrors or local caches.

if [[ $(id -u) -eq 0 ]]; then
  echo "[!] Running as root will leave the build artefacts owned by root." >&2
fi

: "${TOOLCHAIN_PREFIX:?Set TOOLCHAIN_PREFIX to the Sigmastar toolchain prefix (e.g. arm-openipc-linux-gnueabihf-)}"
: "${KERNEL_DIR:?Set KERNEL_DIR to the path of the ssc338q kernel source or headers (e.g. ~/openipc/output/build/linux-ssc338q)}"

if [[ -z "${KERNEL_VERSION:-}" ]]; then
  utsrelease_header="${KERNEL_DIR}/include/generated/utsrelease.h"
  if [[ -r "${utsrelease_header}" ]]; then
    KERNEL_VERSION=$(sed -n 's/^#define UTS_RELEASE "\(.*\)"/\1/p' "${utsrelease_header}")
    if [[ -n "${KERNEL_VERSION}" ]]; then
      export KERNEL_VERSION
      echo "[*] Derived KERNEL_VERSION='${KERNEL_VERSION}' from ${utsrelease_header}" >&2
    else
      echo "[!] Failed to parse kernel release from ${utsrelease_header}." >&2
      echo "[!] Export KERNEL_VERSION manually (e.g. 5.10.113-openipc-ssc338q)." >&2
      exit 1
    fi
  else
    echo "[!] Set KERNEL_VERSION to the kernel release string (e.g. 5.10.113-openipc-ssc338q)." >&2
    echo "[!] ${utsrelease_header} is missing or unreadable." >&2
    exit 1
  fi
fi

: "${KERNEL_VERSION:?Set KERNEL_VERSION to the kernel release string (e.g. 5.10.113-openipc-ssc338q)}"

SCRIPT_DIR=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=""
DEFAULT_REPO_URL="https://github.com/sickgreg/rtl88x2eu-apfpv.git"

if git -C "${SCRIPT_DIR}" rev-parse --show-toplevel >/dev/null 2>&1; then
  REPO_DIR=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)
else
  REPO_URL=${REPO_URL:-${DEFAULT_REPO_URL}}
  if [[ -z "${REPO_URL}" ]]; then
    echo "[!] Set REPO_URL to a reachable rtl88x2eu mirror (e.g. ssh://git@...)" >&2
    exit 1
  fi
  WORKDIR=${WORKDIR:-$PWD}
  REPO_DIR="${WORKDIR}/rtl88x2eu-apfpv"

  echo "[*] Using external repository at ${REPO_DIR}" >&2
  if [[ -d "${REPO_DIR}/.git" ]]; then
    echo "[*] Repository already exists; fetching latest changes" >&2
    if git -C "${REPO_DIR}" remote >/dev/null 2>&1; then
      git -C "${REPO_DIR}" fetch --all --prune
    else
      echo "[!] No remotes configured; skipping fetch" >&2
    fi
  else
    echo "[*] Cloning rtl88x2eu-apfpv from ${REPO_URL}" >&2
    git clone "${REPO_URL}" "${REPO_DIR}"
  fi
fi

if [[ -z "${REPO_DIR}" || ! -d "${REPO_DIR}" ]]; then
  echo "[!] Failed to locate a driver repository.  Ensure this script lives inside the tree or set REPO_URL/WORKDIR." >&2
  exit 1
fi

# Ensure required host packages are installed.
if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y build-essential git bc flex bison libncurses-dev libssl-dev libelf-dev
else
  echo "[!] apt-get not found; install build dependencies manually." >&2
fi

cd "${REPO_DIR}"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "[!] ${REPO_DIR} is not a git repository.  Verify REPO_URL/WORKDIR." >&2
  exit 1
fi

echo "[*] Building rtl88x2eu module for ssc338q"
make clean || true
make ARCH=arm \
     CROSS_COMPILE="${TOOLCHAIN_PREFIX}" \
     KSRC="${KERNEL_DIR}" \
     KVER="${KERNEL_VERSION}" \
     modules

find_module_artifact() {
  if [[ -n "${MODULE_ARTIFACT:-}" && -f "${MODULE_ARTIFACT}" ]]; then
    echo "${MODULE_ARTIFACT}"
    return 0;
  fi

  local candidates=(88x2eu.ko 8812eu.ko rtl88x2eu.ko)
  for candidate in "${candidates[@]}"; do
    if [[ -f "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done

  local first_module
  first_module=$(ls -1 *.ko 2>/dev/null | head -n1 || true)
  if [[ -n "${first_module}" ]]; then
    echo "${first_module}"
    return 0
  fi

  return 1
}

MODULE_FILE=$(find_module_artifact || true)

# Optionally strip the module if the toolchain provides a strip binary.
if [[ -n "${MODULE_FILE}" ]] && command -v "${TOOLCHAIN_PREFIX}strip" >/dev/null 2>&1; then
  "${TOOLCHAIN_PREFIX}strip" -g "${MODULE_FILE}"
fi

if [[ -n "${MODULE_FILE}" ]]; then
  echo "[*] Build complete. Module located at $(realpath "${MODULE_FILE}")"
else
  echo "[!] Build finished but no .ko module was detected in $(pwd)." >&2
fi

echo "[*] To install into a rootfs, set INSTALL_MOD_PATH and rerun make modules_install, e.g.:"
echo "    INSTALL_MOD_PATH=/path/to/rootfs make ARCH=arm CROSS_COMPILE=${TOOLCHAIN_PREFIX} KSRC=${KERNEL_DIR} KVER=${KERNEL_VERSION} modules_install"
