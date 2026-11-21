#!/usr/bin/env bash
# provision-pc.sh — Provision a headless Ubuntu PC for read-only Data Collector access.
#
# Configures OpenSSH, rsync + rrsync, a restricted authorized_keys entry
# (command="<rrsync> -ro /home/<user>",restrict), sshd hardening, and timezone.
# (Does not install or enable systemd-timesyncd/NTP, so PTP time sync is undisturbed.)
#
# Idempotent and safe. Use --dry-run to preview all changes without modifying the system.

set -euo pipefail

SSH_USER="user"
PUBKEY_INPUT=""
TIMEZONE="UTC"
DISABLE_PASSWORD_AUTH=1
DRY_RUN=0

SSHD_DROPIN="/etc/ssh/sshd_config.d/99-datacollector.conf"

usage() {
    cat <<'EOF'
Usage: provision-pc.sh --pubkey <path_or_string> [OPTIONS]

Configure a headless Ubuntu PC for read-only Data Collector access via SSH + rrsync.

Required:
  --pubkey <path_or_string>   Path to an OpenSSH public key file (e.g. collector_key.pub)
                              or the raw public key string.

Options:
  --user <ssh_user>           Target Linux user account on this PC (default: user)
  --timezone <tz>             IANA timezone to configure, e.g. UTC or Asia/Ho_Chi_Minh
                              (default: UTC). Must match the desktop timezone.
  --disable-password-auth     Disable SSH password and keyboard-interactive auth
                              (default: enabled).
  --keep-password-auth        Keep SSH password auth enabled for staged rollout until
                              key-based collection is verified.
  --dry-run                   Print every change/command without modifying the system.
  -h, --help                  Show this help message and exit.

Examples:
  # Preview all changes without modifying the system:
  sudo ./scripts/provision-pc.sh --user user --pubkey /tmp/collector_key.pub \
      --timezone Asia/Ho_Chi_Minh --dry-run

  # Staged rollout (keep password auth until key login is tested):
  sudo ./scripts/provision-pc.sh --user user --pubkey "ssh-ed25519 AAAA... datacollector" \
      --timezone Asia/Ho_Chi_Minh --keep-password-auth

  # Finalize hardening (disable password auth):
  sudo ./scripts/provision-pc.sh --user user --pubkey /tmp/collector_key.pub \
      --timezone Asia/Ho_Chi_Minh --disable-password-auth
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

run_cmd() {
    if [[ "$DRY_RUN" -eq 1 ]]; then
        printf '[DRY-RUN] Would run:'
        printf ' %q' "$@"
        printf '\n'
    else
        "$@"
    fi
}

require_arg() {
    local opt="$1"
    local val="${2-}"
    if [[ -z "$val" || "$val" == --* ]]; then
        log_err "Option ${opt} requires a value."
        exit 2
    fi
}

# --- Parse CLI arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --user)
            require_arg "$1" "${2-}"
            SSH_USER="$2"
            shift 2
            ;;
        --pubkey)
            require_arg "$1" "${2-}"
            PUBKEY_INPUT="$2"
            shift 2
            ;;
        --timezone)
            require_arg "$1" "${2-}"
            TIMEZONE="$2"
            shift 2
            ;;
        --disable-password-auth)
            DISABLE_PASSWORD_AUTH=1
            shift
            ;;
        --keep-password-auth)
            DISABLE_PASSWORD_AUTH=0
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
if [[ -z "$PUBKEY_INPUT" ]]; then
    log_err "Missing required option: --pubkey <path_or_string>"
    usage >&2
    exit 2
fi

if [[ ! "$SSH_USER" =~ ^[a-z_][a-z0-9_-]*[$]?$ ]]; then
    log_err "Invalid Linux username: ${SSH_USER}"
    exit 2
fi

if [[ -f "$PUBKEY_INPUT" ]]; then
    if [[ ! -r "$PUBKEY_INPUT" ]]; then
        log_err "Public key file is not readable: ${PUBKEY_INPUT}"
        exit 1
    fi
    PUBKEY_LINE="$(grep -vE '^[[:space:]]*(#|$)' "$PUBKEY_INPUT" | head -n 1 | tr -d '\r')"
else
    PUBKEY_LINE="$(printf '%s' "$PUBKEY_INPUT" | tr -d '\r')"
fi

if [[ -z "$PUBKEY_LINE" ]]; then
    log_err "Public key is empty."
    exit 2
fi

if [[ "$PUBKEY_LINE" == *$'\n'* ]]; then
    log_err "Public key must be a single line."
    exit 2
