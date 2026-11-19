# Changelog

## [0.3.0] - 2026-11-19

### Features
- **Optional Pictures (`Pictures/lightshot`) & Screen Recordings (`recordings`) Support**:
  - `src/core/Types.h`: Appended `JobState::CopyingRecordings = 9`, updated `PathsConfig` defaults (`screenshots = "Pictures/lightshot"`, `recordings = "recordings"`), added `PcConfig::supportsPictures`/`supportsRecordings`, and extended `PcListing`, `FilteredPc`, and `PcTransferResult` for recordings.
  - `src/core/ConfigStore.cpp`: Added parsing and validation for optional `paths.recordings` and per-PC `supportsPictures`/`supportsRecordings` booleans.
  - `src/core/TimeFilter.{h,cpp}`: Added `TimeFilter::extractFilenameDateTime()` for Lightshot (`yyyy-MM-dd_HH-mm-ss`) and screen recording (`yyyyMMdd_HHmmss`) basenames, and extended `TimeFilter::filterPc()` with `includePictures` and `includeRecordings` toggles.
  - `src/core/RsyncLister.{h,cpp}`: Extended `listPc()` to conditionally list screenshots and recordings based on `pc.supportsPictures`/`pc.supportsRecordings`, treating missing optional media directories (rsync exit 23) as empty lists.
  - `src/core/TransferEngine.{h,cpp}` & `src/core/Manifest.cpp`: Added `CopyingRecordings` transfer stage, monotonic byte tracking across logs/screenshots/recordings, recordings directory verification and `.partial` cleanup, and recordings fields in `manifest.json`.
  - `src/app/SourcesController.{h,cpp}` & `src/app/stubs/StubControllers.{h,cpp}`: Added `includePictures` (default `true`) and `includeRecordings` (default `false`) properties with instant zero-SSH re-filtering, exposed `recordingsBytes`/`recordingsCount`/`supportsPictures`/`supportsRecordings` model roles, and added `pc07` (media disabled) and sample recordings in `--demo` mode.
  - `config.example.json`, `docs/interfaces.md`: Updated default media paths, disabled pictures/recordings on `pc07`, and documented updated core and controller contracts.

## [0.2.0] - 2026-11-19

### Features
- **Per-PC Custom Log Layouts & Filename Timestamp Filtering**:
  - `src/core/Types.h`: Added `TimestampSource` (`Mtime`, `Filename`), `LogSourceConfig`, `PcConfig::logSources`, `FileEntry::sourceIndex`, and `resolveLogSources()` helper.
  - `src/core/ConfigStore.cpp`: Added parsing and validation for optional per-PC `logSources` arrays (validating `name`, safe relative `remotePath`/`destSubdir`, `timestampSource` values `"mtime"`/`"filename"`, and rejecting duplicate non-empty `destSubdir` entries).
  - `src/core/TimeFilter.{h,cpp}`: Added `TimeFilter::extractFilenameDate()` to extract `yyyyMMdd` dates from log basenames and updated `TimeFilter::filterPc()` to filter per log source using either filename date or filesystem mtime.
  - `src/core/RsyncLister.{h,cpp}`: Extended `listPc()` to list multiple log sources per PC in parallel, tagging entries with their `sourceIndex`, while waiting for all log sources and screenshots to complete.
  - `src/core/TransferEngine.{h,cpp}`: Extended `CopyingLogs` to iterate through each resolved `LogSourceConfig` sub-stage into `<sessionDir>/<pcId>/logs[/<destSubdir>]`, preserving smooth cumulative progress, partial-transfer warnings, and recursive `.partial` cleanup on verification.
  - `src/app/SourcesController.cpp`: Wired `resolveLogSources()` into `RsyncLister::listPc()` and `TimeFilter::filterPc()`.
  - `config.example.json`, `docs/interfaces.md`: Documented `LogSourceConfig`/`TimestampSource` contracts and added `pc07` with `syslog` (`.app/aesardp/syslog`) and `metadata` (`app/aesardp/metadata`) filename-filtered log sources.

## [0.1.0] - 2026-11-19

