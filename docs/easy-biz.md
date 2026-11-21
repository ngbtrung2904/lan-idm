# Take-away note

## 1. What is `--user` provided in `setup-desktop.sh`?
- `--user` is the **existing Linux username on the remote PCs (`pc01`–`pc07`)** — specifically the account whose home directory contains the logs and media (`/home/<user>/.app`, `/home/<user>/Pictures/lightshot`, `/home/<user>/recordings`, etc.).
- **Not a new user:** `setup-desktop.sh` and `provision-pc.sh` do not create a new Linux account; they configure SSH key access on that existing remote account (`/home/<user>/.ssh/authorized_keys` with `rrsync -ro /home/<user>`).
- **Not necessarily the desktop's current user:** On the desktop, your key and `config.json` are always saved under your current desktop user's `$HOME` (`~/.local/share/DataCollector/collector_key`, `~/.config/DataCollector/config.json`). `--user` only controls the `"sshUser"` used to SSH into the remote PCs.

---

## 2. Why `systemd-timesyncd` is removed?
- All PCs already synchronize time via **PTP** (`ptp4l` / `phc2sys`).
- Running `systemd-timesyncd` (NTP) alongside PTP causes two time services to compete for the system clock and can break PTP accuracy.
- Therefore, `provision-pc.sh` does **not** install `systemd-timesyncd` or run `timedatectl set-ntp true`; it only aligns the system timezone (`timedatectl set-timezone <tz>`) so `rsync` timestamps match the desktop timezone.

---

## 3. When logging into Data Collector on the desktop, which user/password is used?
- **Operator ID:** Any non-empty name or ID (e.g., `operator01`, your name, or badge ID). It is not a Linux user account — it is only written into `manifest.json` as an audit label.
- **Passphrase:** The **SSH key passphrase** you chose when running `setup-desktop.sh` to create `collector_key` (not your Ubuntu desktop password and not a remote PC password). It unlocks `collector_key` inside Data Collector's private `ssh-agent` for the current session only.

---

## 4. When each remote PC has a unique username, how to pass them?
- In `setup-desktop.sh`, pass `id:user@host` (or `id:user@host:port`) inside `--hosts`:
  ```bash
  ./scripts/setup-desktop.sh \
    --timezone Asia/Ho_Chi_Minh \
    --keep-password-auth \
    --hosts pc01:user01@192.168.10.11,pc02:user02@192.168.10.12,pc03:user03@192.168.10.13,pc04:user04@192.168.10.14,pc05:user05@192.168.10.15,pc06:user06@192.168.10.16,pc07:user07@192.168.10.17
  ```
- Any PC that omits `user@` falls back to `--user <default_user>`.
- In `~/.config/DataCollector/config.json`, each entry in `"pcs"` can also specify `"sshUser"`:
  ```json
  "pcs": [
    { "id": "pc01", "host": "192.168.10.11", "sshUser": "user01" },
    { "id": "pc02", "host": "192.168.10.12", "sshUser": "user02" }
  ]
  ```

---

## 5. End-to-End Explanation: What `setup-desktop.sh` & `provision-pc.sh` Impact on a Remote PC

### Step-by-Step Flow
1. **Before provisioning:**
   - When you run `ssh user1@192.168.10.11`, the remote PC prompts for `user1`'s Linux password and opens an interactive bash shell.
2. **During `setup-desktop.sh`:**
   - Generates a passphrase-protected `ed25519` key pair (`~/.local/share/DataCollector/collector_key`).
   - Scans each remote PC's SSH host key, shows the SHA256 fingerprint for you to confirm, and saves it to `~/.local/share/DataCollector/known_hosts`.
   - Copies and runs `provision-pc.sh` on each remote PC.
3. **What `provision-pc.sh` changes on the remote PC:**
   - **Change A — Restricted `authorized_keys` entry:**
     Adds the collector public key to `/home/<user>/.ssh/authorized_keys` with:
     ```text
     command="/usr/bin/rrsync -ro /home/<user>",restrict ssh-ed25519 AAAA... datacollector
     ```
     This tells OpenSSH on the remote PC: *"Whenever a client authenticates with `collector_key`, NEVER give them an interactive shell. Only allow read-only `rsync` inside `/home/<user>`."*
   - **Change B — SSH password login setting (`/etc/ssh/sshd_config.d/99-datacollector.conf`):**
     - With `--keep-password-auth`: keeps password login enabled (`PubkeyAuthentication yes` only).
     - Without `--keep-password-auth` (final lockdown): sets `PasswordAuthentication no` and `KbdInteractiveAuthentication no` so no one can log in over SSH with a password.

### Summary of All Impacts on a Remote PC

| Item on Remote PC | Impact |
|---|---|
| `openssh-server`, `rsync` | Installed via `apt-get` if missing. |
| `/usr/bin/rrsync` | Standard read-only rsync wrapper used by `authorized_keys`. |
| `/home/<user>/.ssh/authorized_keys` | Adds 1 restricted line for `collector_key` (`rrsync -ro /home/<user>`). Existing keys are preserved. |
| `/etc/ssh/sshd_config.d/99-datacollector.conf` | Keeps password auth enabled when `--keep-password-auth` is passed; disables password auth when omitted. |
| System timezone (`timedatectl set-timezone`) | Aligned with desktop timezone (PTP time sync is untouched). |
| Application files & logs | **Untouched.** Read-only access only. |

---

## 6. Why `ssh user@remote-pc` Failed Even When `--keep-password-auth` Was Used

### Symptom
After running:
```bash
./scripts/setup-desktop.sh --user trungnb --hosts pc01:192.168.68.116 --keep-password-auth
```
Running `ssh trungnb@192.168.68.116` did **not** ask for a password and immediately failed with:
```text
PTY allocation request failed on channel 0
/usr/bin/rrsync error: SSH_ORIGINAL_COMMAND does not run rsync
Connection to 192.168.68.116 closed.
```

### Root Cause (Why Password Prompt Was Skipped)
1. Password authentication was **still enabled** on the remote PC (`--keep-password-auth` worked as intended).
2. However, `collector_key` was originally stored inside `~/.ssh/collector_key` on the Ubuntu desktop.
3. On Ubuntu Desktop, **GNOME Keyring** (`SSH_AUTH_SOCK=/run/user/1000/keyring/ssh`) automatically scans `~/.ssh/*.pub` and loads every key in `~/.ssh/` into your desktop's system SSH agent.
4. When you ran `ssh trungnb@192.168.68.116`, OpenSSH **always tries keys in your system `ssh-agent` BEFORE asking for a password**.
5. The remote PC saw `collector_key`, accepted it (so it never needed to ask for your password!), and immediately applied the `authorized_keys` rule for `collector_key` (`command="/usr/bin/rrsync -ro ...",restrict`), which blocks interactive terminal shells.

### How It Was Fixed
1. **Moved `collector_key` out of `~/.ssh/`** into Data Collector's dedicated folder:
   ```text
   ~/.local/share/DataCollector/collector_key
   ```
2. Because the key is no longer inside `~/.ssh/`, Ubuntu's GNOME Keyring never auto-loads it into your system `ssh-agent`.
3. Now:
   - **Normal terminal `ssh trungnb@192.168.68.116`** has no `collector_key` in the system agent, so it prompts for your password as usual and opens a bash shell.
   - **Data Collector GUI** runs its own private `ssh-agent` and explicitly passes `-i ~/.local/share/DataCollector/collector_key`, so `rsync` collection works seamlessly.
   - If you ever want to force password-only SSH from any machine, you can also run:
     ```bash
     ssh -o PubkeyAuthentication=no <user>@<pc_ip>
     ```
