#!/usr/bin/env bash
# setup-desktop.sh — Set up the Ubuntu desktop operator station for Data Collector.
#
# Generates a passphrase-protected ed25519 SSH key pair, interactively verifies and
# pins each PC's SSH host key fingerprint into known_hosts, optionally distributes
# the key by running provision-pc.sh on each PC, writes config.json, and verifies
# read-only rsync access against every configured PC.
#
# Idempotent and safe. Use --dry-run to preview all changes without modifying the system.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SSH_USER="user"
KEY_PATH_RAW="~/.local/share/DataCollector/collector_key"
KNOWN_HOSTS_RAW="~/.local/share/DataCollector/known_hosts"
CONFIG_OUT_RAW="~/.config/DataCollector/config.json"
HOSTS_SPEC=""
TIMEZONE=""
SKIP_TIMEZONE=0
SKIP_DISTRIBUTE=0
KEEP_PASSWORD_AUTH=0
DRY_RUN=0

TEMP_AGENT_PID=""
TEMP_AGENT_DIR=""

cleanup() {
    if [[ -n "$TEMP_AGENT_PID" ]]; then
        kill "$TEMP_AGENT_PID" 2>/dev/null || true
    fi
    if [[ -n "$TEMP_AGENT_DIR" && -d "$TEMP_AGENT_DIR" ]]; then
        rm -rf "$TEMP_AGENT_DIR"
    fi
    if [[ -n "${KEY_PATH:-}" && -f "${KEY_PATH:-}.pub" ]]; then
        ssh-add -d "${KEY_PATH}.pub" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

usage() {
    cat <<'EOF'
Usage: setup-desktop.sh --hosts <id:host[:port],...> [OPTIONS]

Set up the Ubuntu desktop operator station for Data Collector.

Required:
  --hosts <spec>            Comma-separated list of PCs in id:[user@]host[:port] format.
                            Example (same user): pc01:192.168.10.11,pc02:192.168.10.12
                            Example (per-PC user): pc01:user1@192.168.10.11,pc02:user2@192.168.10.12

Options:
  --user <ssh_user>         Default SSH username on remote PCs when not specified in --hosts
                            (default: user)
  --key-path <path>         Path to ed25519 collector key
                            (default: ~/.local/share/DataCollector/collector_key)
  --known-hosts <path>      Path to Data Collector known_hosts file
                            (default: ~/.local/share/DataCollector/known_hosts)
  --config-out <path>       Path to write config.json
                            (default: ~/.config/DataCollector/config.json)
  --timezone <tz>           Timezone to pass to provision-pc.sh during remote setup
                            (default: desktop's current timezone, or UTC)
  --skip-timezone           Pass --skip-timezone to provision-pc.sh (leave remote PC
                            timezones untouched).
  --skip-distribute         Skip copying/running provision-pc.sh on the PCs (use when
                            PCs were already provisioned manually).
  --keep-password-auth      Pass --keep-password-auth to provision-pc.sh during remote
                            distribution (for staged rollout).
  --dry-run                 Print every change without modifying the system.
  -h, --help                Show this help message and exit.

Examples:
  # Preview all setup actions without modifying the system:
  ./scripts/setup-desktop.sh \
      --hosts pc01:192.168.10.11,pc02:192.168.10.12 \
      --dry-run

  # Full interactive setup (key generation, host fingerprint verification, PC provisioning):
  ./scripts/setup-desktop.sh \
      --user user \
      --hosts pc01:192.168.10.11,pc02:192.168.10.12,pc03:192.168.10.13

  # Desktop-only setup when PCs are already provisioned:
  ./scripts/setup-desktop.sh \
      --hosts pc01:192.168.10.11,pc02:192.168.10.12 \
      --skip-distribute
EOF
}

log_info() {
    printf '[INFO] %s\n' "$*"
}

log_warn() {
    printf '[WARN] %s\n' "$*" >&2
}

log_err() {
    printf '[ERROR] %s\n' "$*" >&2
}

log_dry() {
    printf '[DRY-RUN] %s\n' "$*"
}

require_arg() {
    local opt="$1"
    local val="${2-}"
    if [[ -z "$val" || "$val" == --* ]]; then
        log_err "Option ${opt} requires a value."
        exit 2
    fi
}

expand_tilde() {
    local p="$1"
    if [[ "$p" == "~" ]]; then
        printf '%s' "$HOME"
    elif [[ "$p" == "~/"* ]]; then
        printf '%s/%s' "$HOME" "${p#\~/}"
    else
        printf '%s' "$p"
    fi
}

contract_tilde() {
    local p="$1"
    if [[ "$p" == "$HOME" ]]; then
        printf '~'
    elif [[ "$p" == "$HOME/"* ]]; then
        printf '~/%s' "${p#"$HOME/"}"
    else
        printf '%s' "$p"
    fi
}

json_escape() {
    local s="$1"
    s="${s//\\/\\\\}"
    s="${s//\"/\\\"}"
    s="${s//$'\n'/\\n}"
    s="${s//$'\r'/\\r}"
    s="${s//$'\t'/\\t}"
    printf '%s' "$s"
}

# --- Parse CLI arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --user)
            require_arg "$1" "${2-}"
            SSH_USER="$2"
            shift 2
            ;;
        --key-path)
            require_arg "$1" "${2-}"
            KEY_PATH_RAW="$2"
            shift 2
            ;;
        --known-hosts)
            require_arg "$1" "${2-}"
            KNOWN_HOSTS_RAW="$2"
            shift 2
            ;;
        --config-out)
            require_arg "$1" "${2-}"
            CONFIG_OUT_RAW="$2"
            shift 2
            ;;
        --hosts)
            require_arg "$1" "${2-}"
            HOSTS_SPEC="$2"
            shift 2
            ;;
        --timezone)
            require_arg "$1" "${2-}"
            TIMEZONE="$2"
            shift 2
            ;;
        --skip-timezone)
            SKIP_TIMEZONE=1
            shift
            ;;
        --skip-distribute)
            SKIP_DISTRIBUTE=1
            shift
            ;;
        --keep-password-auth)
            KEEP_PASSWORD_AUTH=1
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
            log_err "Unknown option: $1"
            usage >&2
            exit 2
            ;;
    esac
