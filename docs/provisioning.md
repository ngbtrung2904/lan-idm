# Data Collector — Provisioning Guide

This guide explains how to prepare the Ubuntu desktop operator station and the 6 headless Ubuntu PCs (`pc01`–`pc06`) for read-only log and screenshot collection over SSH + `rsync`.

No custom daemon or service runs on the PCs. Access is enforced solely by OpenSSH key authentication and `rrsync -ro`.

---

## 1. Prerequisites

Ensure all 7 machines (1 desktop + 6 headless PCs) meet these requirements before provisioning:

| Requirement | Desktop | Headless PCs (`pc01`–`pc06`) |
|---|---|---|
| OS | Ubuntu 22.04 LTS or 24.04 LTS | Ubuntu 22.04 LTS or 24.04 LTS |
| Network | LAN reachability to all PCs on TCP port 22 | Static IP or reliable LAN DNS |
| SSH | OpenSSH 8+ (`openssh-client`) | OpenSSH 8+ (`openssh-server`) |
| rsync | `rsync` 3.1+ | `rsync` 3.1+ with `rrsync` |
| Privileges | Operator user account | `sudo` access during initial setup |
| Data paths | Writable USB mount under `/media` or `/run/media` | `/home/<user>/.app` (logs) and `/home/<user>/screenshot` (screenshots) |

> **Security note:** Never commit private keys, passphrases, or production hostnames/IPs to version control. The IP addresses below (`192.168.10.11`–`192.168.10.16`) are placeholders.

---

## 2. Timezone and NTP Synchronization Requirement

> **CRITICAL:** The desktop and all 6 PCs **MUST** be configured with the **exact same IANA timezone** and have **NTP enabled** (`systemd-timesyncd`).

### Why this is mandatory

1. Because the collector SSH key is locked to `rrsync -ro`, Data Collector cannot run remote `find` commands. Instead, it lists files with `rsync -r --list-only --no-human-readable` and filters by modification time (`mtime`) on the desktop.
2. `rsync --list-only` prints timestamps in each **PC's local timezone without a UTC offset** (e.g. `2026/05/12 14:30:00`), and Data Collector parses them in the **desktop's local timezone**.
3. If a PC's timezone differs from the desktop's timezone (or its clock drifts), every file on that PC will be shifted relative to the operator's selected Start/End window — causing missed files or unexpected extra files.

### Setting and verifying time sync

Pick one timezone for the entire fleet (for example `UTC` or your local timezone such as `Asia/Ho_Chi_Minh`) and apply it to the desktop first:

```bash
sudo timedatectl set-timezone Asia/Ho_Chi_Minh
sudo timedatectl set-ntp true
```

`scripts/provision-pc.sh` applies the same `--timezone` and enables `systemd-timesyncd` on every PC. Verify synchronization on the desktop and each PC:

```bash
timedatectl status
# Confirm:
#   Time zone: Asia/Ho_Chi_Minh (+07, +0700)   <-- identical on all 7 machines
#   System clock synchronized: yes
#   NTP service: active
```

---

## 3. Provisioning Workflow

Both scripts are idempotent and support `--dry-run`, which prints every command and file change without modifying the system. **Always preview with `--dry-run` first.**

### Option A: Automated Setup from the Desktop (Recommended)

`scripts/setup-desktop.sh` performs the complete setup in five steps:
1. Generates a passphrase-protected `ed25519` key pair (`~/.ssh/collector_key`).
2. Scans each PC's SSH host key (`ssh-keyscan`), displays its **SHA256 fingerprint**, and asks you to verify and confirm before writing to `~/.local/share/DataCollector/known_hosts`. (Host keys are never auto-accepted.)
3. Copies and runs `scripts/provision-pc.sh` on each PC (you enter the PC password / `sudo` password once during initial setup).
4. Writes `~/.config/DataCollector/config.json`.
5. Runs a read-only `rsync -d --list-only` probe against every PC.

#### Step 1 — Preview with `--dry-run`

```bash
./scripts/setup-desktop.sh \
  --user user \
  --timezone Asia/Ho_Chi_Minh \
  --hosts pc01:192.168.10.11,pc02:192.168.10.12,pc03:192.168.10.13,pc04:192.168.10.14,pc05:192.168.10.15,pc06:192.168.10.16 \
  --dry-run
```

