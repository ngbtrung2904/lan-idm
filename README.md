# Data Collector

**Data Collector** is a Linux desktop application (Qt 6 QML + C++17) that pulls application logs, pictures, and screen recordings from headless Linux PCs (`pc01`–`pc07`) onto a removable USB drive using key-based SSH and `rsync` — with **no custom daemon or agent running on the remote PCs**.

---

## Key Features

- **Zero custom agent on remote PCs:** Uses standard OpenSSH + `rsync` locked down with `rrsync -ro /home/<user>` (`restrict` in `authorized_keys`).
- **Session-scoped key unlock:** Runs a private `ssh-agent` owned by the application so the operator enters the key passphrase once per session.
- **Desktop-side time filtering & custom PC layouts:**
  - Supports standard PCs (`pc01`–`pc06`: `.app` logs, optional `Pictures/lightshot` pictures, optional `recordings/` screen recordings).
  - Supports per-PC custom log layouts (`pc07`: `.app/aesardp/syslog` and `app/aesardp/metadata` with `yyyyMMdd` filename-date filtering and media options disabled).
  - Supports per-PC `sshUser` overrides in `config.json`.
- **Auto-detected USB target & safe eject:** Polls removable mounts under `/media` and `/run/media`, checks capacity (with a 2% safety margin), writes into `<usb>/collect_<yyyyMMdd_hhmmss>/` with an atomic `manifest.json`, and supports safe `udisksctl` unmount + power-off.
- **Resilient transfers:** Bounded concurrency (`maxParallel`, default `2`), `--partial-dir` resumability, USB removal pause/retry, per-PC retry, and clean cancellation.

---

## Repository Layout

```text
.
├── CMakeLists.txt              # Build, install, and CPack (.deb) configuration (v0.3.0)
├── CMakePresets.json           # Qt 6.7.2 debug / analysis presets
├── config.example.json         # Example 7-PC configuration template
├── changelog.md                # Release history
├── docs/                       # Documentation (see Documentation Index below)
├── packaging/                  # .desktop entry and install.sh helper
├── scripts/
│   ├── askpass.sh              # SSH_ASKPASS helper for private ssh-agent
│   ├── provision-pc.sh         # Headless PC provisioning script (OpenSSH, rrsync, tz)
│   ├── setup-desktop.sh        # Desktop setup script (key, known_hosts, config.json)
│   └── fake-fleet/             # Local 6-sshd test fleet & deterministic fixture generator
├── src/
│   ├── core/                   # C++17 core library (ConfigStore, KeyAgent, RsyncLister,
│   │                           #   TimeFilter, SpaceEstimator, UsbMonitor, TransferEngine, Manifest)
│   ├── app/                    # Qt Quick controllers, models, main.cpp, and --demo stubs
│   └── qml/                    # Qt Quick Controls 2 UI (Login, Sources, Progress, Theme)
└── tests/
    ├── unit/                   # Qt Test unit suites (core remote, USB monitor, transfer engine)
    └── integration/            # Fake-fleet integration, 300k soak, and offscreen GUI smoke tests
```

---

## Documentation Index

| Document | Audience | Summary |
|---|---|---|
| [docs/quick-start-guide.md](docs/quick-start-guide.md) | Operators | Two-part walkthrough: Part A (test with 1 PC) and Part B (daily multi-PC collection). |
| [docs/user-guide.md](docs/user-guide.md) | Operators | Full operator manual: sign-in, time range & media toggles, progress/eject, status & error table, security note, and **desktop/remote PC rollback steps**. |
| [docs/quick-start-provisioning.md](docs/quick-start-provisioning.md) | Administrators | Phased rollout checklist (1 PC test → all 7 PCs → SSH password lockdown → emergency recovery). |
| [docs/provisioning.md](docs/provisioning.md) | Administrators | Complete provisioning reference: prerequisites, PTP & timezone alignment, `setup-desktop.sh` / `provision-pc.sh` flags, `rrsync -ro` verification, and key rotation/rollback. |
| [docs/easy-biz.md](docs/easy-biz.md) | Administrators / Operators | Practical Q&A: `--user` vs per-PC `user@host`, why `systemd-timesyncd` is omitted for PTP, operator login credentials, end-to-end PC impacts, and why `collector_key` lives in `~/.local/share/DataCollector/`. |
| [docs/interfaces.md](docs/interfaces.md) | Developers | Core C++17 types (`Types.h`) and QML controller/model contracts (`AuthController`, `SourcesController`, `TransferController`). |
| [docs/decisions.md](docs/decisions.md) | Developers | Architecture & implementation decisions (`D1`–`D15`). |
| [docs/spikes.md](docs/spikes.md) | Developers | Phase 0 technical spike findings (`rrsync`, `ssh-agent` + `askpass`, USB/`udisksctl`, `progress2`). |
| [docs/progress.md](docs/progress.md) | Project Tracking | Phase 0–5 delivery status log. |
| [changelog.md](changelog.md) | All | Versioned summary of features, fixes, docs, and chores (`0.1.0` → `0.3.0`). |

---

## Quick Start

### 1. Build & Test

Requires Ubuntu 22.04+ / 24.04+, CMake 3.21+, C++17 compiler, Qt 6.2+ (`Core`, `Quick`, `QuickControls2`, `Test`), `openssh-client`, `rsync`, and `udisks2`.

```bash
# Configure and build (see CMakePresets.json for QT_VERSION / CMAKE_PREFIX_PATH)
cmake --preset gcc-debug
cmake --build --preset gcc-debug

# Run unit tests
ctest --preset gcc-debug --output-on-failure

# Run full fake-fleet integration, 300k soak, and offscreen GUI smoke tests
./tests/integration/run_integration.sh
```

### 2. Preview UI in Demo Mode (No Fleet Required)

```bash
./build/Desktop_Qt_6_7_2-Debug/src/app/data-collector --demo
```

### 3. Install

```bash
# Preview install steps
./packaging/install.sh --dry-run

# Build Release and install to /usr/local (or pass --prefix / --destdir)
./packaging/install.sh
```

To generate a `.deb` package from a configured build directory:

```bash
cpack --config build/Desktop_Qt_6_7_2-Debug/CPackConfig.cmake
```

### 4. Provision Desktop & Remote PCs

Preview first with `--dry-run`, keeping password login enabled during initial testing (`--keep-password-auth`) and leaving existing PTP/timezones untouched if already configured (`--skip-timezone`):

```bash
./scripts/setup-desktop.sh \
  --skip-timezone \
  --keep-password-auth \
  --hosts pc01:user01@192.168.10.11,pc02:user02@192.168.10.12 \
  --dry-run
```

See [docs/quick-start-provisioning.md](docs/quick-start-provisioning.md) and [docs/provisioning.md](docs/provisioning.md) for the full rollout, `pc07` `config.json` customization, and rollback instructions.

