# Data Collector — Operator Guide

Data Collector copies application logs and screenshots from headless PCs onto a USB drive over the local network. Follow the five steps below for each collection.

---

## 1. Choose a destination (USB drive or local disk)

- **USB drive (default):** Insert a writable USB drive into the desktop before or after opening the app. The drive is detected automatically and shown in the top-right header chip (**USB ready** with mount path and free space). If multiple USB drives are plugged in, click the header chip to choose the target drive from the dropdown, or click **Rescan drives** if a drive was just inserted.
- **Local disk folder:** If no USB drive is attached—or if you prefer to save directly onto the operator desktop—click the destination chip in the header (or the **Save to local disk** action in the no-USB banner) and choose **Local disk**.
  - By default, local collections are saved under `~/DataCollector` (configurable via `"localOutputPath"` in `config.json`). You can also enter or browse to a different local folder before starting the copy.
  - While Local Disk mode is active, plugging or unplugging a USB drive will not interrupt or steal the active destination.
  - Because no removable drive is used, **Eject USB safely** is not shown after a local disk copy finishes.

## 2. Open Data Collector & Sign in

- Open **Data Collector** from the desktop application menu (or run `data-collector`).
- Enter your **Operator ID** (recorded in the collection manifest for audit tracking).
- Enter the **Passphrase** for the collection SSH key and click **Sign in**.
  - The key is unlocked in a private session agent for this session only and locked automatically when you click **Sign out** or close the app.
  - **Lockout:** After **5 consecutive failed attempts**, Sign in is disabled for **30 seconds**. Wait for the timer to finish, then try again.

## 3. Select time range & PCs

- **Time range:** Choose a **Start** and **End** date/time, or click a preset (**Last 24 h**, **Last 7 days**, **Today**). Changing the range updates file counts and sizes immediately without reconnecting to the PCs.
  - Screenshots are included when their timestamp falls between Start and End.
  - Logs modified at or after Start are included so active log files are not missed.
- **PCs:** Online PCs are selected automatically. Use the checkboxes (or the header select-all checkbox) to choose which online PCs to collect from.
  - If a PC was just powered on or reconnected, click **Rescan PCs** to re-check all PCs.
- Check the footer bar to confirm the estimated total fits within the USB drive's free space.

## 4. Start copy & monitor progress

- Click **Start copy**. The app creates a timestamped session folder on the USB drive:
  ```
  <usb>/collect_<yyyyMMdd_hhmmss>/
    ├── manifest.json
    ├── pc01/{logs,screenshots}/
    └── ...
  ```
- **Keep the USB drive plugged in** until all copies finish.
- The progress screen displays overall completion, transfer rate, estimated time remaining, and each PC's current stage.
- To abort early, click **Cancel copy** and confirm. Partial files are preserved on the USB drive and recorded as `cancelled` in `manifest.json`.

## 5. Eject USB safely

- Wait until the title shows **Copy finished** (or **Copy finished with warnings**).
  - If there are warnings, click **Show details** to review them (they are also recorded in `manifest.json`).
  - If an individual PC failed, you can click **Retry** on that PC before ejecting.
- Click **Eject USB safely** and wait for the confirmation message (**Safe to remove USB drive**) before physically unplugging the drive.
- **Never unplug the USB drive while a copy is in progress.**

---

## 6. Status chips & error reference

| Chip / Message | Meaning | What to do next |
|---|---|---|
| **Online** | PC is reachable and ready. | Select or deselect the PC as needed. |
| **Offline** — *"Unreachable. Check power and network, then rescan."* | PC could not be reached over SSH (powered off, disconnected, or timeout). | Verify power and network cable on the PC, then click **Rescan PCs**. |
| **Key rejected** | SSH authentication failed on this PC. | Contact your system administrator to re-provision the collection key on this PC, then click **Rescan PCs**. |
| **Host key changed** | The PC's SSH host key does not match the trusted `known_hosts` record. | **Do not ignore.** Contact your system administrator to verify the PC's fingerprint and update `known_hosts`. |
| **Incorrect passphrase** (Login) | The SSH key passphrase was wrong. | Re-enter the passphrase carefully. After 5 failures, wait 30 seconds before retrying. |
| **No USB drive detected** (amber banner) | No writable removable USB drive is mounted. | Plug in a USB drive (or click **Rescan drives** in the header menu). You can still pick a time range and PCs while waiting. |
| **Not enough space on \<drive\>** (red banner) | Selected data plus a 2% safety margin exceeds USB free space. | Deselect one or more PCs, narrow the time range, or switch to a larger USB drive. |
| **USB drive removed** (blocking panel during copy) | The USB drive was disconnected mid-transfer. | Re-insert the same USB drive and click **Retry** to resume, or click **Cancel** to abort. |
| **Failed** — *"Disk full"* / write error | The USB drive ran out of space while writing. | Cancel or finish remaining PCs, eject safely, and use a drive with more free space. |
| **Done with warnings** | Copy completed, but some files could not be read or vanished during transfer (rsync exit 23/24) or verification count differed. | Click **Show details** and inspect `manifest.json` in the session folder. |
| **Failed** — *"Lost connection"* | Network connection to the PC dropped mid-copy (exit 30/255). | Other PCs continue automatically. Check the PC's network and click **Retry** for that PC. |
| **Configuration error** (on startup) | `~/.config/DataCollector/config.json` is missing or contains an invalid key. | Report the named key from the dialog to your system administrator to fix `config.json`. |

---

## 7. Security note

- **Collected files on the USB drive are stored unencrypted.** Handle, transport, and store the USB drive in accordance with your site's data-handling and physical security policy.
- Always click **Sign out** (or close Data Collector) when finished so the private SSH key agent is stopped immediately.

---

## 8. Rollback (`setup-desktop.sh` & Remote PCs)

If you ran `scripts/setup-desktop.sh` and want to completely undo its changes on both the desktop and the remote PCs:

### 8.1 Rollback on the Desktop

Run on the desktop operator station:

```bash
# 1. Remove Data Collector config, known_hosts, and collector SSH key pair
rm -rf ~/.config/DataCollector
rm -rf ~/.local/share/DataCollector

# 2. If an older run created collector_key inside ~/.ssh/, remove it too and clear from ssh-agent
ssh-add -d ~/.ssh/collector_key.pub 2>/dev/null || true
rm -f ~/.ssh/collector_key ~/.ssh/collector_key.pub
```

### 8.2 Rollback on Each Remote PC

Run on each remote PC (via SSH if password/admin login is enabled, or via the PC's local monitor + keyboard console if password login was disabled):

```bash
# 1. Remove the Data Collector sshd drop-in (re-enables default SSH password authentication)
sudo rm -f /etc/ssh/sshd_config.d/99-datacollector.conf
sudo sshd -t && sudo systemctl reload ssh

# 2. Remove the restricted rrsync key line from authorized_keys (replace <user> with the PC's Linux username)
sudo sed -i '/rrsync -ro/d' /home/<user>/.ssh/authorized_keys

# 3. (Optional) Remove /usr/local/bin/rrsync if provision-pc.sh extracted it from rsync docs
sudo rm -f /usr/local/bin/rrsync

# 4. (Optional) Restore timezone if you want a different timezone than the one set during provisioning
# sudo timedatectl set-timezone <Original/Timezone>
```

*(Note: `openssh-server` and `rsync` are standard system packages and are normally left installed. No application logs, pictures, or recordings are ever modified by setup or rollback.)*

### 8.3 Verify Rollback

From the desktop:
```bash
# Normal password SSH should prompt for password and open a shell
ssh <user>@<pc_ip>
```
