# Phase 0 — Technical Spikes

Environment tested:
- OS: Ubuntu 24.04.5 LTS (`noble`)
- rsync: `3.2.7` (protocol 31)
- OpenSSH: `OpenSSH_9.6p1`
- Qt: `6.7.2` (`/home/trungnb/Qt/6.7.2/gcc_64`, targeting Qt 6.2+ APIs)
- `rrsync`: `/usr/bin/rrsync`
- `udisksctl`: `/usr/bin/udisksctl`

---

## S1: `rsync --list-only` against restricted `rrsync -ro`

**Verdict: GO** (with one critical SSH option adjustment).

### Critical Finding: `-i <sshKeyPath>` required with `IdentitiesOnly=yes`
When passing `-o IdentitiesOnly=yes -o IdentityAgent=<sock>` **without** `-i <key_path>`, OpenSSH refuses to offer keys held in the private agent (`Permission denied (publickey)`). Adding `-i <sshKeyPath>` alongside `-o IdentitiesOnly=yes -o IdentityAgent=<sock>` authenticates cleanly using the unlocked key from the private agent without ever touching `~/.ssh/id_*` or the system agent.
- Action: `SshOptions` will always include `-i <sshKeyPath>`. Recorded in `docs/decisions.md`.

### Format comparison
1. `rsync -r --list-only --no-human-readable -e "<ssh>" user@host:.app/`
```text
drwxrwxr-x        4096 2026/11/19 20:43:39 .
-rw-rw-r--          10 2026/11/19 20:43:39 app.log
lrwxrwxrwx           7 2026/11/19 20:43:39 link.log
drwxrwxr-x        4096 2026/11/19 20:43:39 sub dir
-rw-rw-r--          10 2026/11/19 20:43:39 sub dir/my log file.log
drwxrwxr-x        4096 2026/11/19 20:43:39 sub dir/.hidden
-rw-rw-r--           8 2026/11/19 20:43:39 sub dir/.hidden/café_日本語.log
```
2. `rsync -rn --out-format="%l|%M|%n"` emitted extra `skipping non-regular file "link.log"` lines and requires a dummy destination directory.

**Decision**: Use `rsync -r --list-only --no-human-readable`. Keep only lines whose permission token starts with `-` (regular files). Strip commas from size tokens if present (for compatibility with rsync 3.1/3.2). Spaces, UTF-8/Unicode names, and hidden directories are preserved verbatim.

### Restriction verification (`rrsync -ro`)
- Remote shell attempt (`ssh ... id`): rejected with `/usr/bin/rrsync error: SSH_ORIGINAL_COMMAND does not run rsync` (exit 1).
- Write attempt (`rsync local -> remote`): rejected with `/usr/bin/rrsync error: sending to read-only server is not allowed` (exit 12).

### Scale benchmark (100,000 files)
- 100 directories x 1,000 files (100,101 lines): listed in **1,425 ms** (~1.4 s). Well within interactive latency budgets.

---

## S2: Private `ssh-agent -D` + `ssh-add` with `SSH_ASKPASS_REQUIRE=force`

**Verdict: GO**

- Started `ssh-agent -D -a <socket>` as a child process.
- Invoked `ssh-add <key>` with `DISPLAY` unset, stdin `< /dev/null`, `setsid` (no controlling TTY), `SSH_AUTH_SOCK=<socket>`, `SSH_ASKPASS=<askpass.sh>`, `SSH_ASKPASS_REQUIRE=force`, and passphrase passed only via a single child-process env var (`DC_KEY_PASS`).
- Result:
  ```text
  Identity added: /tmp/dc-spikes/ssh/client_key (trungnb@tower7810)
  ssh-add exit: 0
  ```
- Killing the `QProcess` owning `ssh-agent -D` terminates the agent immediately; `KeyAgent` destructor, `lock()`, and `QCoreApplication::aboutToQuit` all terminate/kill the child and remove the socket file.
- Note: On Linux we also check `prctl(PR_SET_PDEATHSIG, SIGTERM)` in `QProcess::setChildProcessModifier` (available in Qt 6.0+; or `setProcessEnvironment` + parent cleanup) so even on `SIGKILL` of the desktop app, `ssh-agent` dies automatically. (Will verify Qt 6.2 compatibility: `QProcess::setChildProcessModifier` was added in Qt 6.0 — safe for Qt 6.2+.)

---

## S3: USB detection and `udisksctl` eject

**Verdict: GO**

- `/sys/block/<dev>/removable` is world-readable without root (`nvme0n1: 0`, `sda: 0`, `sr0: 1`, `loop*: 0`).
- For a partition device (e.g. `/dev/sdb1`, `/dev/mmcblk0p1`), resolving `/sys/class/block/<devname>` realpath points to `.../block/<parent>/<devname>`, allowing clean extraction of `<parent>` and reading `/sys/block/<parent>/removable == 1`.
- `QStorageInfo::mountedVolumes()` provides mount point, `device()`, `name()` (label), `bytesAvailable()`, `bytesTotal()`, `isValid()`, `isReady()`, `isReadOnly()`.
- `udisksctl` is installed at `/usr/bin/udisksctl` and works without root for `udisksctl status`, `unmount -b <dev>`, and `power-off -b <dev>`.
- `UsbMonitor` will inject a volume provider interface so unit tests can deterministically test none, one, multiple, removal, re-insert, and eject without requiring physical USB hardware.

---

## S4: `--info=progress2,name1` parsing with `--files-from`

**Verdict: GO**

Sample raw output (with `\r` shown as `^M`):
```text
2026-11/
2026-11/big shot 1.png
^M              0   0%    0.00kB/s    0:00:00  ^M      4,194,304 100%  173.91MB/s    0:00:00 (xfr#1, to-chk=0/2)
```

- Splitting incremental stdout chunks on `\r` and `\n` cleanly separates:
  - Progress lines: match `^\s*([\d,]+)\s+(\d+)%\s+([\d.]+)([kMGT]?B/s)\s+(\d+:\d+:\d+)` -> extracts `bytesDone` (strip commas), `percent`, `rateBytesPerSec`.
  - File name lines (`name1`): non-empty lines not matching the progress regex and not ending in `/` -> updates `currentFile`.
- `--no-inc-recursive` ensures rsync calculates total counts upfront with `--files-from --from0`.
- Combining per-PC `bytesDone` against the pre-computed `SpaceEstimator` planned bytes gives exact weighted overall progress (`sum(bytesDone) / sum(bytesPlanned)`).

