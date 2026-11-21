# Data Collector — Quick-Start Provisioning Guide

**Prerequisites:** Ubuntu 22.04+ on all machines, LAN connectivity, `sudo` access on each PC, and PTP time synchronization active. The scripts align the timezone across all PCs without touching PTP or installing `systemd-timesyncd`.

---

## Phase 1: Set Up the Desktop (One-Time)

1. **Preview first** — `--dry-run` shows everything without changing anything:
   ```bash
   ./scripts/setup-desktop.sh --user user --timezone Asia/Ho_Chi_Minh \
     --hosts pc01:192.168.10.11 --dry-run
   ```

2. **Run it for real** with just 1 PC (we'll add the rest later):
   ```bash
   ./scripts/setup-desktop.sh --user user --timezone Asia/Ho_Chi_Minh \
     --hosts pc01:192.168.10.11 --keep-password-auth
   ```

3. **When prompted**, do two things carefully:
   - **Key passphrase** — choose a strong one. You'll type it every time you open Data Collector.
   - **Host key fingerprint** — compare the `SHA256:...` shown on screen against the PC's actual fingerprint (run `ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub -E sha256` on the PC). Type **y** only if they match.

4. **After it finishes**: key pair generated at `~/.local/share/DataCollector/collector_key`, rsync installed on the PC, config written to `~/.config/DataCollector/config.json`, and a read-only probe runs — you should see **probe: OK**.
   > **Note:** Check `config.example.json` for pc07-specific config differences (different log paths, no screenshots/recordings).

---

## Phase 2: Test with 1 PC

Set up helper variables, then run 3 tests from the desktop:
```bash
KEY=~/.local/share/DataCollector/collector_key; KH=~/.local/share/DataCollector/known_hosts
HOST=192.168.10.11
SSH_OPTS="-i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=$KH"
```

1. **Read must work** → `rsync -d --list-only -e "ssh $SSH_OPTS" user@${HOST}:./` — expect directory listing (exit 0)
2. **Shell must fail** → `ssh $SSH_OPTS user@${HOST} id` — expect rejected by rrsync (non-zero exit)
3. **Write must fail** → `rsync -e "ssh $SSH_OPTS" /tmp/test.txt user@${HOST}:./` — expect "sending to read-only server not allowed"

Then open **Data Collector** and do a test collection from pc01 — follow **Part A** in the [Quick Start Guide](quick-start-guide.md) (plug in USB → **Sign in** → pick **Last 1 h** → select pc01 → **Start copy**). Check the USB for `collect_<date_time>/pc01/logs/` with files inside.

**If it works → Phase 3.** If not: (a) PC offline → check power + network, (b) probe fails → re-run with `--dry-run`, (c) connection refused → verify `openssh-server` is running on the PC.

---

## Phase 3: Roll Out to All PCs

Re-run with all 7 hosts — the script is idempotent, so existing PCs are updated safely:
```bash
./scripts/setup-desktop.sh --user user --timezone Asia/Ho_Chi_Minh \
  --hosts pc01:192.168.10.11,pc02:192.168.10.12,pc03:192.168.10.13,pc04:192.168.10.14,pc05:192.168.10.15,pc06:192.168.10.16,pc07:192.168.10.17 \
  --keep-password-auth
```

Repeat Phase 2 verification for each new PC. Once all 7 pass, lock down.
> **Tip:** Manual per-PC setup (Option B) is also available — see the [full guide](provisioning.md).

---

## Phase 4: Lock Down SSH

Re-run **without** `--keep-password-auth` — this disables password login on all PCs:
```bash
./scripts/setup-desktop.sh --user user --timezone Asia/Ho_Chi_Minh \
  --hosts pc01:192.168.10.11,pc02:192.168.10.12,pc03:192.168.10.13,pc04:192.168.10.14,pc05:192.168.10.15,pc06:192.168.10.16,pc07:192.168.10.17
```

Only the restricted collector key can connect after this.

---

## Emergency: Re-Enable Password Auth

Connect a monitor + keyboard to the PC, then:
```bash
sudo rm -f /etc/ssh/sshd_config.d/99-datacollector.conf
sudo sshd -t && sudo systemctl reload ssh
```

---

## Quick Reference

| Flag | Description |
|---|---|
| `--hosts <id:[user@]ip,...>` | **Required.** Comma-separated PC list (`id:ip` or `id:user@ip` when each PC has a unique username) |
| `--user <name>` | Default SSH username on PCs when `user@` is omitted in `--hosts` (default: `user`) |
| `--timezone <tz>` | IANA timezone for all machines |
| `--keep-password-auth` | Keep password login enabled — remove after testing |
| `--dry-run` | Preview changes without modifying anything |
| `--key-path <path>` | Custom key location (default: `~/.local/share/DataCollector/collector_key`) |

For full details (manual setup, key rotation, host key changes), see [provisioning.md](provisioning.md).
