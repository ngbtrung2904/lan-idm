# Take-away note
## What is user provided in setup-desktop.sh?
- `--user` is the existing Linux username on the remote PCs (pc01–pc07) — specifically the account whose home directory contains the logs and media (/home/<user>/.app, /home/<user>/Pictures/lightshot, /home/<user>/recordings, etc.).
- Not a new user: setup-desktop.sh and provision-pc.sh do not create a new Linux account; they configure SSH key access on that existing remote account (/home/<user>/.ssh/authorized_keys with rrsync -ro /home/<user>).
- Not necessarily the desktop's current user: On the desktop, your key and config.json are always saved under your current desktop user's $HOME (~/.ssh/collector_key, ~/.config/DataCollector/config.json). --user only controls the "sshUser" used to SSH into the remote PCs.

## Why systemd-timesyncd is removed?
- All PCs are now synced time via PTP service. Do not need systemd-timesyncd anymore.

## When login to desktop, which user/password is used?
- You can use any user (e.g operator1)
- Passphrase: the SSH key passphrase you chose when run setup-desktop.sh

## Each PCs has unique user, you should pass them into setup-desktop.sh
- `--user` is the existing Linux username on the remote PCs (pc01–pc07) — specifically the account whose home directory contains the logs and media (/home/<user>/.app, /home/<user>/Pictures/lightshot, /home/<user>/recordings, etc.).
