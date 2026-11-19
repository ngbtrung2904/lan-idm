# Data Collector: Orchestrator Guide (v1)

Audience: the orchestrator agent (and the developer, frontend-developer, tester, reviewer and writer subagents it drives).
Goal: implement a Linux desktop application that pulls logs and screenshots from 6 headless PCs onto a USB drive, using SSH and rsync. No custom service runs on the PCs.

---

## 0. Rules for the orchestrator

1. Read this whole guide before delegating. Do not start coding before Phase 0 (spikes) is accepted.
2. Delegate by phase (section 8). Every task has an owner role, deliverables and acceptance criteria. A task is done only when its acceptance criteria are met and the build and tests pass.
3. Define interfaces first (Phase 1). Backend and frontend then work in parallel against those interfaces. Do not let either side change an interface without updating `docs/interfaces.md` and telling the other side.
4. Gate each phase: run the build, run the tests, run the reviewer on the diff. Do not start the next phase on a red build.
5. Retry a failing task at most 3 times with concrete feedback. After that, stop and report to the user with what was tried.
6. Anything not specified here: choose the simplest option, record it in `docs/decisions.md` with one line of reasoning, and continue. Only stop for the items in section 12 marked BLOCKING (none today).
7. Never commit secrets, keys, passphrases or real hostnames to the repo. Use `config.example.json`.
8. Keep a running `docs/progress.md` (phase, task, status, blockers). The user may check it at any time.

---

## 1. Context

- A system has 6 PCs without monitors (`pc01` to `pc06`) and 1 desktop with a monitor.
- Each PC runs an application at runtime. It writes logs under `/home/user/.app` and data (screenshots) under `/home/user/screenshot`.
- An operator plugs a USB drive into the desktop, opens the app, picks a start and end time, and the app copies matching files from every selected PC onto the USB.
- A custom copy service plus TCP protocol was considered and rejected as overengineering. The chosen design is key-based SSH with rsync, driven by a Qt app on the desktop.
- The GUI mock-up exists (login, sources, progress, plus no-USB and low-space states). Section 6 restates it in text, because agents cannot open the mock-up.

Target environment: Ubuntu Linux on all machines, desktop with a monitor, LAN between desktop and PCs. Assume rsync 3.1+ and OpenSSH 8+ on all machines. Build against Qt 6.2 APIs (so it builds on Ubuntu 22.04). Do not use newer Qt features.

---

## 2. Decisions already made

| # | Decision |
|---|---|
| D1 | Transport is SSH with key authentication, files are moved by rsync. No agent or service on the PCs. |
| D2 | The desktop app is Qt 6 (QML UI, C++17 core), built with CMake, Linux only. |
| D3 | The operator unlocks a passphrase-protected SSH key at login. The app runs its own private `ssh-agent`, so there is one passphrase prompt per session, never per PC. |
| D4 | Each PC's `authorized_keys` line restricts the key to read-only rsync under `/home/user` (`rrsync -ro`). The app therefore cannot run `find` or any shell command remotely. See section 4. |
| D5 | The USB drive is auto-detected. No separate selection page. A dropdown in the header handles several drives. |
| D6 | The time filter runs on the desktop, on the file listing returned by rsync, then the filtered list is passed to rsync via `--files-from`. |
| D7 | Concurrency is limited (default 2 PCs at once) because the USB drive is the bottleneck. Configurable. |
| D8 | Copies go to a session folder on the USB (section 5.6), with a `manifest.json`. |
| D9 | Passwords are not used anywhere. Password authentication is disabled on the PCs after keys work. |

---

## 3. Architecture

```
┌──────────────────────────── Desktop (Qt 6) ───────────────────────────────┐
│ QML views: Login · Sources · Progress (+ NoUSB, LowSpace states)          │
│        ▲ properties / models / signals                                    │
│ Controllers (C++): AuthController · SourcesController · TransferController│
│        ▲                                                                  │
│ Core (C++17, no GUI deps):                                                │
│   KeyAgent        private ssh-agent + ssh-add                             │
│   SshOptions      builds the -e "ssh ..." option string                   │
│   HostProbe       online / auth / host-key check per PC                   │
│   RsyncLister     runs rsync --list-only, parses to FileEntry             │
│   TimeFilter      selects files by window and rules                       │
│   SpaceEstimator  sums sizes, compares with USB free space                │
│   UsbMonitor      detects removable drives, free space, eject             │
│   TransferEngine  per-PC jobs, scheduler, progress parsing, cancel        │
│   Manifest        manifest.json writer (atomic)                           │
│   ConfigStore     config.json (PC list, paths, concurrency)               │
└───────────────────────────────┬───────────────────────────────────────────┘
                                │ ssh + rsync (key auth, read-only)
                  pc01 … pc06: sshd + rrsync -ro /home/user
```

