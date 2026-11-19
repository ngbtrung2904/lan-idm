#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
FLEET_DIR="${DC_FAKE_FLEET_DIR:-/tmp/dc-fake-fleet}"

resolve_build_dir() {
    if [[ -n "${DC_BUILD_DIR:-}" ]]; then
        echo "$DC_BUILD_DIR"
        return 0
    fi
    local candidate
    for candidate in \
        "${REPO_ROOT}/build/Desktop_Qt_6_7_2-Debug" \
        "${REPO_ROOT}/build/gcc-debug" \
        "${REPO_ROOT}/build"/*; do
        if [[ -x "${candidate}/tests/test_fake_fleet_integration" && -x "${candidate}/tests/test_gui_smoke" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    echo "${REPO_ROOT}/build/Desktop_Qt_6_7_2-Debug"
}

BUILD_DIR="$(resolve_build_dir)"

INTEGRATION_BIN="${BUILD_DIR}/tests/test_fake_fleet_integration"
GUI_SMOKE_BIN="${BUILD_DIR}/tests/test_gui_smoke"

for bin in "$INTEGRATION_BIN" "$GUI_SMOKE_BIN"; do
    if [[ ! -x "$bin" ]]; then
        echo "Error: test executable not found or not executable: $bin" >&2
        echo "Build first with: cmake --build --preset gcc-debug" >&2
        exit 1
    fi
done

cleanup() {
    local rc=$?
    DC_FAKE_FLEET_DIR="$FLEET_DIR" "${REPO_ROOT}/scripts/fake-fleet/down.sh" || true
    exit "$rc"
}
trap cleanup EXIT INT TERM

echo "=== Starting fake fleet (${FLEET_DIR}) ==="
DC_FAKE_FLEET_DIR="$FLEET_DIR" "${REPO_ROOT}/scripts/fake-fleet/up.sh" \
    --offline pc05 \
    --unreadable pc04

echo "=== Running test_fake_fleet_integration ==="
QT_QPA_PLATFORM=offscreen DC_FAKE_FLEET_DIR="$FLEET_DIR" "$INTEGRATION_BIN"

echo "=== Running test_gui_smoke ==="
QT_QPA_PLATFORM=offscreen DC_FAKE_FLEET_DIR="$FLEET_DIR" "$GUI_SMOKE_BIN"

echo "=== Verifying zero orphan ssh-agent or rsync processes ==="
orphan_agents="$(pgrep -af '[s]sh-agent.*datacollector-agent-' || true)"
if [[ -n "$orphan_agents" ]]; then
    echo "Error: orphan Data Collector ssh-agent process(es) detected:" >&2
    echo "$orphan_agents" >&2
    exit 1
fi

orphan_rsync="$(pgrep -af "[r]sync.*(127\\.0\\.0\\.1|${FLEET_DIR})" || true)"
if [[ -n "$orphan_rsync" ]]; then
    echo "Error: orphan rsync process(es) detected:" >&2
    echo "$orphan_rsync" >&2
    exit 1
fi

echo "=== All integration and GUI smoke tests passed with zero orphan processes ==="
