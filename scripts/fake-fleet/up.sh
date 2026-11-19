#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STATE_DIR="${DC_FAKE_FLEET_DIR:-/tmp/dc-fake-fleet}"
PASSPHRASE="${DC_FAKE_FLEET_PASSPHRASE:-testpass123}"
SSHD_BIN="${DC_SSHD_BIN:-/usr/sbin/sshd}"
RRSYNC_BIN="${DC_RRSYNC_BIN:-/usr/bin/rrsync}"
CURRENT_USER="$(id -un)"

PC_IDS=(pc01 pc02 pc03 pc04 pc05 pc06)

usage() {
    cat <<EOF
Usage:
  $(basename "$0") [options]
  $(basename "$0") stop-pc <pcId>
  $(basename "$0") start-pc <pcId>

Options:
  --offline <pcId>        Leave <pcId> sshd stopped (repeatable or comma-separated)
  --unreadable <pcId>     Create a chmod 000 file on <pcId> (repeatable or comma-separated)
  --count-per-pc <N>      Regular files per PC for fixture generation (default: 50)
  --now <epoch>           Reference UNIX epoch for deterministic mtimes
  -h, --help              Show this help

Environment:
  DC_FAKE_FLEET_DIR         Runtime state directory (default: /tmp/dc-fake-fleet)
  DC_FAKE_FLEET_PASSPHRASE  Client key passphrase (default: testpass123)
EOF
}

validate_pc_id() {
    local pc="$1"
    local candidate
    for candidate in "${PC_IDS[@]}"; do
        if [[ "$candidate" == "$pc" ]]; then
            return 0
        fi
    done
    echo "Error: invalid PC id '$pc'. Expected one of: ${PC_IDS[*]}" >&2
    return 1
}