fi

# Extract key type and base64 key blob for validation & idempotent matching
read -r PUBKEY_TYPE PUBKEY_BLOB PUBKEY_REST <<<"$PUBKEY_LINE"
if [[ ! "$PUBKEY_TYPE" =~ ^(ssh-ed25519|ssh-rsa|ecdsa-sha2-nistp(256|384|521)|sk-ssh-ed25519@openssh\.com|sk-ecdsa-sha2-nistp256@openssh\.com)$ ]] || [[ -z "$PUBKEY_BLOB" ]]; then
    log_err "Invalid OpenSSH public key format (expected e.g. 'ssh-ed25519 AAAA... [comment]')."
    exit 2
fi
# Normalize whitespace in the key line (preserve optional comment)
if [[ -n "${PUBKEY_REST:-}" ]]; then
    PUBKEY_NORMALIZED="${PUBKEY_TYPE} ${PUBKEY_BLOB} ${PUBKEY_REST}"
else
    PUBKEY_NORMALIZED="${PUBKEY_TYPE} ${PUBKEY_BLOB}"
fi
KEY_FINGERPRINT_MATCH="${PUBKEY_TYPE} ${PUBKEY_BLOB}"

if [[ -d /usr/share/zoneinfo ]] && [[ ! -f "/usr/share/zoneinfo/${TIMEZONE}" ]]; then
    log_err "Invalid timezone '${TIMEZONE}': /usr/share/zoneinfo/${TIMEZONE} not found."
    exit 2
fi

if [[ "$DRY_RUN" -eq 0 && "$EUID" -ne 0 ]]; then
    log_err "This script must be run as root (e.g. with sudo). Use --dry-run to preview without root."
    exit 1
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
    log_info "Running in --dry-run mode. No system changes will be made."
fi

# Resolve user home directory and primary group
if getent passwd "$SSH_USER" >/dev/null 2>&1; then
    USER_HOME="$(getent passwd "$SSH_USER" | cut -d: -f6)"
    SSH_GROUP="$(id -gn "$SSH_USER")"
else
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log_warn "User '${SSH_USER}' does not exist on this host (dry-run will assume /home/${SSH_USER})."
        USER_HOME="/home/${SSH_USER}"
        SSH_GROUP="${SSH_USER}"
    else
        log_err "User '${SSH_USER}' does not exist on this PC. Create the user first."
        exit 1
    fi
fi

# --- Step 1: Install required packages ---
log_info "Step 1/5: Checking required packages (openssh-server, rsync)..."
MISSING_PKGS=()
for pkg in openssh-server rsync; do
    if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q '^install ok installed$'; then
        MISSING_PKGS+=("$pkg")
    fi
done

if [[ "${#MISSING_PKGS[@]}" -gt 0 ]]; then
    log_info "Packages to install: ${MISSING_PKGS[*]}"
    run_cmd apt-get update -qq
    run_cmd env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${MISSING_PKGS[@]}"
else
    log_info "All required packages are already installed."
fi

# --- Step 2: Ensure rrsync is available ---
log_info "Step 2/5: Checking rrsync availability..."
RRSYNC_BIN=""
if [[ -x /usr/bin/rrsync ]]; then
    RRSYNC_BIN="/usr/bin/rrsync"
    log_info "Found rrsync at ${RRSYNC_BIN}."
elif [[ -x /usr/local/bin/rrsync ]]; then
    RRSYNC_BIN="/usr/local/bin/rrsync"
    log_info "Found rrsync at ${RRSYNC_BIN}."
elif [[ -f /usr/share/doc/rsync/scripts/rrsync ]]; then
    RRSYNC_BIN="/usr/local/bin/rrsync"
    log_info "Installing rrsync from /usr/share/doc/rsync/scripts/rrsync to ${RRSYNC_BIN}..."
    run_cmd install -m 0755 /usr/share/doc/rsync/scripts/rrsync "$RRSYNC_BIN"
elif [[ -f /usr/share/doc/rsync/scripts/rrsync.gz ]]; then
    RRSYNC_BIN="/usr/local/bin/rrsync"
    log_info "Extracting rrsync from /usr/share/doc/rsync/scripts/rrsync.gz to ${RRSYNC_BIN}..."
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log_dry "Would extract /usr/share/doc/rsync/scripts/rrsync.gz -> ${RRSYNC_BIN} (mode 0755)"
    else
        gzip -dc /usr/share/doc/rsync/scripts/rrsync.gz > "${RRSYNC_BIN}.tmp"
        chmod 0755 "${RRSYNC_BIN}.tmp"
        mv -f "${RRSYNC_BIN}.tmp" "$RRSYNC_BIN"
    fi