done

# --- Validate arguments ---
if [[ -z "$HOSTS_SPEC" ]]; then
    log_err "Missing required option: --hosts <id:host[:port],...>"
    usage >&2
    exit 2
fi

if [[ ! "$SSH_USER" =~ ^[a-z_][a-z0-9_-]*[$]?$ ]]; then
    log_err "Invalid SSH username: ${SSH_USER}"
    exit 2
fi

KEY_PATH="$(expand_tilde "$KEY_PATH_RAW")"
KNOWN_HOSTS="$(expand_tilde "$KNOWN_HOSTS_RAW")"
CONFIG_OUT="$(expand_tilde "$CONFIG_OUT_RAW")"

KEY_PATH_CONFIG="$(contract_tilde "$KEY_PATH")"
KNOWN_HOSTS_CONFIG="$(contract_tilde "$KNOWN_HOSTS")"

if [[ "$SKIP_TIMEZONE" -eq 0 ]]; then
    if [[ -z "$TIMEZONE" ]]; then
        if command -v timedatectl >/dev/null 2>&1; then
            TIMEZONE="$(timedatectl show -p Timezone --value 2>/dev/null || true)"
        fi
        if [[ -z "$TIMEZONE" ]]; then
            TIMEZONE="UTC"
        fi
    fi

    if [[ ! "$TIMEZONE" =~ ^[A-Za-z0-9._/+-]+$ ]]; then
        log_err "Invalid timezone: ${TIMEZONE}"
        exit 2
    fi
