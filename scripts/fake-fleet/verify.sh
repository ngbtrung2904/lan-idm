#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
STATE_DIR="${DC_FAKE_FLEET_DIR:-/tmp/dc-fake-fleet}"
PASSPHRASE="${DC_FAKE_FLEET_PASSPHRASE:-testpass123}"
CURRENT_USER="$(id -un)"

WORK_TMP="$(mktemp -d /tmp/dc-fake-fleet-verify.XXXXXX)"
AGENT_PID=""
AGENT_SOCK="${WORK_TMP}/agent.sock"

cleanup() {
    local rc=$?
    if [[ -n "$AGENT_PID" ]] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill -TERM "$AGENT_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK_TMP" 2>/dev/null || true
    DC_FAKE_FLEET_DIR="$STATE_DIR" "${SCRIPT_DIR}/down.sh" >/dev/null 2>&1 || true
    exit "$rc"
}
trap cleanup EXIT INT TERM

pass() {
    echo "[PASS] $1"
}

fail() {
    echo "[FAIL] $1" >&2
    exit 1
}

start_agent_and_unlock() {
    if [[ -n "$AGENT_PID" ]] && kill -0 "$AGENT_PID" 2>/dev/null; then
        kill -TERM "$AGENT_PID" 2>/dev/null || true
        wait "$AGENT_PID" 2>/dev/null || true
    fi
    rm -f "$AGENT_SOCK"
    ssh-agent -D -a "$AGENT_SOCK" >/dev/null 2>&1 &
    AGENT_PID=$!
    local i
    for ((i = 0; i < 30; i++)); do
        [[ -S "$AGENT_SOCK" ]] && break
        sleep 0.1
    done
    [[ -S "$AGENT_SOCK" ]] || fail "Private ssh-agent socket was not created"

    local askpass="${REPO_ROOT}/scripts/askpass.sh"
    [[ -x "$askpass" ]] || fail "askpass.sh not executable at ${askpass}"

    env -u DISPLAY \
        SSH_AUTH_SOCK="$AGENT_SOCK" \
        SSH_ASKPASS="$askpass" \
        SSH_ASKPASS_REQUIRE=force \
        DC_SSH_PASSPHRASE="$PASSPHRASE" \
        setsid ssh-add "${STATE_DIR}/ssh/collector_key" </dev/null >/dev/null 2>&1 \
        || fail "ssh-add failed to unlock ephemeral client key"
}

ssh_cmd_for_port() {
    local port="$1"
    printf 'ssh -p %s -i %s -o IdentitiesOnly=yes -o IdentityAgent=%s -o UserKnownHostsFile=%s -o StrictHostKeyChecking=yes -o BatchMode=yes -o ConnectTimeout=5' \
        "$port" "${STATE_DIR}/ssh/collector_key" "$AGENT_SOCK" "${STATE_DIR}/ssh/known_hosts"
}

echo "=== 1. Starting fake fleet (all 6 PCs) ==="
DC_FAKE_FLEET_DIR="$STATE_DIR" DC_FAKE_FLEET_PASSPHRASE="$PASSPHRASE" \
    "${SCRIPT_DIR}/up.sh"

for idx in 1 2 3 4 5 6; do
    pc="pc0${idx}"
    pid_file="${STATE_DIR}/sshd/${pc}/sshd.pid"
    [[ -f "$pid_file" ]] || fail "Missing pidfile for ${pc}"
    pid="$(tr -d '[:space:]' < "$pid_file")"
    kill -0 "$pid" 2>/dev/null || fail "sshd for ${pc} (pid ${pid}) is not running"
done
pass "All 6 sshd instances are running"

python3 - "${STATE_DIR}/config.json" "${STATE_DIR}/fixtures_manifest.json" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1], encoding="utf-8"))
assert len(cfg["pcs"]) == 6, "Expected 6 PCs in config.json"
assert [p["port"] for p in cfg["pcs"]] == [22101, 22102, 22103, 22104, 22105, 22106]
manifest = json.load(open(sys.argv[2], encoding="utf-8"))
for w in ("last_24h", "last_7d", "custom_window_logs_after_end_true", "custom_window_logs_after_end_false"):
    assert w in manifest["windows"], f"Missing window {w}"