#### Step 2 — Run live

For a **staged rollout** (recommended on first deployment), pass `--keep-password-auth` so password login stays enabled on the PCs until you have tested the GUI end-to-end:

```bash
./scripts/setup-desktop.sh \
  --user user \
  --timezone Asia/Ho_Chi_Minh \
  --keep-password-auth \
  --hosts pc01:192.168.10.11,pc02:192.168.10.12,pc03:192.168.10.13,pc04:192.168.10.14,pc05:192.168.10.15,pc06:192.168.10.16
```

When prompted:
- **Key passphrase:** Choose a strong passphrase and share it only with authorized operators. Do **not** leave it blank.
- **Host key fingerprints:** Compare each displayed `SHA256:...` fingerprint against the PC's local host key (`ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub -E sha256`) before typing `y`.

Once you have verified collection works, re-run without `--keep-password-auth` to disable SSH password authentication on all PCs.

### Option B: Manual / Per-PC Provisioning

If you prefer to provision each PC directly (for example, via local console or an existing management tool):

1. On the desktop, generate the key, pin host keys, and create `config.json` without touching the PCs:
   ```bash
   ./scripts/setup-desktop.sh \
     --user user \
     --skip-distribute \
     --hosts pc01:192.168.10.11,pc02:192.168.10.12,pc03:192.168.10.13,pc04:192.168.10.14,pc05:192.168.10.15,pc06:192.168.10.16
   ```
   *(The final probe will report failures until the PCs are provisioned in step 2.)*

2. Copy `scripts/provision-pc.sh` and `~/.ssh/collector_key.pub` to each PC, preview with `--dry-run`, then run:
   ```bash
   # Preview first
   sudo ./provision-pc.sh \
     --user user \
     --pubkey /tmp/collector_key.pub \
     --timezone Asia/Ho_Chi_Minh \
     --dry-run

   # Apply
   sudo ./provision-pc.sh \
     --user user \
     --pubkey /tmp/collector_key.pub \
     --timezone Asia/Ho_Chi_Minh \
     --disable-password-auth
   ```

3. Re-run `setup-desktop.sh --skip-distribute` on the desktop to confirm all 6 read-only probes pass.

### Script CLI Reference

#### `scripts/provision-pc.sh`

| Flag | Default | Description |
|---|---|---|
| `--pubkey <path_or_string>` | *(required)* | Path to `.pub` file or raw OpenSSH public key string |
| `--user <ssh_user>` | `user` | Target Linux account on the PC |
| `--timezone <tz>` | `UTC` | IANA timezone to set via `timedatectl` |
| `--disable-password-auth` | *(enabled)* | Disable `PasswordAuthentication` and `KbdInteractiveAuthentication` |
| `--keep-password-auth` | *(off)* | Keep password auth enabled for staged rollout |
| `--dry-run` | *(off)* | Print every command and file change without modifying the system |

#### `scripts/setup-desktop.sh`

| Flag | Default | Description |
|---|---|---|
| `--hosts <id:host[:port],...>` | *(required)* | Comma-separated PC list (port defaults to 22) |
| `--user <ssh_user>` | `user` | SSH username on remote PCs |
| `--key-path <path>` | `~/.ssh/collector_key` | Path to ed25519 key pair |
| `--known-hosts <path>` | `~/.local/share/DataCollector/known_hosts` | App-dedicated `known_hosts` file |
| `--config-out <path>` | `~/.config/DataCollector/config.json` | Output path for `config.json` |
| `--timezone <tz>` | Desktop's current TZ | Timezone forwarded to `provision-pc.sh` |
| `--skip-distribute` | *(off)* | Skip remote `provision-pc.sh` execution |
| `--keep-password-auth` | *(off)* | Forward `--keep-password-auth` to `provision-pc.sh` |
| `--dry-run` | *(off)* | Print every change without modifying the system or connecting |

---

## 4. Verifying `rrsync -ro` Restriction

After provisioning, confirm from the desktop that the collector key can **only** read under `/home/<user>` and cannot open a shell, write files, or escape the home directory.