fi

# Parse --hosts into parallel arrays
PC_IDS=()
PC_USERS=()
PC_HOSTS=()
PC_PORTS=()

IFS=',' read -r -a HOST_ITEMS <<< "$HOSTS_SPEC"
for item in "${HOST_ITEMS[@]}"; do
    # Trim surrounding spaces
    item="${item#"${item%%[![:space:]]*}"}"
    item="${item%"${item##*[![:space:]]}"}"
    if [[ -z "$item" ]]; then
        continue
    fi

    IFS=':' read -r -a parts <<< "$item"
    if [[ "${#parts[@]}" -lt 2 || "${#parts[@]}" -gt 3 ]]; then
        log_err "Invalid host entry '${item}'. Expected format: id:[user@]host or id:[user@]host:port"
        exit 2
    fi

    pc_id="${parts[0]}"
    pc_user_host="${parts[1]}"
    pc_port="${parts[2]:-22}"

    if [[ "$pc_user_host" == *"@"* ]]; then
        pc_user="${pc_user_host%%@*}"
        pc_host="${pc_user_host#*@}"
    else
        pc_user="$SSH_USER"
        pc_host="$pc_user_host"
    fi

    if [[ ! "$pc_id" =~ ^[A-Za-z0-9._-]+$ ]]; then
        log_err "Invalid PC id '${pc_id}' in '${item}'."
        exit 2
    fi
    if [[ ! "$pc_user" =~ ^[a-z_][a-z0-9_-]*[$]?$ ]]; then
        log_err "Invalid SSH username '${pc_user}' in '${item}'."
        exit 2
    fi
    if [[ -z "$pc_host" || "$pc_host" =~ [[:space:]] ]]; then
        log_err "Invalid host '${pc_host}' in '${item}'."
        exit 2
    fi
    if [[ ! "$pc_port" =~ ^[0-9]+$ ]] || [[ "$pc_port" -lt 1 || "$pc_port" -gt 65535 ]]; then
        log_err "Invalid port '${pc_port}' in '${item}' (must be 1..65535)."
        exit 2
    fi

    PC_IDS+=("$pc_id")
    PC_USERS+=("$pc_user")
    PC_HOSTS+=("$pc_host")
    PC_PORTS+=("$pc_port")
done

if [[ "${#PC_IDS[@]}" -eq 0 ]]; then
    log_err "No valid hosts parsed from --hosts."
    exit 2
fi

# Check required local tools
for bin in ssh ssh-keygen ssh-keyscan ssh-agent ssh-add rsync; do
    if ! command -v "$bin" >/dev/null 2>&1; then
        if [[ "$DRY_RUN" -eq 1 ]]; then
            log_warn "Required command '${bin}' not found in PATH."
        else
            log_err "Required command '${bin}' not found. Install openssh-client and rsync."
            exit 1
        fi
    fi
done

if [[ "$DRY_RUN" -eq 1 ]]; then
    log_info "Running in --dry-run mode. No system changes or network connections will be made."
fi

# --- Step 1: Generate passphrase-protected ed25519 key pair ---
log_info "Step 1/5: Checking ed25519 collector key (${KEY_PATH})..."
KEY_DIR="$(dirname "$KEY_PATH")"

if [[ -f "$KEY_PATH" && -f "${KEY_PATH}.pub" ]]; then
    log_info "Collector key already exists at ${KEY_PATH} (reusing existing key)."
else
    if [[ -f "$KEY_PATH" && ! -f "${KEY_PATH}.pub" ]]; then
        log_err "Private key ${KEY_PATH} exists but public key ${KEY_PATH}.pub is missing."
        exit 1
    fi
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log_dry "Would ensure directory ${KEY_DIR} exists (mode 0700)"
        log_dry "Would run: ssh-keygen -t ed25519 -a 64 -C \"datacollector@$(hostname -s)\" -f $(printf '%q' "$KEY_PATH")"
        log_dry "  (Operator will be prompted interactively for a strong passphrase.)"
        log_dry "Would set permissions: chmod 0600 $(printf '%q' "$KEY_PATH") && chmod 0644 $(printf '%q' "${KEY_PATH}.pub")"
    else
        install -d -m 0700 "$KEY_DIR"
        log_info "Generating new ed25519 key pair. Enter a strong passphrase when prompted (do NOT leave empty)."
        ssh-keygen -t ed25519 -a 64 -C "datacollector@$(hostname -s)" -f "$KEY_PATH"
        chmod 0600 "$KEY_PATH"
        chmod 0644 "${KEY_PATH}.pub"
    fi
fi

# --- Step 2: Fetch and interactively verify SSH host keys ---
log_info "Step 2/5: Verifying SSH host keys into ${KNOWN_HOSTS}..."
KNOWN_HOSTS_DIR="$(dirname "$KNOWN_HOSTS")"

if [[ "$DRY_RUN" -eq 1 ]]; then
    log_dry "Would ensure directory ${KNOWN_HOSTS_DIR} exists (mode 0700)"
    log_dry "Would ensure file ${KNOWN_HOSTS} exists (mode 0600)"
    for i in "${!PC_IDS[@]}"; do
        pc_id="${PC_IDS[$i]}"
        pc_host="${PC_HOSTS[$i]}"
        pc_port="${PC_PORTS[$i]}"
        if [[ "$pc_port" -eq 22 ]]; then
            kh_target="$pc_host"
        else
            kh_target="[${pc_host}]:${pc_port}"
        fi
        log_dry "Host ${pc_id} (${pc_host}:${pc_port}):"
        log_dry "  Would check if '${kh_target}' is already in ${KNOWN_HOSTS} via ssh-keygen -F"
        log_dry "  If missing, would run: ssh-keyscan -T 5 -p ${pc_port} -t ed25519,ecdsa,rsa ${pc_host}"
        log_dry "  Would display SHA256 fingerprint(s) via ssh-keygen -lf - -E sha256"
        log_dry "  Would prompt operator interactively [y/N] before appending to ${KNOWN_HOSTS}"
    done
else
    install -d -m 0700 "$KNOWN_HOSTS_DIR"
    if [[ ! -f "$KNOWN_HOSTS" ]]; then
        install -m 0600 /dev/null "$KNOWN_HOSTS"
    else
        chmod 0600 "$KNOWN_HOSTS"
    fi

    for i in "${!PC_IDS[@]}"; do
        pc_id="${PC_IDS[$i]}"
        pc_host="${PC_HOSTS[$i]}"
        pc_port="${PC_PORTS[$i]}"
        if [[ "$pc_port" -eq 22 ]]; then
            kh_target="$pc_host"
        else
            kh_target="[${pc_host}]:${pc_port}"
        fi

        if ssh-keygen -F "$kh_target" -f "$KNOWN_HOSTS" >/dev/null 2>&1; then
            log_info "Host key for ${pc_id} (${kh_target}) is already pinned in ${KNOWN_HOSTS}."
            continue
        fi

        log_info "Scanning SSH host key(s) for ${pc_id} (${pc_host}:${pc_port})..."
        TMP_SCAN="$(mktemp)"
        if ! ssh-keyscan -T 5 -p "$pc_port" -t ed25519,ecdsa,rsa "$pc_host" > "$TMP_SCAN" 2>/dev/null || [[ ! -s "$TMP_SCAN" ]]; then
            rm -f "$TMP_SCAN"
            log_err "Failed to fetch SSH host key from ${pc_id} (${pc_host}:${pc_port}). Check power, network, and sshd."
            exit 1
        fi

        printf '\n--- SSH Host Key Fingerprints for %s (%s:%s) ---\n' "$pc_id" "$pc_host" "$pc_port"
        ssh-keygen -lf "$TMP_SCAN" -E sha256
        printf '%s\n' '----------------------------------------------------------'

        read -r -p "Verify the SHA256 fingerprint(s) above. Trust and add ${pc_id} (${pc_host}:${pc_port})? [y/N]: " reply
        if [[ "$reply" =~ ^[Yy]([Ee][Ss])?$ ]]; then
            cat "$TMP_SCAN" >> "$KNOWN_HOSTS"
            chmod 0600 "$KNOWN_HOSTS"
            log_info "Added host key(s) for ${pc_id} to ${KNOWN_HOSTS}."
        else
            rm -f "$TMP_SCAN"
            log_err "Operator rejected host key for ${pc_id} (${pc_host}:${pc_port}). Aborting setup."
            exit 1
        fi
        rm -f "$TMP_SCAN"
    done
fi

# --- Step 3: Optionally distribute key and run provision-pc.sh on each PC ---
if [[ "$SKIP_DISTRIBUTE" -eq 1 ]]; then
    log_info "Step 3/5: Skipping remote PC provisioning (--skip-distribute)."
else
    if [[ "$SKIP_TIMEZONE" -eq 1 ]]; then
        log_info "Step 3/5: Provisioning remote PCs via provision-pc.sh (timezone=unchanged)..."
        PROVISION_TZ_ARGS="--skip-timezone"
        PROVISION_TZ_DISPLAY="--skip-timezone"
    else
        log_info "Step 3/5: Provisioning remote PCs via provision-pc.sh (timezone=${TIMEZONE})..."
        PROVISION_TZ_ARGS="--timezone '${TIMEZONE}'"
        PROVISION_TZ_DISPLAY="--timezone ${TIMEZONE}"
    fi
    PROVISION_SCRIPT="${SCRIPT_DIR}/provision-pc.sh"
    if [[ ! -f "$PROVISION_SCRIPT" ]]; then
        log_err "Provisioning script not found at ${PROVISION_SCRIPT}."
        exit 1
    fi

    PROVISION_AUTH_FLAG="--disable-password-auth"
    if [[ "$KEEP_PASSWORD_AUTH" -eq 1 ]]; then
        PROVISION_AUTH_FLAG="--keep-password-auth"
    fi

    if [[ "$DRY_RUN" -eq 1 ]]; then
        for i in "${!PC_IDS[@]}"; do
            pc_id="${PC_IDS[$i]}"
            pc_user="${PC_USERS[$i]}"
            pc_host="${PC_HOSTS[$i]}"
            pc_port="${PC_PORTS[$i]}"
            log_dry "Host ${pc_id} (${pc_user}@${pc_host}:${pc_port}):"
            log_dry "  Would copy ${PROVISION_SCRIPT} via scp -P ${pc_port} -o StrictHostKeyChecking=yes -o UserKnownHostsFile=${KNOWN_HOSTS}"
            log_dry "  Would run remotely via ssh -t -p ${pc_port} -o StrictHostKeyChecking=yes -o UserKnownHostsFile=${KNOWN_HOSTS} ${pc_user}@${pc_host}:"
            log_dry "    sudo bash /tmp/provision-pc.sh --user ${pc_user} --pubkey <contents of ${KEY_PATH}.pub> ${PROVISION_TZ_DISPLAY} ${PROVISION_AUTH_FLAG}"
        done
    else
        for i in "${!PC_IDS[@]}"; do
            pc_id="${PC_IDS[$i]}"
            pc_user="${PC_USERS[$i]}"
            pc_host="${PC_HOSTS[$i]}"
            pc_port="${PC_PORTS[$i]}"
            log_info "Provisioning ${pc_id} (${pc_user}@${pc_host}:${pc_port})... (enter password / sudo password if prompted)"

            REMOTE_TMP_SCRIPT="/tmp/datacollector-provision-pc-$$.sh"
            REMOTE_TMP_PUB="/tmp/datacollector-pubkey-$$.pub"
            CTRL_DIR="$(mktemp -d)"
            chmod 0700 "$CTRL_DIR"
            CTRL_SOCK="${CTRL_DIR}/ctrl.sock"

            scp -P "$pc_port" \
                -o StrictHostKeyChecking=yes \
                -o UserKnownHostsFile="$KNOWN_HOSTS" \
                -o ConnectTimeout=10 \
                -o ControlMaster=auto \
                -o ControlPath="$CTRL_SOCK" \
                -o ControlPersist=30 \
                "$PROVISION_SCRIPT" "${pc_user}@${pc_host}:${REMOTE_TMP_SCRIPT}"

            scp -P "$pc_port" \
                -o StrictHostKeyChecking=yes \
                -o UserKnownHostsFile="$KNOWN_HOSTS" \
                -o ConnectTimeout=10 \
                -o ControlMaster=auto \
                -o ControlPath="$CTRL_SOCK" \
                -o ControlPersist=30 \
                "${KEY_PATH}.pub" "${pc_user}@${pc_host}:${REMOTE_TMP_PUB}"

            set +e
            ssh -t -p "$pc_port" \
                -o StrictHostKeyChecking=yes \
                -o UserKnownHostsFile="$KNOWN_HOSTS" \
                -o ConnectTimeout=10 \
                -o ControlMaster=auto \
                -o ControlPath="$CTRL_SOCK" \
                "${pc_user}@${pc_host}" \
                "sudo bash '${REMOTE_TMP_SCRIPT}' --user '${pc_user}' --pubkey '${REMOTE_TMP_PUB}' ${PROVISION_TZ_ARGS} '${PROVISION_AUTH_FLAG}'; rc=\$?; rm -f '${REMOTE_TMP_SCRIPT}' '${REMOTE_TMP_PUB}'; exit \$rc"
            SSH_RC=$?
            set -e

            ssh -O exit -o ControlPath="$CTRL_SOCK" "${pc_user}@${pc_host}" 2>/dev/null || true
            rm -rf "$CTRL_DIR"

            if [[ "$SSH_RC" -ne 0 ]]; then
                log_err "Remote provisioning failed on ${pc_id} (${pc_user}@${pc_host}:${pc_port}) with exit code ${SSH_RC}."
                exit "$SSH_RC"
            fi
        done
    fi
fi

# --- Step 4: Write or merge config.json ---
log_info "Step 4/5: Generating configuration file (${CONFIG_OUT})..."
CONFIG_DIR="$(dirname "$CONFIG_OUT")"

# Build JSON payload matching config.example.json schema
JSON_PCS=""
for i in "${!PC_IDS[@]}"; do
    pc_id_esc="$(json_escape "${PC_IDS[$i]}")"
    pc_user_val="${PC_USERS[$i]}"
    pc_user_esc="$(json_escape "$pc_user_val")"
    pc_host_esc="$(json_escape "${PC_HOSTS[$i]}")"
    pc_port="${PC_PORTS[$i]}"

    entry="    { \"id\": \"${pc_id_esc}\", \"host\": \"${pc_host_esc}\", \"sshUser\": \"${pc_user_esc}\""
    if [[ "$pc_port" -ne 22 ]]; then
        entry+=", \"port\": ${pc_port}"
    fi
    entry+=" }"

    if [[ "$i" -lt $((${#PC_IDS[@]} - 1)) ]]; then
        JSON_PCS+="${entry},"$'\n'
    else
        JSON_PCS+="${entry}"$'\n'
    fi
done

FRESH_CONFIG_JSON="{
  \"sshUser\": \"$(json_escape "$SSH_USER")\",
  \"sshKeyPath\": \"$(json_escape "$KEY_PATH_CONFIG")\",
  \"knownHostsPath\": \"$(json_escape "$KNOWN_HOSTS_CONFIG")\",
  \"maxParallel\": 2,
  \"paths\": {
    \"logs\": \".app\",
    \"screenshots\": \"Pictures/lightshot\",
    \"recordings\": \"recordings\"
  },
  \"logs\": {
    \"includeModifiedAfterEnd\": true
  },
  \"ui\": {
    \"theme\": \"light\"
  },
  \"pcs\": [
${JSON_PCS}  ]
}
"

if command -v python3 >/dev/null 2>&1; then
    CONFIG_JSON="$(
        FRESH_JSON="$FRESH_CONFIG_JSON" \
        EXISTING_PATH="$CONFIG_OUT" \
        python3 - <<'PY'
import json
import os
import sys

fresh = json.loads(os.environ["FRESH_JSON"])
existing_path = os.environ["EXISTING_PATH"]

if os.path.isfile(existing_path):
    try:
        with open(existing_path, "r", encoding="utf-8") as fh:
            cfg = json.load(fh)
        if not isinstance(cfg, dict):
            raise ValueError("root is not an object")
    except Exception as exc:
        print(f"[ERROR] Existing config {existing_path} is not valid JSON: {exc}", file=sys.stderr)
        sys.exit(1)

    cfg.setdefault("sshUser", fresh["sshUser"])
    cfg["sshKeyPath"] = fresh["sshKeyPath"]
    cfg["knownHostsPath"] = fresh["knownHostsPath"]
    cfg.setdefault("maxParallel", fresh["maxParallel"])
    cfg.setdefault("paths", fresh["paths"])
    cfg.setdefault("logs", fresh["logs"])
    cfg.setdefault("ui", fresh["ui"])

    existing_pcs = cfg.get("pcs")
    if not isinstance(existing_pcs, list):
        existing_pcs = []

    default_user = cfg.get("sshUser", fresh["sshUser"])
    by_id = {p.get("id"): idx for idx, p in enumerate(existing_pcs) if isinstance(p, dict) and "id" in p}

    for new_pc in fresh["pcs"]:
        pc_id = new_pc["id"]
        pc_user = new_pc.pop("sshUser", default_user)
        if pc_id in by_id:
            target = existing_pcs[by_id[pc_id]]
            target["host"] = new_pc["host"]
            if "port" in new_pc:
                target["port"] = new_pc["port"]
            else:
                target.pop("port", None)
            if pc_user != default_user:
                target["sshUser"] = pc_user
            else:
                target.pop("sshUser", None)
        else:
            entry = {"id": pc_id, "host": new_pc["host"]}
            if pc_user != default_user:
                entry["sshUser"] = pc_user
            if "port" in new_pc:
                entry["port"] = new_pc["port"]
            existing_pcs.append(entry)
            by_id[pc_id] = len(existing_pcs) - 1

    cfg["pcs"] = existing_pcs
else:
    cfg = fresh
    default_user = cfg["sshUser"]
    for pc in cfg["pcs"]:
        if pc.get("sshUser") == default_user:
            pc.pop("sshUser", None)

print(json.dumps(cfg, indent=2))
PY
    )"$'\n'
    if [[ -f "$CONFIG_OUT" ]]; then
        log_info "Existing ${CONFIG_OUT} detected — merging ${#PC_IDS[@]} PC(s) while preserving existing entries."
    fi
