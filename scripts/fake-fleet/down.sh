#!/usr/bin/env bash
set -euo pipefail

STATE_DIR="${DC_FAKE_FLEET_DIR:-/tmp/dc-fake-fleet}"

if [[ ! -d "$STATE_DIR" ]]; then
    echo "Fake fleet state directory not found (${STATE_DIR}); nothing to stop."
    exit 0
fi

# Stop all sshd instances cleanly via their pidfiles
shopt -s nullglob
for pid_file in "${STATE_DIR}"/sshd/*/sshd.pid; do
    pid="$(tr -d '[:space:]' < "$pid_file" 2>/dev/null || true)"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        comm=""
        if [[ -r "/proc/${pid}/comm" ]]; then
            comm="$(cat "/proc/${pid}/comm" 2>/dev/null || true)"
        fi
        if [[ -z "$comm" || "$comm" == "sshd" ]]; then
            kill -TERM "$pid" 2>/dev/null || true
            for ((i = 0; i < 30; i++)); do
                if ! kill -0 "$pid" 2>/dev/null; then
                    break
                fi
                sleep 0.1
            done
            if kill -0 "$pid" 2>/dev/null; then
                kill -KILL "$pid" 2>/dev/null || true
            fi
        fi
    fi
    rm -f "$pid_file"
done
shopt -u nullglob

# Restore permissions so chmod 000 files never block cleanup
chmod -R u+rwX "$STATE_DIR" 2>/dev/null || true
rm -rf "$STATE_DIR"

echo "Fake fleet stopped and ${STATE_DIR} removed."