pc_port() {
    local pc="$1"
    local num="${pc#pc}"
    echo $((22100 + 10#$num))
}

wait_for_port() {
    local port="$1"
    local attempts=50
    local i
    for ((i = 0; i < attempts; i++)); do
        if (exec 3<>"/dev/tcp/127.0.0.1/${port}") 2>/dev/null; then
            exec 3<&-
            exec 3>&-
            return 0
        fi
        sleep 0.1
    done
    return 1
}

stop_one_pc() {
    local pc="$1"
    validate_pc_id "$pc"
    local pid_file="${STATE_DIR}/sshd/${pc}/sshd.pid"
    if [[ ! -f "$pid_file" ]]; then
        return 0
    fi
    local pid
    pid="$(tr -d '[:space:]' < "$pid_file" || true)"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        # Verify the PID belongs to sshd before terminating
        local comm=""
        if [[ -r "/proc/${pid}/comm" ]]; then
            comm="$(cat "/proc/${pid}/comm" 2>/dev/null || true)"
        fi
        if [[ -z "$comm" || "$comm" == "sshd" ]]; then
            kill -TERM "$pid" 2>/dev/null || true
            local i
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
}

start_one_pc() {
    local pc="$1"
    validate_pc_id "$pc"
    local sshd_dir="${STATE_DIR}/sshd/${pc}"
    local cfg="${sshd_dir}/sshd_config"
    local log="${sshd_dir}/sshd.log"
    local pid_file="${sshd_dir}/sshd.pid"
    local port
    port="$(pc_port "$pc")"

    if [[ ! -f "$cfg" ]]; then
        echo "Error: sshd_config not found for ${pc} at ${cfg}. Run up.sh first." >&2
        return 1
    fi

    if [[ -f "$pid_file" ]]; then
        local existing_pid
        existing_pid="$(tr -d '[:space:]' < "$pid_file" || true)"
        if [[ -n "$existing_pid" ]] && kill -0 "$existing_pid" 2>/dev/null; then
            return 0
        fi
        rm -f "$pid_file"
    fi

    "$SSHD_BIN" -f "$cfg" -E "$log"

    if ! wait_for_port "$port"; then
        echo "Error: sshd for ${pc} failed to listen on 127.0.0.1:${port}" >&2
        if [[ -f "$log" ]]; then
            cat "$log" >&2
        fi
        return 1
    fi
}

# Handle subcommands first
if [[ $# -ge 1 ]]; then
    case "$1" in
        stop-pc)
            if [[ $# -ne 2 ]]; then
                echo "Usage: $(basename "$0") stop-pc <pcId>" >&2
                exit 1
            fi
            stop_one_pc "$2"
            echo "Stopped ${2}"
            exit 0
            ;;
        start-pc)
            if [[ $# -ne 2 ]]; then
                echo "Usage: $(basename "$0") start-pc <pcId>" >&2
                exit 1
            fi
            start_one_pc "$2"
            echo "Started ${2} on 127.0.0.1:$(pc_port "$2")"
            exit 0
            ;;
    esac
fi

# Parse default `up` options
declare -A OFFLINE_PCS=()
UNREADABLE_ARGS=()
COUNT_PER_PC="50"
NOW_EPOCH=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --offline)
            if [[ $# -lt 2 ]]; then
                echo "Error: --offline requires a value" >&2
                exit 1
            fi
            IFS=',' read -ra items <<< "$2"
            for item in "${items[@]}"; do
                [[ -n "$item" ]] || continue
                validate_pc_id "$item"
                OFFLINE_PCS["$item"]=1
            done
            shift 2
            ;;
        --unreadable)
            if [[ $# -lt 2 ]]; then
                echo "Error: --unreadable requires a value" >&2
                exit 1
            fi
            IFS=',' read -ra items <<< "$2"
            for item in "${items[@]}"; do
                [[ -n "$item" ]] || continue
                validate_pc_id "$item"
                UNREADABLE_ARGS+=(--add-unreadable-on "$item")
            done
            shift 2
            ;;
        --count-per-pc)
            if [[ $# -lt 2 ]]; then
                echo "Error: --count-per-pc requires a value" >&2
                exit 1
            fi
            COUNT_PER_PC="$2"
            shift 2
            ;;
        --now)
            if [[ $# -lt 2 ]]; then
                echo "Error: --now requires a value" >&2
                exit 1
            fi
            NOW_EPOCH="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: unknown argument '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

# Sanity check required binaries
for bin in "$SSHD_BIN" "$RRSYNC_BIN" ssh-keygen ssh-keyscan python3; do
    if ! command -v "$bin" >/dev/null 2>&1 && [[ ! -x "$bin" ]]; then
        echo "Error: required binary not found or not executable: $bin" >&2
        exit 1
    fi
done

# Idempotently clean any previous run in STATE_DIR
if [[ -d "$STATE_DIR" ]]; then
    for pc in "${PC_IDS[@]}"; do
        stop_one_pc "$pc"
    done
    chmod -R u+rwX "$STATE_DIR" 2>/dev/null || true
    rm -rf "$STATE_DIR"
fi

mkdir -p "${STATE_DIR}/ssh" "${STATE_DIR}/sshd" "${STATE_DIR}/pcs"
chmod 700 "${STATE_DIR}/ssh" "${STATE_DIR}/sshd"

# 1. Generate ephemeral ed25519 client key protected by PASSPHRASE
CLIENT_KEY="${STATE_DIR}/ssh/collector_key"
ssh-keygen -q -t ed25519 -N "$PASSPHRASE" -C "dc-fake-fleet-client" -f "$CLIENT_KEY"
chmod 600 "$CLIENT_KEY"
chmod 644 "${CLIENT_KEY}.pub"
CLIENT_PUB="$(cat "${CLIENT_KEY}.pub")"

# 2. Generate deterministic fixtures for pc01..pc06
GEN_CMD=(
    python3 "${SCRIPT_DIR}/generate-fixtures.py"
    --base-dir "${STATE_DIR}/pcs"
    --count-per-pc "$COUNT_PER_PC"
)
if [[ -n "$NOW_EPOCH" ]]; then
    GEN_CMD+=(--now "$NOW_EPOCH")
fi
if [[ ${#UNREADABLE_ARGS[@]} -gt 0 ]]; then
    GEN_CMD+=("${UNREADABLE_ARGS[@]}")
fi
"${GEN_CMD[@]}"
ln -sf "${STATE_DIR}/pcs/fixtures_manifest.json" "${STATE_DIR}/fixtures_manifest.json"

# 3. Configure and start sshd instances on 127.0.0.1:22101..22106
for pc in "${PC_IDS[@]}"; do
    port="$(pc_port "$pc")"
    pc_home="${STATE_DIR}/pcs/${pc}"
    sshd_dir="${STATE_DIR}/sshd/${pc}"
    mkdir -p "$sshd_dir"
    chmod 700 "$sshd_dir"

    host_key="${sshd_dir}/ssh_host_ed25519_key"
    ssh-keygen -q -t ed25519 -N "" -C "dc-fake-fleet-${pc}" -f "$host_key"
    chmod 600 "$host_key"

    auth_keys="${sshd_dir}/authorized_keys"
    printf 'restrict,command="%s -ro %s" %s\n' "$RRSYNC_BIN" "$pc_home" "$CLIENT_PUB" > "$auth_keys"
    chmod 600 "$auth_keys"

    cfg="${sshd_dir}/sshd_config"
    cat > "$cfg" <<EOF
ListenAddress 127.0.0.1
Port ${port}
HostKey ${host_key}
PidFile ${sshd_dir}/sshd.pid
AuthorizedKeysFile ${auth_keys}
StrictModes no
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
ChallengeResponseAuthentication no
UsePAM no
AllowUsers ${CURRENT_USER}
PermitRootLogin no
X11Forwarding no
AllowTcpForwarding no
AllowAgentForwarding no
PermitTTY no
LogLevel INFO
EOF
    chmod 600 "$cfg"

    if [[ -z "${OFFLINE_PCS[$pc]:-}" ]]; then
        start_one_pc "$pc"
    fi
done

# 4. Build known_hosts using ssh-keyscan for running PCs (and host pubkey for offline PCs)
KNOWN_HOSTS="${STATE_DIR}/ssh/known_hosts"
: > "$KNOWN_HOSTS"
for pc in "${PC_IDS[@]}"; do
    port="$(pc_port "$pc")"
    if [[ -z "${OFFLINE_PCS[$pc]:-}" ]]; then
        ssh-keyscan -p "$port" -t ed25519 127.0.0.1 2>/dev/null >> "$KNOWN_HOSTS"
    else
        host_pub="${STATE_DIR}/sshd/${pc}/ssh_host_ed25519_key.pub"
        key_type="$(awk '{print $1}' "$host_pub")"
        key_data="$(awk '{print $2}' "$host_pub")"
        printf '[127.0.0.1]:%s %s %s\n' "$port" "$key_type" "$key_data" >> "$KNOWN_HOSTS"
    fi
done
chmod 644 "$KNOWN_HOSTS"

# 5. Write config.json ready for DATA_COLLECTOR_CONFIG
CONFIG_JSON="${STATE_DIR}/config.json"
cat > "$CONFIG_JSON" <<EOF
{
  "sshUser": "${CURRENT_USER}",
  "sshKeyPath": "${CLIENT_KEY}",
  "knownHostsPath": "${KNOWN_HOSTS}",
  "maxParallel": 2,
  "paths": {
    "logs": ".app",
    "screenshots": "screenshot"
  },
  "logs": {
    "includeModifiedAfterEnd": true
  },
  "pcs": [
    { "id": "pc01", "host": "127.0.0.1", "port": 22101 },
    { "id": "pc02", "host": "127.0.0.1", "port": 22102 },
    { "id": "pc03", "host": "127.0.0.1", "port": 22103 },
    { "id": "pc04", "host": "127.0.0.1", "port": 22104 },
    { "id": "pc05", "host": "127.0.0.1", "port": 22105 },
    { "id": "pc06", "host": "127.0.0.1", "port": 22106 }
  ]
}
EOF

echo "Fake fleet is up in ${STATE_DIR}"
echo "  DATA_COLLECTOR_CONFIG=${CONFIG_JSON}"
echo "  Manifest: ${STATE_DIR}/fixtures_manifest.json"
if [[ ${#OFFLINE_PCS[@]} -gt 0 ]]; then
    echo "  Offline PCs: ${!OFFLINE_PCS[*]}"
fi