w_true = manifest["windows"]["custom_window_logs_after_end_true"]["pcs"]["pc01"]["logs"]["count"]
w_false = manifest["windows"]["custom_window_logs_after_end_false"]["pcs"]["pc01"]["logs"]["count"]
assert w_true > w_false, f"Expected includeModifiedAfterEnd=true ({w_true}) > false ({w_false})"
PY
pass "config.json and fixtures_manifest.json validated"

echo "=== 2. Unlocking ephemeral key in private ssh-agent ==="
start_agent_and_unlock
pass "Unlocked collector_key in private ssh-agent"

echo "=== 3. Verifying rsync --list-only on all 6 PCs ==="
for idx in 1 2 3 4 5 6; do
    port=$((22100 + idx))
    ssh_e="$(ssh_cmd_for_port "$port")"
    logs_list="$(rsync -r --list-only --no-human-readable -e "$ssh_e" "${CURRENT_USER}@127.0.0.1:.app/")"
    shots_list="$(rsync -r --list-only --no-human-readable -e "$ssh_e" "${CURRENT_USER}@127.0.0.1:screenshot/")"

    grep -Fq "sub dir/my log file.log" <<< "$logs_list" || fail "pc0${idx}: missing space-named log in listing"
    grep -Fq "café_日本語.log" <<< "$logs_list" || fail "pc0${idx}: missing UTF-8 log in listing"
    grep -Fq ".hidden/trace.log" <<< "$logs_list" || fail "pc0${idx}: missing .hidden/trace.log in listing"
    grep -Eq "^l.*link_to_recent\.log" <<< "$logs_list" || fail "pc0${idx}: missing symlink in log listing"
    grep -Fq "2026-11/big shot 1.png" <<< "$shots_list" || fail "pc0${idx}: missing space-named screenshot in listing"
    grep -Fq "écran_テスト.png" <<< "$shots_list" || fail "pc0${idx}: missing UTF-8 screenshot in listing"
done
pass "rsync --list-only verified across pc01..pc06 (spaces, UTF-8, .hidden, symlink)"

echo "=== 4. Verifying rsync file copy from pc01 ==="
COPY_DEST="${WORK_TMP}/copy_pc01"
mkdir -p "${COPY_DEST}/logs" "${COPY_DEST}/screenshots"
ssh_e_1="$(ssh_cmd_for_port 22101)"
rsync -rt --no-links -e "$ssh_e_1" "${CURRENT_USER}@127.0.0.1:.app/" "${COPY_DEST}/logs/"
rsync -rt --no-links -e "$ssh_e_1" "${CURRENT_USER}@127.0.0.1:screenshot/" "${COPY_DEST}/screenshots/"
cmp -s "${STATE_DIR}/pcs/pc01/.app/sub dir/my log file.log" "${COPY_DEST}/logs/sub dir/my log file.log" \
    || fail "Copied space-named log differs from source"
cmp -s "${STATE_DIR}/pcs/pc01/.app/café_日本語.log" "${COPY_DEST}/logs/café_日本語.log" \
    || fail "Copied UTF-8 log differs from source"
cmp -s "${STATE_DIR}/pcs/pc01/screenshot/2026-11/big shot 1.png" "${COPY_DEST}/screenshots/2026-11/big shot 1.png" \
    || fail "Copied multi-MB screenshot differs from source"
pass "rsync copy verified (exact byte match on logs and screenshots)"

echo "=== 5. Verifying shell attempt (ssh ... id) is refused by rrsync ==="
set +e
shell_out="$(ssh -p 22101 \
    -i "${STATE_DIR}/ssh/collector_key" \
    -o IdentitiesOnly=yes \
    -o IdentityAgent="$AGENT_SOCK" \
    -o UserKnownHostsFile="${STATE_DIR}/ssh/known_hosts" \
    -o StrictHostKeyChecking=yes \
    -o BatchMode=yes \
    "${CURRENT_USER}@127.0.0.1" id 2>&1)"
shell_rc=$?
set -e
[[ "$shell_rc" -ne 0 ]] || fail "Shell attempt unexpectedly succeeded!"
grep -Eiq "rrsync|does not run rsync" <<< "$shell_out" \
    || fail "Unexpected shell refusal output: ${shell_out}"