Rules:
- The core has no Qt Quick or Widgets dependency, so it can be unit-tested without a display.
- Every external program is run with `QProcess` using an argument list. Never build a shell command string, never use `sh -c`.
- Nothing blocks the GUI thread. Core objects use signals and `QProcess` asynchronously, or run in worker threads owned by the engine.

Suggested layout:

```
data-collector/
  CMakeLists.txt
  config.example.json
  docs/ (interfaces.md, decisions.md, spikes.md, provisioning.md, progress.md, user-guide.md)
  scripts/ (provision-pc.sh, setup-desktop.sh, askpass.sh, fake-fleet/)
  src/core/        # section 3 core classes
  src/app/         # controllers, models, main.cpp
  src/qml/         # Theme.qml, components/, views/
  tests/           # unit/ and integration/
```

---

## 4. Remote access design

### 4.1 Key and agent
- One ed25519 key pair per desktop, passphrase protected. Public key is installed on each PC by `scripts/setup-desktop.sh` (password typed once during setup only).
- At login, `KeyAgent` starts `ssh-agent -D -a <XDG_RUNTIME_DIR>/datacollector-agent.sock` as a child process, then runs `ssh-add <key>` with `SSH_AUTH_SOCK` set, `SSH_ASKPASS=scripts/askpass.sh`, `SSH_ASKPASS_REQUIRE=force`. The askpass helper prints the passphrase from an environment variable that is set only on that one child process.
- The passphrase is never written to disk, never logged, and cleared from memory after `ssh-add` returns. Sign out, app exit and crash recovery all kill the agent.
- Spike S2 must confirm this works on the target OS. If askpass is unreliable, fall back to driving `ssh-add` through a pty.

### 4.2 Restricted key on each PC
`authorized_keys` line on each PC:

```
command="rrsync -ro /home/user",restrict ssh-ed25519 AAAA... datacollector
```

Remote paths are then relative to `/home/user`: use `.app/` and `screenshot/`.

> Correction to earlier advice: running `find -newermt` on the PC and using `rrsync` are mutually exclusive, because the forced command only allows rsync. That is why the app lists files with rsync and filters on the desktop (D6). Do not add remote `find`.

### 4.3 SSH options (always)
```
-o BatchMode=yes -o ConnectTimeout=5 -o ServerAliveInterval=10 -o ServerAliveCountMax=3
-o StrictHostKeyChecking=yes -o UserKnownHostsFile=<appdata>/known_hosts
-o IdentitiesOnly=yes -o IdentityAgent=<agent socket>
```
Host keys are verified once during `setup-desktop.sh` (fingerprint shown to the operator) and stored in the app's own `known_hosts`. Never use `StrictHostKeyChecking=no`.

### 4.4 Commands

Probe (online + auth check, no recursion):
```
rsync -d --list-only -e "<ssh opts>" user@HOST:./
```

List (run twice per PC, for `.app/` and `screenshot/`):
```
rsync -r --list-only --no-human-readable -e "<ssh opts>" user@HOST:.app/
```
Parse: type/permissions, size (strip thousands separators if present), `yyyy/MM/dd HH:mm:ss`, relative path. Keep regular files only (permission string starts with `-`). Spike S1 verifies the exact output format on the real rsync versions and decides whether `--out-format='%l|%M|%n'` with `-n` is more robust.