else
    if [[ "$DRY_RUN" -eq 1 ]]; then
        RRSYNC_BIN="/usr/bin/rrsync"
        log_warn "rrsync not found yet; assuming ${RRSYNC_BIN} will be provided after rsync installation."
    else
        log_err "rrsync not found at /usr/bin/rrsync or /usr/share/doc/rsync/scripts/rrsync(.gz)."
        exit 1
    fi
fi

# --- Step 3: Configure restricted authorized_keys ---
log_info "Step 3/5: Configuring restricted authorized_keys for user '${SSH_USER}'..."
SSH_DIR="${USER_HOME}/.ssh"
AUTH_KEYS="${SSH_DIR}/authorized_keys"
RESTRICTED_LINE="command=\"${RRSYNC_BIN} -ro ${USER_HOME}\",restrict ${PUBKEY_NORMALIZED}"

if [[ ! -d "$SSH_DIR" ]]; then
    run_cmd install -d -m 0700 -o "$SSH_USER" -g "$SSH_GROUP" "$SSH_DIR"
else
    run_cmd chmod 0700 "$SSH_DIR"
    run_cmd chown "${SSH_USER}:${SSH_GROUP}" "$SSH_DIR"
fi

if [[ -f "$AUTH_KEYS" && -r "$AUTH_KEYS" ]]; then
    EXACT_FOUND=0
    KEY_FOUND=0
    while IFS= read -r existing_line || [[ -n "$existing_line" ]]; do
        if [[ "$existing_line" == "$RESTRICTED_LINE" ]]; then
            EXACT_FOUND=1
            break
        fi
        if [[ "$existing_line" == *"$KEY_FINGERPRINT_MATCH"* ]]; then
            KEY_FOUND=1
        fi
    done < "$AUTH_KEYS"

    if [[ "$EXACT_FOUND" -eq 1 ]]; then
        log_info "Restricted authorized_keys entry is already present (no change)."
    elif [[ "$KEY_FOUND" -eq 1 ]]; then
        log_info "Updating existing authorized_keys entry for this key to enforce rrsync -ro restriction..."
        if [[ "$DRY_RUN" -eq 1 ]]; then
            log_dry "Would replace existing line matching '${PUBKEY_TYPE} ${PUBKEY_BLOB:0:16}...' in ${AUTH_KEYS} with:"
            log_dry "  ${RESTRICTED_LINE}"
        else
            TMP_KEYS="$(mktemp "${SSH_DIR}/.authorized_keys.XXXXXX")"
            REPLACED=0
            while IFS= read -r existing_line || [[ -n "$existing_line" ]]; do
                if [[ "$existing_line" == *"$KEY_FINGERPRINT_MATCH"* ]]; then
                    if [[ "$REPLACED" -eq 0 ]]; then
                        printf '%s\n' "$RESTRICTED_LINE" >> "$TMP_KEYS"
                        REPLACED=1
                    fi
                else
                    printf '%s\n' "$existing_line" >> "$TMP_KEYS"
                fi
            done < "$AUTH_KEYS"
            chmod 0600 "$TMP_KEYS"
            chown "${SSH_USER}:${SSH_GROUP}" "$TMP_KEYS"
            mv -f "$TMP_KEYS" "$AUTH_KEYS"
        fi
    else
        log_info "Appending restricted authorized_keys entry to ${AUTH_KEYS}..."
        if [[ "$DRY_RUN" -eq 1 ]]; then
            log_dry "Would append to ${AUTH_KEYS} (mode 0600, owner ${SSH_USER}:${SSH_GROUP}):"
            log_dry "  ${RESTRICTED_LINE}"
        else
            printf '%s\n' "$RESTRICTED_LINE" >> "$AUTH_KEYS"
            chmod 0600 "$AUTH_KEYS"
            chown "${SSH_USER}:${SSH_GROUP}" "$AUTH_KEYS"
        fi
    fi
else
    log_info "Creating ${AUTH_KEYS} with restricted key entry..."
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log_dry "Would create ${AUTH_KEYS} (mode 0600, owner ${SSH_USER}:${SSH_GROUP}) containing:"
        log_dry "  ${RESTRICTED_LINE}"
    else
        TMP_KEYS="$(mktemp "${SSH_DIR}/.authorized_keys.XXXXXX")"
        printf '%s\n' "$RESTRICTED_LINE" > "$TMP_KEYS"
        chmod 0600 "$TMP_KEYS"
        chown "${SSH_USER}:${SSH_GROUP}" "$TMP_KEYS"
        mv -f "$TMP_KEYS" "$AUTH_KEYS"
    fi