pass "Remote shell attempt refused by rrsync (exit ${shell_rc})"

echo "=== 6. Verifying rsync upload (write attempt) is refused by rrsync -ro ==="
printf 'unauthorized write\n' > "${WORK_TMP}/upload_test.txt"
set +e
upload_out="$(rsync -e "$ssh_e_1" "${WORK_TMP}/upload_test.txt" "${CURRENT_USER}@127.0.0.1:.app/upload_test.txt" 2>&1)"
upload_rc=$?
set -e
[[ "$upload_rc" -ne 0 ]] || fail "Write attempt unexpectedly succeeded!"
grep -Eiq "read-only|rrsync" <<< "$upload_out" \
    || fail "Unexpected write refusal output: ${upload_out}"
[[ ! -e "${STATE_DIR}/pcs/pc01/.app/upload_test.txt" ]] || fail "Uploaded file was created on remote!"
pass "Write attempt refused by rrsync -ro (exit ${upload_rc})"

echo "=== 7. Verifying stop-pc / start-pc mid-run control ==="
DC_FAKE_FLEET_DIR="$STATE_DIR" "${SCRIPT_DIR}/up.sh" stop-pc pc05 >/dev/null
ssh_e_5="$(ssh_cmd_for_port 22105)"
if rsync -r --list-only -e "$ssh_e_5" "${CURRENT_USER}@127.0.0.1:.app/" >/dev/null 2>&1; then
    fail "pc05 still reachable after stop-pc"
fi
DC_FAKE_FLEET_DIR="$STATE_DIR" "${SCRIPT_DIR}/up.sh" start-pc pc05 >/dev/null
rsync -r --list-only -e "$ssh_e_5" "${CURRENT_USER}@127.0.0.1:.app/" >/dev/null \
    || fail "pc05 not reachable after start-pc"
pass "stop-pc / start-pc verified on pc05"

echo "=== 8. Verifying down.sh cleans everything up ==="
pids_before=()
for idx in 1 2 3 4 5 6; do
    pids_before+=("$(tr -d '[:space:]' < "${STATE_DIR}/sshd/pc0${idx}/sshd.pid")")
done
DC_FAKE_FLEET_DIR="$STATE_DIR" "${SCRIPT_DIR}/down.sh"
for pid in "${pids_before[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
        fail "sshd pid ${pid} still running after down.sh"
    fi
done
[[ ! -e "$STATE_DIR" ]] || fail "${STATE_DIR} still exists after down.sh"
pass "down.sh stopped all 6 sshd instances and removed ${STATE_DIR}"

echo "=== 9. Verifying --offline pc05 --unreadable pc04 and chmod 000 cleanup ==="
DC_FAKE_FLEET_DIR="$STATE_DIR" DC_FAKE_FLEET_PASSPHRASE="$PASSPHRASE" \
    "${SCRIPT_DIR}/up.sh" --offline pc05 --unreadable pc04
[[ ! -f "${STATE_DIR}/sshd/pc05/sshd.pid" ]] || fail "pc05 should be offline"
[[ -f "${STATE_DIR}/pcs/pc04/.app/unreadable_secret.log" ]] || fail "unreadable file missing on pc04"

start_agent_and_unlock
ssh_e_4="$(ssh_cmd_for_port 22104)"
mkdir -p "${WORK_TMP}/copy_pc04"
set +e
rsync -rt --no-links -e "$ssh_e_4" "${CURRENT_USER}@127.0.0.1:.app/" "${WORK_TMP}/copy_pc04/" >/dev/null 2>&1
unreadable_rc=$?
set -e
[[ "$unreadable_rc" -eq 23 ]] || fail "Expected rsync exit 23 on unreadable file, got ${unreadable_rc}"
pass "--offline pc05 and --unreadable pc04 (rsync exit 23) verified"

DC_FAKE_FLEET_DIR="$STATE_DIR" "${SCRIPT_DIR}/down.sh"
[[ ! -e "$STATE_DIR" ]] || fail "${STATE_DIR} still exists after down.sh with chmod 000 file"
pass "down.sh cleanly removed state containing chmod 000 file"

echo "=== ALL FAKE-FLEET VERIFICATION CHECKS PASSED ==="