Set helper variables for one PC (e.g. `pc01` at `192.168.10.11`):

```bash
KEY=~/.ssh/collector_key
KH=~/.local/share/DataCollector/known_hosts
HOST=192.168.10.11
SSH_OPTS="-i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=$KH"
```

1. **Read-only listing MUST succeed:**
   ```bash
   rsync -d --list-only -e "ssh $SSH_OPTS" user@${HOST}:./
   # Expected: directory listing of /home/user (exit code 0)
   ```

2. **Interactive shell / remote command MUST be rejected:**
   ```bash
   ssh $SSH_OPTS user@${HOST} id
   # Expected: rejected by rrsync ("rrsync: Not invoked via sshd" / "rrsync: restricted to rsync") with non-zero exit code
   ```

3. **Writing / uploading a file MUST be rejected:**
   ```bash
   touch /tmp/dc-write-test.txt
   rsync -e "ssh $SSH_OPTS" /tmp/dc-write-test.txt user@${HOST}:./
   # Expected: rejected by rrsync ("sending to read-only server not allowed") with non-zero exit code
   rm -f /tmp/dc-write-test.txt
   ```

4. **Path traversal outside `/home/<user>` MUST be rejected:**
   ```bash
   rsync --list-only -e "ssh $SSH_OPTS" user@${HOST}:../../etc/passwd
   # Expected: rejected or resolved inside /home/user only (never exposes /etc/passwd)
   ```

---

## 5. Rollback and Recovery

### 5.1 Re-enabling Password Authentication (Lockout Recovery)

Because the collector key is restricted to `rrsync -ro`, it cannot open an administrative shell. If you disabled password authentication and do not have a separate unrestricted admin SSH key, recover via the PC's **local console** (attach a monitor and keyboard):

```bash
# 1. Remove the Data Collector sshd drop-in
sudo rm -f /etc/ssh/sshd_config.d/99-datacollector.conf

# 2. Validate sshd configuration
sudo sshd -t

# 3. Reload sshd
sudo systemctl reload ssh
```

> **Tip:** Before running `--disable-password-auth` in production, consider adding a separate administrator SSH key (without `rrsync` restriction) to a dedicated admin account, or verify local console access is available.

### 5.2 Removing `99-datacollector.conf` Cleanly

To undo the sshd hardening applied by `provision-pc.sh` while keeping `openssh-server` and `rsync` installed:

```bash
sudo rm -f /etc/ssh/sshd_config.d/99-datacollector.conf
sudo sshd -t && sudo systemctl reload ssh
```

### 5.3 Revoking or Rotating the Collector Key

If the collector key file is compromised, lost, or due for scheduled rotation:

1. **Revoke the old key on every PC:**
   Edit `/home/<user>/.ssh/authorized_keys` and remove the line starting with `command="/usr/bin/rrsync -ro /home/<user>",restrict ...`:
   ```bash
   sudo sed -i '/rrsync -ro/d' /home/user/.ssh/authorized_keys
   ```
2. **Generate a new key pair on the desktop:**
   Move or securely delete the old key pair (`~/.ssh/collector_key` and `~/.ssh/collector_key.pub`), then run `scripts/setup-desktop.sh` (or `ssh-keygen -t ed25519 -a 64 -f ~/.ssh/collector_key`).
3. **Install the new public key on every PC:**
   Re-run `scripts/provision-pc.sh --pubkey <new_key.pub> ...` on each PC (it replaces/appends idempotently) and verify with `rsync -d --list-only`.

### 5.4 Handling a Changed PC Host Key

If a PC is reinstalled or its SSH host keys are regenerated, Data Collector will display **"Host key changed"** and refuse to connect. Never bypass host key checking. Instead:

1. Verify out-of-band (on the PC's console) that the host key change is legitimate:
   ```bash
   ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub -E sha256
   ```
2. On the desktop, remove the stale entry from Data Collector's `known_hosts`:
   ```bash
   ssh-keygen -R <PC_IP> -f ~/.local/share/DataCollector/known_hosts
   ```
3. Re-run `scripts/setup-desktop.sh --skip-distribute --hosts <id>:<PC_IP>` to inspect and confirm the new SHA256 fingerprint interactively.