fi

# --- Step 4: Configure sshd drop-in ---
log_info "Step 4/5: Configuring SSH daemon (${SSHD_DROPIN})..."

if [[ "$DISABLE_PASSWORD_AUTH" -eq 1 ]]; then
    log_warn "Password authentication will be DISABLED on this PC. Ensure you have verified key access or local console access before disconnecting."
    DESIRED_SSHD_CONF="# Managed by Data Collector provision-pc.sh
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
"
else
    log_warn "Staged rollout (--keep-password-auth): Password authentication remains enabled. Re-run with --disable-password-auth after verifying collection works."
    DESIRED_SSHD_CONF="# Managed by Data Collector provision-pc.sh (staged rollout: password auth kept enabled)
PubkeyAuthentication yes
"
fi

SSHD_CHANGED=1
if [[ -f "$SSHD_DROPIN" && -r "$SSHD_DROPIN" ]]; then
    CURRENT_SSHD_CONF="$(cat "$SSHD_DROPIN"; printf 'x')"
    if [[ "${CURRENT_SSHD_CONF%x}" == "$DESIRED_SSHD_CONF" ]]; then
        SSHD_CHANGED=0
        log_info "${SSHD_DROPIN} is already up to date (no change)."
    fi
fi

if [[ "$SSHD_CHANGED" -eq 1 ]]; then
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log_dry "Would ensure directory /etc/ssh/sshd_config.d exists (mode 0755)"
        log_dry "Would write ${SSHD_DROPIN} (mode 0644) with content:"
        while IFS= read -r line; do
            log_dry "  ${line}"
        done <<< "${DESIRED_SSHD_CONF%$'\n'}"
        log_dry "Would validate sshd configuration: sshd -t"
        log_dry "Would enable and reload ssh service: systemctl enable ssh && systemctl reload ssh"
    else
        install -d -m 0755 /etc/ssh/sshd_config.d
        BACKUP_CONF=""
        if [[ -f "$SSHD_DROPIN" ]]; then
            BACKUP_CONF="$(mktemp /tmp/99-datacollector.conf.bak.XXXXXX)"
            cp -p "$SSHD_DROPIN" "$BACKUP_CONF"
        fi

        TMP_CONF="$(mktemp /etc/ssh/sshd_config.d/.99-datacollector.conf.XXXXXX)"
        printf '%s' "$DESIRED_SSHD_CONF" > "$TMP_CONF"
        chmod 0644 "$TMP_CONF"
        mv -f "$TMP_CONF" "$SSHD_DROPIN"

        if ! sshd -t; then
            log_err "sshd -t validation failed! Restoring previous configuration."
            if [[ -n "$BACKUP_CONF" ]]; then
                mv -f "$BACKUP_CONF" "$SSHD_DROPIN"
            else
                rm -f "$SSHD_DROPIN"
            fi
            exit 1
        fi
        [[ -n "$BACKUP_CONF" ]] && rm -f "$BACKUP_CONF"

        systemctl enable ssh >/dev/null 2>&1 || true
        if systemctl is-active --quiet ssh; then
            systemctl reload ssh
        else
            systemctl start ssh
        fi
        log_info "Validated and reloaded sshd."
    fi
else
    if [[ "$DRY_RUN" -eq 1 ]]; then
        log_dry "Would verify sshd config (sshd -t) and ensure ssh service is enabled/active."
    else
        sshd -t
        systemctl enable --now ssh >/dev/null 2>&1 || true
    fi
fi

# --- Step 5: Configure timezone ---
log_info "Step 5/5: Configuring timezone (${TIMEZONE}) (leaving PTP time sync untouched)..."

CURRENT_TZ=""
if command -v timedatectl >/dev/null 2>&1; then
    CURRENT_TZ="$(timedatectl show -p Timezone --value 2>/dev/null || true)"
fi

if [[ "$CURRENT_TZ" == "$TIMEZONE" ]]; then
    log_info "Timezone is already set to ${TIMEZONE} (no change)."
else
    run_cmd timedatectl set-timezone "$TIMEZONE"
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
    log_info "Dry-run complete. No changes were applied."
else
    log_info "Provisioning complete for user '${SSH_USER}' (timezone=${TIMEZONE}, password_auth_disabled=${DISABLE_PASSWORD_AUTH})."
fi
