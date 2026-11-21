# Data Collector — Quick Start Guide

This guide walks you through copying logs and screenshots from headless PCs to a USB drive. **Do Part A first to make sure everything works, then move to Part B.**

---

## Part A: Test with 1 PC (first-time setup)

### Steps

1. **Plug in a USB drive** into the desktop. Wait for the app header to show **USB ready** with free space.
2. **Open Data Collector** from the desktop app menu.
3. **Sign in** — enter your **Operator ID** and **Passphrase**, then click **Sign in**.
4. **Check the PC list** — make sure at least 1 PC shows as **Online** (green). If not, click **Rescan PCs**.
5. **Pick a small time range** — click **Last 24 h** (or **Last 1 h** if available).
6. **Select only 1 PC** — check the box next to just one online PC.
7. **Click Start copy**. Wait until the screen shows **Copy finished**.
8. **Click Eject USB safely**. Wait for the message **Safe to remove USB drive**, then unplug.

### What to check

Open the USB drive in your file manager. You should see:

- A folder named `collect_<date_time>/`
- Inside it: a **manifest.json** file and a folder named after the PC (e.g., `pc01/`)
- Inside the PC folder: `logs/` and `screenshots/` folders with files in them

If all three are there, **Part A passed**. Move to Part B.

### If something goes wrong

| Problem | What to do |
|---|---|
| **PC shows Offline** | Check the PC is powered on and the network cable is plugged in. Click **Rescan PCs**. |
| **Sign in fails** | Double-check your Passphrase. After 5 wrong tries, wait 30 seconds. |
| **No USB detected** | Plug in a USB drive. Click **Rescan drives** in the top-right header. |
| **Not enough space** | Pick a shorter time range, or use a larger USB drive. |

---

## Part B: Collect from all PCs (daily use)

Once Part A works, collecting from all PCs is the same process — just two changes:

1. **Select all PCs** — check every online PC in the list.
2. **Adjust the time range** as needed (e.g., **Last 7 days**, or set custom **Start** and **End** dates).

Then follow the same steps: **Start copy** → wait for **Copy finished** → **Eject USB safely**.

> **Tip:** Check the footer bar before starting — it shows the estimated total size. Make sure it fits on your USB drive.

---

**Security note:** Files on the USB drive are unencrypted. Handle the drive according to your site's data policy. Always click **Sign out** when done.