Copy (one invocation per PC per source, sequentially: logs first, then screenshots):
```
rsync -t --files-from=<tmp list> --from0 --partial-dir=.partial --info=progress2,name1 --no-inc-recursive \
      -e "<ssh opts>" user@HOST:.app/  <session>/<pcId>/logs/
rsync -t --files-from=<tmp list> --from0 --partial-dir=.partial --info=progress2,name1 --no-inc-recursive \
      -e "<ssh opts>" user@HOST:screenshot/  <session>/<pcId>/screenshots/
```
Notes:
- `--files-from` with `--from0`: NUL-separated, safe for any file name. Sanitize every listed path first (reject `..`, absolute paths, empty names) before writing the list file.
- `--partial-dir` keeps interrupted transfers resumable. Remove `.partial` after a successful PC.
- Progress: parse `progress2` lines (split on `\r` and `\n`) for percent, bytes done, rate. Parse `name1` lines for the current file name.
- Exit codes: 0 ok; 23 and 24 are partial transfer (mark PC "Done with warnings" and list the errors); 30 timeout; 255 ssh failure. Map all to the states in section 10.

---

## 5. Behaviour specification

### 5.1 Startup
1. Load `config.json` (section 5.8). If missing or invalid, show an error dialog naming the key and exit cleanly.
2. Show Login. The Sources screen is reachable only after the key is unlocked.

### 5.2 Login
- Fields: Operator ID, Passphrase. Sign in runs `KeyAgent.unlock`. Wrong passphrase shows an inline error and clears the passphrase field. After 5 consecutive failures, disable Sign in for 30 seconds.
- Operator ID is an audit label only. It is written to the manifest and the app log. It is not authentication, the passphrase is. (See open question Q3.)
- Sign out kills the agent, clears state, returns to Login.

### 5.3 Sources screen
- On entry: start probing all configured PCs in parallel (limit 6), then list the online ones.
- Time window: Start and End `datetime-local` style pickers. Defaults: Start = now minus 24 h, End = now. Presets: Last 24 h, Last 7 days, Today. Changing the window re-filters the cached listings immediately (no new SSH round trip). Validate Start < End.
- Rescan PCs: re-probe and re-list everything, then re-filter.
- PC row: checkbox (disabled when not Online), name, IP, status chip, Logs size and file count, Screenshots size and file count, Total. Offline rows show the message "Unreachable. Check power and network, then rescan." Auth failure and host key mismatch have their own chips and messages (section 10).
- Select-all checkbox selects online PCs only.
- Footer: "N of M PCs selected", destination path, estimate bar (selected bytes vs USB free bytes), Start copy.
- Start copy is enabled only when: at least one PC selected, a writable USB is selected, estimate fits, and window is valid.