else
    if [[ -f "$CONFIG_OUT" ]]; then
        log_warn "python3 not found; overwriting existing ${CONFIG_OUT} instead of merging."
    fi
    CONFIG_JSON="$FRESH_CONFIG_JSON"
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
    log_dry "Would ensure directory ${CONFIG_DIR} exists (mode 0755)"
    log_dry "Would write ${CONFIG_OUT} (mode 0644) with content:"
    while IFS= read -r line; do
        log_dry "  ${line}"
    done <<< "${CONFIG_JSON%$'\n'}"
else
    install -d -m 0755 "$CONFIG_DIR"
    TMP_CFG="$(mktemp "${CONFIG_DIR}/.config.json.XXXXXX")"
    printf '%s' "$CONFIG_JSON" > "$TMP_CFG"
    chmod 0644 "$TMP_CFG"
    mv -f "$TMP_CFG" "$CONFIG_OUT"
    log_info "Wrote ${CONFIG_OUT}."
fi

# --- Step 5: Verify read-only probe against each PC ---
log_info "Step 5/5: Verifying read-only rsync probe against each PC..."

if [[ "$DRY_RUN" -eq 1 ]]; then
    log_dry "Would start a temporary ssh-agent and run: ssh-add $(printf '%q' "$KEY_PATH") (one passphrase prompt)"
    for i in "${!PC_IDS[@]}"; do
        pc_id="${PC_IDS[$i]}"
        pc_user="${PC_USERS[$i]}"
        pc_host="${PC_HOSTS[$i]}"
        pc_port="${PC_PORTS[$i]}"
        SSH_OPTS="ssh -p ${pc_port} -i ${KEY_PATH} -o BatchMode=yes -o ConnectTimeout=5 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 -o StrictHostKeyChecking=yes -o UserKnownHostsFile=${KNOWN_HOSTS} -o IdentitiesOnly=yes -o IdentityAgent=<temp-agent-sock>"
        log_dry "Would probe ${pc_id}: rsync -d --list-only -e \"${SSH_OPTS}\" ${pc_user}@${pc_host}:./"
    done
    log_dry "Would stop temporary ssh-agent on exit."
    log_info "Dry-run complete. No changes were applied."
    exit 0