### Features
- **Core Library (`src/core/`)**:
  - `src/core/Types.h`: Core domain structs, enums (`PcState`, `JobState`, `AppConfig`, `PcConfig`, `FileEntry`, `PcListing`, `FilteredPc`, `UsbDrive`, `SpaceEstimate`, `TimeWindow`, `PcTransferResult`, `SessionManifest`), and state text/message helpers.
  - `src/core/ConfigStore.{h,cpp}`: JSON configuration loader, tilde path expansion, schema validation, and default path resolution (`--config`, `DC_CONFIG_PATH`, `~/.config/data-collector/config.json`).
  - `src/core/PathSanitizer.{h,cpp}`: Path normalization and traversal rejection for rsync file lists and relative paths.
  - `src/core/TimeFilter.{h,cpp}`: Pure time-window filtering for log files (with optional `includeModifiedAfterEnd` support) and timestamped screenshots (`YYYY-MM-DD_HH-MM-SS.png` with mtime fallback).
  - `src/core/SpaceEstimator.{h,cpp}`: Space estimation with safety margin and greedy fitting-PC count calculation.
  - `src/core/SshOptions.{h,cpp}`: Centralized SSH option construction enforcing `StrictHostKeyChecking=yes`, `BatchMode=yes`, `IdentitiesOnly=yes`, and custom `known_hosts` / agent socket handling.
  - `src/core/KeyAgent.{h,cpp}` & `scripts/askpass.sh`: Dedicated per-session `ssh-agent` lifecycle and non-blocking passphrase unlock via `SSH_ASKPASS` without logging or persisting passphrases.
  - `src/core/HostProbe.{h,cpp}`: Parallel SSH reachability probing and failure classification (`Online`, `Offline`, `AuthFailed`, `HostKeyFailed`, `Timeout`).
  - `src/core/RsyncLister.{h,cpp}`: Remote directory listing via `rsync --list-only` and structured `FileEntry` parsing.
  - `src/core/UsbMonitor.{h,cpp}`: Removable USB drive discovery via `QStorageInfo` and sysfs, free-space polling, removal detection, safe eject sequence (`sync` -> `udisksctl unmount` -> `udisksctl power-off`), and `DC_USB_OVERRIDE_PATH` test hook.
  - `src/core/TransferEngine.{h,cpp}` & `src/core/Manifest.{h,cpp}`: Parallel per-PC rsync transfers (`--files-from`, `--partial`, `--protect-args`), progress/rate/ETA tracking, post-transfer verification, USB-removal pause/resume, per-PC retry, clean cancellation, and atomic `manifest.json` generation.
- **Application Controllers & QML UI (`src/app/`, `src/qml/`)**:
  - `src/app/AuthController.{h,cpp}`: Operator login, key-agent unlock coordination, and failed-attempt throttling.
  - `src/app/SourcesController.{h,cpp}`: `PcListModel`, `DriveListModel`, parallel PC probing/listing, instant time-window filtering, USB space estimation, and writable session folder preparation.
  - `src/app/TransferController.{h,cpp}`: `JobListModel`, transfer session orchestration, USB removal handling, retry/cancel controls, and safe eject status reporting.
  - `src/app/stubs/StubControllers.{h,cpp}`: Self-contained demo mode controllers (`--demo`) for UI verification without SSH or config files.
  - `src/app/main.cpp`: CLI parsing (`--demo`, `--config`), configuration error window handling, controller wiring, and clean shutdown on quit.
  - `src/qml/`: Complete Qt Quick / QML 6.2 UI (`Main.qml`, `ConfigErrorWindow.qml`, `Theme.qml`, views, and reusable components).
- **Provisioning, Packaging & Testing (`scripts/`, `packaging/`, `tests/`)**:
  - `scripts/provision-pc.sh` & `scripts/setup-desktop.sh`: Fleet PC provisioning and operator desktop setup scripts.
  - `scripts/fake-fleet/`: Local multi-host `sshd` fake-fleet harness (`up.sh`, `down.sh`, `generate-fixtures.py`, `verify.sh`).
  - `packaging/data-collector.desktop`, `packaging/install.sh`, and CPack DEB rules in `CMakeLists.txt`.
  - `tests/unit/` and `tests/integration/`: Unit tests (`test_core_remote.cpp`, `test_transfer_engine.cpp`, `test_usb_monitor.cpp`) and end-to-end integration tests (`test_fake_fleet_integration.cpp`, `test_gui_smoke.cpp`).

### Fixes
- `CMakeLists.txt`: Corrected initial project version from `1.0.0` to `0.1.0` in accordance with pre-1.0 versioning rules.

### Docs
- `docs/decisions.md`, `docs/interfaces.md`, `docs/spikes.md`, `docs/progress.md`: Architecture decisions, core/UI contracts, spike verification notes, and milestone tracking.
- `docs/user-guide.md`, `docs/provisioning.md`, `config.example.json`: Operator usage guide, fleet provisioning documentation, and example configuration.
- `changelog.md`: Added initial `0.1.0` changelog.

### Chores
- `src/app/AuthController.{h,cpp}`, `src/app/SourcesController.{h,cpp}`, `src/app/TransferController.{h,cpp}`, `src/app/stubs/StubControllers.{h,cpp}`, `src/app/main.cpp`, `tests/integration/test_gui_smoke.cpp`: Standardized code style across the application layer (`mFoo` member naming, `kFoo` constants, `Q_DISABLE_COPY` on all `QObject`/model classes, explicit parameters with no default values on public APIs, forward declarations in headers, strict include ordering, and `nullptr` checks).
- `src/core/`: Refactored core library headers and implementations for strict style compliance (`mFoo` member naming, `Q_DISABLE_COPY`, RAII ownership, explicit parameters on public APIs, and include ordering).