### 5.4 Time filter rules (defaults, configurable)
- Screenshots: include if `start <= mtime <= end`.
- Logs: include if `mtime >= start` (a log last written after the window may still hold in-window data, and an appended log's mtime is its last write). Config flag `logs.includeModifiedAfterEnd` (default true).
- All times are interpreted in the desktop's local timezone, and rsync reports PC local times. The provisioning doc must require the same timezone and NTP on all machines. Document this clearly.

### 5.5 USB handling
- `UsbMonitor` polls every 2 seconds (`QStorageInfo::mountedVolumes()`), keeps volumes that are valid, ready, writable, mounted under `/media` or `/run/media`, and whose parent block device has `/sys/block/<dev>/removable == 1`. Spike S3 confirms this on the target desktop.
- States: none, one, several. With several, the default is the most recently mounted. The header chip is a dropdown of drives (label, mount point, free space) plus "Rescan drives".
- Free space is re-read every 2 seconds and before Start copy.
- USB removed during a copy: pause all jobs, show a blocking error on the Progress screen ("USB drive removed"), allow Retry after the drive returns (jobs resume via rsync) or Cancel.
- Eject: only enabled when no job is running. Run `sync`, then `udisksctl unmount -b <dev>`, then `udisksctl power-off -b <dev>`. Show success or error.

### 5.6 Transfer
- Session folder: `<usb>/collect_<yyyyMMdd_hhmmss>/<pcId>/{logs,screenshots}/` and `<usb>/collect_<...>/manifest.json`. (This changes the destination text in the mock-up footer from `/media/usb/<hostname>/` to the session folder. Update the mock-up text during Phase 5.)
- `TransferEngine` keeps a queue of PCs and runs up to `maxParallel` at once (default 2). Each PC runs logs then screenshots.
- Per-PC states: Queued, Copying logs, Copying screenshots, Verifying, Done, Done with warnings, Failed, Cancelled, Skipped.
- Overall progress is weighted by bytes: `sum(bytes done) / sum(bytes planned)`. Show combined rate and a rough time remaining (moving average over about 10 seconds).
- Verify per PC: compare file count and total bytes at the destination with the planned list. Mismatch means "Done with warnings" and a line in the manifest.
- Cancel: send SIGTERM to every rsync child (and its ssh), wait up to 3 seconds, then SIGKILL. Keep `.partial` data so a later run can resume. The destination folder is left as is, and the manifest records "cancelled".
- Resume: starting a new copy creates a new session folder. Resuming into an old session is out of scope for v1.
- Manifest (`manifest.json`, written atomically with temp file + rename, updated after every PC): operator ID, app version, window start and end, start and finish times, per PC: status, file counts, bytes, errors, and the rsync exit codes.

### 5.7 Safety checks before Start
- Re-read USB free space, re-validate the estimate (needed bytes plus 2% margin), and confirm the destination is writable by creating the session folder.

### 5.8 Configuration (`~/.config/DataCollector/config.json`)
```json
{
  "sshUser": "user",
  "sshKeyPath": "~/.ssh/collector_key",
  "knownHostsPath": "~/.local/share/DataCollector/known_hosts",
  "maxParallel": 2,
  "paths": { "logs": ".app", "screenshots": "screenshot" },
  "logs": { "includeModifiedAfterEnd": true },
  "pcs": [
    { "id": "pc01", "host": "192.168.10.11" },
    { "id": "pc02", "host": "192.168.10.12" }
  ]
}
```
Ship `config.example.json` with all 6 entries. IPs above are placeholders from the mock-up.

---

## 6. GUI specification

Platform: Qt Quick Controls 2 with the Basic style, customized through a `Theme.qml` singleton. Minimum window 1024 x 720, content max width 1100 centered, layouts must reflow when resized. Every control has `Accessible.name`, keyboard order is logical, minimum hit target 44 px, visible focus ring (2 px `primary`).

Theme tokens:

| Token | Value |
|---|---|
| bg | #F3F4F6 |
| panel | #FFFFFF |
| border | #D9DDE3 |
| text | #1B2430 |
| muted | #5B6675 |
| primary | #1F5FD6 |
| success / success bg | #17603A / #E7F4EC |
| warning / warning bg | #7A4A00 / #FFF4E0 |
| error / error bg | #8A2A24 / #FBEAE8 |
| dark panel (login left) | #16212F |
| Fonts | Inter for text, JetBrains Mono for paths, IPs, file names. Fall back to system sans and monospace if not installed (do not fail). |
| Radii | 8 px controls, 10 px cards, 999 px chips |

### 6.1 Login
Two columns (stack on narrow widths).
- Left, dark panel: app name "Data Collector" with a download icon, headline "Collect logs and screenshots from every PC onto one USB drive.", one sentence of help, and two path rows ("Logs `/home/user/.app`", "Screenshots `/home/user/screenshot`"). These two paths come from config, not hard-coded.
- Right, white card (max width 400): title "Sign in", help text, Operator ID field, Passphrase field (with a lock icon note "Unlocks the collection key for this session only."), full-width Sign in button, footer "Need access? Contact your system administrator."
- States: default, busy (button disabled with spinner while unlocking), error (inline message under the passphrase field).

### 6.2 Sources
- Header bar (white, 64 px): app name left. Right: USB chip (dropdown), operator name, Sign out link.
- USB chip states: ready (green: "USB ready", mount point, free space, chevron), none (amber: "No USB drive detected"), menu open (white with primary border, listbox of drives with label, mount point, free space and a check on the selected one, divider, "Rescan drives").
- Title "Select sources" and one line of help.
- Banners (below the title, `role=alert` or `status`): amber "No USB drive detected" (text: plug in a USB drive, it will be picked up automatically, you can still choose a range and PCs); red "Not enough space on <drive>" (text: needs about X GB, drive has Y GB free, only N PCs fit, deselect PCs or choose a larger drive).
- Time card: Start, End pickers, preset buttons, "Rescan PCs" outlined button right-aligned.
- PCs table card: columns select, PC (name + IP in mono), Status chip, Logs, Screenshots, Total. The offline row is greyed and spans its three data cells with the unreachable message.
- Footer card: "N of M PCs selected", destination line, estimate text ("About X GB" left, "of Y GB free" right; red when over), progress bar, Start copy (primary, disabled grey when not allowed).

### 6.3 Progress
- Same header.
- Title "Copying to USB", subtitle with window, number of PCs and "Keep the USB drive plugged in until all copies finish."
- Overall card: large percent, "overall", copied of total GB, combined MB/s, "About N min left", 12 px progress bar.
- Table card: columns PC, Stage, Progress, Copied. Stage cell shows a chip (Done, Queued, Skipped, Failed) or, while copying, "Copying logs/screenshots, N MB/s" with the current file name in mono (ellipsized). Progress cell has an 8 px bar and the percent. Done bars are green, active bars blue, failed bars red.
- Footer actions: "Cancel copy" (outlined, red text, asks for confirmation), "Back to sources" link (only when idle or finished), "Eject USB safely" (disabled until no job is running and the copy is finished).
- Finished state (not in the mock-up, implement it): title "Copy finished" or "Copy finished with warnings", summary line, a "Show details" expander listing warnings from the manifest, "Eject USB safely" enabled.
- USB removed state: blocking inline error panel at the top of the screen with Retry and Cancel.

Status colors must never rely on color alone: every chip also carries text, and offline/failed rows carry a text message.

---

## 7. Interface contracts (write these into `docs/interfaces.md` in Phase 1)

Core types (C++17):

```cpp
enum class PcState { Unknown, Checking, Online, Offline, AuthFailed, HostKeyMismatch };
struct PcConfig  { QString id; QString host; int port = 22; };
struct FileEntry { QString relPath; qint64 size = 0; QDateTime mtime; };
struct PcListing { QString pcId; QVector<FileEntry> logs, screenshots; };
struct TimeWindow{ QDateTime start, end; bool isValid() const; };
struct UsbDrive  { QString device, label, mountPoint; qint64 freeBytes = 0, totalBytes = 0; };
enum class JobState { Queued, CopyingLogs, CopyingScreenshots, Verifying, Done, DoneWithWarnings, Failed, Cancelled, Skipped };
```

Controllers exposed to QML (properties, invokables, signals):
- `AuthController`: `state` (LoggedOut, Unlocking, LoggedIn), `error`, `operatorId`; `Q_INVOKABLE unlock(operatorId, passphrase)`, `lock()`.
- `SourcesController`: `pcModel` (roles: id, host, state, stateText, logsBytes, logsCount, shotsBytes, shotsCount, totalBytes, selected, selectable), `windowStart`, `windowEnd` (QDateTime, read/write), `selectedCount`, `selectedBytes`, `drives` (model), `currentDrive`, `driveState` (None, Ready, Multiple), `freeBytes`, `fits`, `canStart`, `busy`; `Q_INVOKABLE rescanPcs()`, `rescanDrives()`, `selectAll(bool)`, `applyPreset(name)`, `selectDrive(index)`, `startCopy()`.
- `TransferController`: `jobModel` (roles: id, host, stage, stageText, currentFile, rate, percent, copiedBytes, totalBytes, state), `overallPercent`, `copiedBytes`, `totalBytes`, `rate`, `etaSeconds`, `phase` (Idle, Running, Finished, FinishedWithWarnings, UsbRemoved, Cancelling), `warnings`; `Q_INVOKABLE cancel()`, `retry()`, `eject()`.

The frontend developer builds all QML against stub implementations of these controllers (`src/app/stubs/`) that emit realistic changing data, so the UI is complete before the real engine exists. Swap stubs for real controllers by changing one `qmlRegisterType` / context property block.

---

## 8. Work breakdown

### Phase 0: Spikes (developer, orchestrator reviews)
Output: `docs/spikes.md` with findings, sample outputs and a go/no-go per spike.
- S1: Run `rsync --list-only` (with and without `--no-human-readable`, and the `-n --out-format` variant) against a restricted `rrsync -ro` key. Record the exact output format, how it handles spaces, unicode and hidden directories, and the speed with 100,000 files.
- S2: Prove `ssh-agent -D` + `ssh-add` with `SSH_ASKPASS_REQUIRE=force` unlocks a passphrase key from a Qt `QProcess`, without a TTY or DISPLAY. Prove the agent dies when the parent exits.
- S3: Prove USB detection (removable filter, mount points) and eject with `udisksctl` on the target desktop.
- S4: Prove `--info=progress2,name1` output can be parsed reliably from `QProcess` (carriage-return framing) and gives a usable total with `--files-from`.
Acceptance: all four documented with evidence. If a spike fails, the orchestrator updates sections 4 to 5 and `docs/decisions.md` before Phase 1.

### Phase 1: Foundation (orchestrator + writer)
- Create the repo layout and CMake skeleton (core library, app, tests, QML resources). Builds an empty window.
- Write `docs/interfaces.md` from section 7, `docs/decisions.md`, and `config.example.json`.
- Add `Theme.qml` from the section 6 tokens.
Acceptance: `cmake --build` succeeds, an empty app launches, tests target runs (even with zero tests).

### Phase 2: Parallel build (run A to E at the same time)
- A. Core remote (developer): `ConfigStore`, `SshOptions`, `KeyAgent`, `HostProbe`, `RsyncLister` + parser, `TimeFilter`, `SpaceEstimator`.
  Acceptance: unit tests pass for parser (spaces, unicode, commas, symlinks, dirs, empty dir), filter (window edges, logs rule on and off, DST day, start >= end), estimator (margin, exact fit). HostProbe maps stderr and exit codes to all `PcState` values against the fake fleet.
- B. USB (developer): `UsbMonitor` and eject.
  Acceptance: unit tests with an injectable volume provider; manual test on the desktop passes for none, one, several drives, removal and re-insert, and eject.
- C. QML UI (frontend-developer): all views from section 6 including the three Sources states and the Progress finished and USB-removed states, with stub controllers.
  Acceptance: screenshot of each state matches the spec; keyboard-only run through Login, Sources and Progress works; no layout breakage between 1024 and 1920 px wide.
- D. Test harness (tester): `scripts/fake-fleet/` that brings up 6 fake PCs, each with sshd, the restricted key line and generated `.app` and `screenshot` trees with known mtimes (a few thousand files, a few large files, names with spaces and unicode). Prefer containers; fall back to one sshd on loopback with six users and `Host` aliases. Plus a fixture generator with a manifest of expected results per time window.
  Acceptance: `scripts/fake-fleet/up.sh` and `down.sh` work; `rsync` through the restricted key lists and copies; `rsync` outside `-ro` or any shell attempt is refused.
- E. Provisioning (developer or writer): `scripts/provision-pc.sh` (install sshd and rsync, create the restricted `authorized_keys` line, disable password auth, set NTP) and `scripts/setup-desktop.sh` (create key, distribute, verify fingerprints interactively, build `known_hosts`), plus `docs/provisioning.md` (timezone and NTP requirement, rollback steps).
  Acceptance: script dry-run mode prints every change; works against the fake fleet.

Phase 2 gate: build green, all unit tests green, reviewer pass on A and B.

### Phase 3: Engine and wiring (developer, frontend-developer)
- `TransferEngine`: queue, `maxParallel`, per-PC pipeline (logs, screenshots, verify), progress parsing, cancel, retry, `Manifest`.
  Acceptance: integration tests on the fake fleet cover full run, cancel mid-run, resume after kill, partial transfer (exit 23 via an unreadable file), host dropping mid-copy, USB removed mid-copy (simulated), full disk (small loop-mounted image).
- Controllers (`AuthController`, `SourcesController`, `TransferController`) replace the stubs. Models are `QAbstractListModel` subclasses and update in place (no full resets on progress ticks).
  Acceptance: manual end-to-end with the fake fleet through the real GUI.

### Phase 4: Hardening (tester, reviewer)
- Failure injection per section 10. Soak test: 6 PCs x 50,000 screenshots, GUI stays responsive and memory stays flat.
- Reviewer checklist (every item must be checked and reported):
  - Passphrase never logged, never written to disk, cleared after use.
  - No shell strings, no `sh -c`; all `QProcess` use argument lists.
  - Listed file names sanitized before `--files-from`; NUL separated.
  - No `StrictHostKeyChecking=no` anywhere.
  - All child processes killed on exit, sign out and cancel; no orphan `ssh-agent`.
  - No blocking calls on the GUI thread; no unbounded log or model growth.
  - Every user-visible error has a plain-language message and a next step.
  - Manifest written atomically.
Acceptance: reviewer report with zero open high-severity items.

### Phase 5: Packaging and docs (developer, writer)
- CMake install target and a `.desktop` entry; a `.deb` if packaging tooling is available, otherwise a documented install script.
- `docs/user-guide.md`: one page for operators (plug USB, open app, sign in, choose range, Start copy, Eject, what each message means).
- Update the mock-up footer text to the session folder destination.
Acceptance: a clean Ubuntu desktop VM can install, run the setup script against the fake fleet, and complete a collection following only the user guide.

---

## 9. Test strategy

- Unit tests (Qt Test) for every core class, no network, no display (`QT_QPA_PLATFORM=offscreen`).
- Integration tests against the fake fleet, run by a script, with deterministic fixtures and expected file lists per time window.
- GUI smoke tests: launch offscreen, drive the controllers through Login, Sources, Start, Finish and assert state and model contents.
- Manual checklist on real hardware before release (the orchestrator reports it, the user runs it): real PCs, real USB, large copy, USB pulled mid-copy, PC powered off mid-copy.

---

## 10. Error handling matrix

| Condition | Detection | User sees | App behaviour |
|---|---|---|---|
| PC unreachable | ssh exit 255, timeout | Offline chip, "Unreachable. Check power and network, then rescan." | Row not selectable |
| Auth failed | stderr "Permission denied" | "Key rejected" chip | Not selectable; Rescan available |
| Host key changed | stderr "REMOTE HOST IDENTIFICATION HAS CHANGED" | "Host key changed" chip + warning text | Never auto-accept; instruct admin to verify |
| Wrong passphrase | `ssh-add` fails | Inline error at Login | Count failures, throttle |
| No USB | monitor | amber banner | Start disabled |
| USB too small | estimate | red banner, red bar | Start disabled |
| USB removed mid-copy | volume disappears | blocking error panel | Pause jobs; Retry or Cancel |
| Disk full on USB | rsync write error | job Failed with message | Other jobs continue or stop based on free space |
| Partial transfer | rsync exit 23, 24 | "Done with warnings" + details | Listed in manifest |
| PC drops mid-copy | exit 30, 255 | job Failed, "Lost connection" | Other PCs continue; Retry per PC |
| Cancel | user | confirm dialog | Section 5.6 cancel sequence |
| Config invalid | startup | dialog naming the key | Exit cleanly |

---

## 11. Security notes

- Read-only, restricted key: the worst case for a stolen key file is read access to two folders, and only after the passphrase is known.
- Passphrase-protected key plus a private agent bound to the app lifetime. Do not use the system agent.
- Disable password authentication on every PC after key login works (done by `provision-pc.sh`).
- Logs of the app must not contain passphrases or the key path content. File names are fine.
- The USB drive carries the collected data unencrypted. Out of scope for v1, mention in the user guide.

---

## 12. Open questions (none block the build; defaults apply)

| # | Question | Default used |
|---|---|---|
| Q1 | Should the operator be able to copy only logs or only screenshots? | No in v1. Both are always copied. Add two checkboxes later if requested (P2). |
| Q2 | Add a "Fit to drive" button that deselects PCs until the estimate fits? | No in v1 (P2). The banner already states how many PCs fit. |
| Q3 | Is the Operator ID checked against a list? | No. Free text, audit label only. |
| Q4 | Real hostnames, IPs, SSH user, Ubuntu and Qt versions on the desktop? | Placeholders from the mock-up, user `user`, Qt 6.2+ APIs. Orchestrator should ask the user for the real values before Phase 5. |
| Q5 | Is it acceptable that logs modified after End are included? | Yes (section 5.4), configurable. |

---

## 13. Definition of done

- All three screens plus the NoUSB, LowSpace, Finished and UsbRemoved states are implemented and match section 6.
- A full collection from the fake fleet and from the real 6 PCs completes, with the manifest matching the files on the USB.
- All tests green, reviewer report clean, user guide and provisioning docs delivered.
- No passphrase, key or real address is in the repo or logs.