fi

# Start a private temporary ssh-agent so the operator enters the passphrase once
TEMP_AGENT_DIR="$(mktemp -d)"
chmod 0700 "$TEMP_AGENT_DIR"
TEMP_AGENT_SOCK="${TEMP_AGENT_DIR}/agent.sock"
eval "$(ssh-agent -a "$TEMP_AGENT_SOCK" -s)" >/dev/null
TEMP_AGENT_PID="$SSH_AGENT_PID"

log_info "Unlocking collector key (${KEY_PATH}) in temporary ssh-agent for probe verification..."
SSH_AUTH_SOCK="$TEMP_AGENT_SOCK" ssh-add "$KEY_PATH"

PROBE_FAILURES=0
for i in "${!PC_IDS[@]}"; do
    pc_id="${PC_IDS[$i]}"
    pc_user="${PC_USERS[$i]}"
    pc_host="${PC_HOSTS[$i]}"
    pc_port="${PC_PORTS[$i]}"

    SSH_CMD="ssh -p ${pc_port} -i ${KEY_PATH} -o BatchMode=yes -o ConnectTimeout=5 -o ServerAliveInterval=10 -o ServerAliveCountMax=3 -o StrictHostKeyChecking=yes -o UserKnownHostsFile=${KNOWN_HOSTS} -o IdentitiesOnly=yes -o IdentityAgent=${TEMP_AGENT_SOCK}"

    if rsync -d --list-only -e "$SSH_CMD" "${pc_user}@${pc_host}:./" >/dev/null 2>&1; then
        log_info "[OK]   ${pc_id} (${pc_user}@${pc_host}:${pc_port}) — read-only rsync probe succeeded."
    else
        log_err "[FAIL] ${pc_id} (${pc_user}@${pc_host}:${pc_port}) — read-only rsync probe failed."
        PROBE_FAILURES=$((PROBE_FAILURES + 1))
    fi
done

if [[ "$PROBE_FAILURES" -gt 0 ]]; then
    log_warn "Setup finished with ${PROBE_FAILURES} probe failure(s). Check network, authorized_keys, and rrsync on failed PCs."
    exit 1
fi

log_info "Desktop setup complete. All ${#PC_IDS[@]} PC(s) passed read-only probe verification."
