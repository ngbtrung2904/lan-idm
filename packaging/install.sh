#!/usr/bin/env bash
# Install Data Collector onto the desktop workstation.
#
# Builds a Release binary (or installs from an existing CMake build directory),
# runs `cmake --install`, verifies that askpass.sh has 0755 permissions, and
# prints next steps for provisioning and launching the application.
#
# Usage:
#   packaging/install.sh [options]
#
# Options:
#   --prefix <dir>       Install prefix (default: /usr/local)
#   --build-dir <dir>    CMake build directory (default: <repo>/build/release)
#   --destdir <dir>      Stage installation under DESTDIR (for packaging/testing)
#   --skip-build         Skip CMake configure/build and install from --build-dir
#   --dry-run            Print planned actions without modifying the system
#   -h, --help           Show this help message

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX="/usr/local"
BUILD_DIR="${REPO_ROOT}/build/release"
DESTDIR_ARG=""
SKIP_BUILD=0
DRY_RUN=0

usage() {
    cat <<'EOF'
Usage: packaging/install.sh [options]

Build and install Data Collector on a Linux desktop.

Options:
  --prefix <dir>       Install prefix (default: /usr/local)
  --build-dir <dir>    CMake build directory (default: <repo>/build/release)
  --destdir <dir>      Stage install under DESTDIR (default: unset)
  --skip-build         Use existing --build-dir without configuring/building
  --dry-run            Print every command without modifying the system
  -h, --help           Show this help and exit

Examples:
  # Standard system install (may require sudo for /usr/local)
  sudo packaging/install.sh

  # Preview all actions without making changes
  packaging/install.sh --dry-run

  # Install from an already-built directory into a staging folder
  packaging/install.sh --skip-build --build-dir build/Desktop_Qt_6_7_2-Debug --destdir /tmp/dc-stage
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)
            [[ $# -ge 2 ]] || { echo "Error: --prefix requires a directory." >&2; exit 1; }
            PREFIX="$2"
            shift 2
            ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "Error: --build-dir requires a directory." >&2; exit 1; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --destdir)
            [[ $# -ge 2 ]] || { echo "Error: --destdir requires a directory." >&2; exit 1; }
            DESTDIR_ARG="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

run_cmd() {
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        printf '[dry-run]'
        printf ' %q' "$@"
        printf '\n'
    else
        "$@"
    fi
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Error: required tool '$1' was not found in PATH." >&2
        exit 1
    fi
}

require_cmd cmake

# 1. Configure and build Release (unless --skip-build was specified)
if [[ "${SKIP_BUILD}" -eq 0 ]]; then
    JOBS=4
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    fi

    echo "==> Configuring Release build in ${BUILD_DIR}"
    run_cmd cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
        -DBUILD_TESTS=OFF

    echo "==> Building Data Collector"
    run_cmd cmake --build "${BUILD_DIR}" --target data-collector --parallel "${JOBS}"
else
    echo "==> Skipping build; using existing build directory: ${BUILD_DIR}"
    if [[ "${DRY_RUN}" -eq 0 && ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
        echo "Error: ${BUILD_DIR}/CMakeCache.txt not found. Run without --skip-build first." >&2
        exit 1
    fi
fi

# 2. Install via cmake --install
INSTALL_CMD=(cmake --install "${BUILD_DIR}" --prefix "${PREFIX}")
echo "==> Installing Data Collector (prefix=${PREFIX}${DESTDIR_ARG:+, DESTDIR=${DESTDIR_ARG}})"
if [[ -n "${DESTDIR_ARG}" ]]; then
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        printf '[dry-run] DESTDIR=%q' "${DESTDIR_ARG}"
        printf ' %q' "${INSTALL_CMD[@]}"
        printf '\n'
    else
        DESTDIR="${DESTDIR_ARG}" "${INSTALL_CMD[@]}"
    fi
else
    run_cmd "${INSTALL_CMD[@]}"
fi

# 3. Verify askpass.sh has 0755 permissions
echo "==> Verifying askpass.sh permissions (expected 0755)"
STAGE_PREFIX="${DESTDIR_ARG}${PREFIX}"
if [[ "${DRY_RUN}" -eq 1 ]]; then
    echo "[dry-run] Would verify askpass.sh under ${STAGE_PREFIX}/lib*/data-collector/askpass.sh has mode 755"
else
    ASKPASS_PATH=""
    for candidate in \
        "${STAGE_PREFIX}/lib/data-collector/askpass.sh" \
        "${STAGE_PREFIX}/lib64/data-collector/askpass.sh" \
        "${STAGE_PREFIX}/lib/"*"/data-collector/askpass.sh"; do
        if [[ -f "${candidate}" ]]; then
            ASKPASS_PATH="${candidate}"
            break
        fi
    done

    if [[ -z "${ASKPASS_PATH}" ]]; then
        echo "Error: askpass.sh was not found under ${STAGE_PREFIX}/lib*/data-collector/" >&2
        exit 1
    fi

    ACTUAL_MODE="$(stat -c '%a' "${ASKPASS_PATH}")"
    if [[ "${ACTUAL_MODE}" != "755" ]]; then
        echo "Warning: ${ASKPASS_PATH} has mode ${ACTUAL_MODE}; fixing to 755."
        chmod 755 "${ASKPASS_PATH}"
        ACTUAL_MODE="$(stat -c '%a' "${ASKPASS_PATH}")"
    fi

    if [[ "${ACTUAL_MODE}" != "755" ]]; then
        echo "Error: Failed to set 0755 on ${ASKPASS_PATH} (current mode: ${ACTUAL_MODE})." >&2
        exit 1
    fi
    echo "    OK: ${ASKPASS_PATH} (mode 0${ACTUAL_MODE})"
fi

# 4. Update desktop database if available and installing to a live system prefix
if [[ -z "${DESTDIR_ARG}" ]] && command -v update-desktop-database >/dev/null 2>&1; then
    APPS_DIR="${PREFIX}/share/applications"
    if [[ "${DRY_RUN}" -eq 1 || -d "${APPS_DIR}" ]]; then
        echo "==> Refreshing desktop entry cache in ${APPS_DIR}"
        run_cmd update-desktop-database "${APPS_DIR}" || true
    fi
fi

# 5. Print next steps
cat <<EOF

Installation complete!

Next steps:
  1. Provision each headless PC (if not already done):
       See ${PREFIX}/share/doc/data-collector/provisioning.md
  2. Configure SSH keys, known_hosts, and ~/.config/DataCollector/config.json:
       ${REPO_ROOT}/scripts/setup-desktop.sh
     (Reference config: ${PREFIX}/share/doc/data-collector/config.example.json)
  3. Launch Data Collector:
       - From your desktop application menu: "Data Collector"
       - Or from a terminal: ${PREFIX}/bin/data-collector
  4. Operator quick-start guide:
       ${PREFIX}/share/doc/data-collector/user-guide.md
EOF
