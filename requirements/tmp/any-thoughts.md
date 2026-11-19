# Implementation Plan: Multi-PC Data Collector & USB Exporter (`aesa-idm`)

## 1. System Overview & Objective
Develop a native Linux desktop application in **Qt 6.7.2 (C++17 + QML)** running on an **Ubuntu Desktop (`PC-01`)** that collects logs, CSV files, screenshots, and screen recordings from **7 Ubuntu PCs** (`PC-01` local desktop + `PC-02`..`PC-07` remote PCs over LAN) for a user-selected date and exports them onto a plugged-in **USB drive**.

### Core User Workflow
1. User plugs a USB drive into `PC-01` (Ubuntu Desktop) and opens `AesaIdm`.
2. Application detects the USB drive (and mounts/unmounts/ejects it natively in C++ via **Qt D-Bus + UDisks2** without `sudo`).
3. In the top bar, user selects the **Target Date**, enters the **SSH Key Passphrase**, selects the **USB Target**, and clicks **`↓ Download to USB`**.
4. Application unlocks the SSH private key in memory, scans the configured scattered directories (`logs`, `csv`, `screenshots`, `recordings`) across all 7 PCs in parallel, verifies available USB space, streams matching files onto the USB drive while computing SHA-256 digests (`manifest.sha256`), flushes kernel buffers (`::sync()`), and lets the user safely eject the USB drive.

---

## 2. Mandatory `.opencode/rules/` Compliance

All agents working on this repository **MUST** read and strictly obey `/home/trungnb/workspace/my-work/aesa-idm/.opencode/rules/*.md` before writing or reviewing code:

### 2.1. C/C++ Naming & Code Style (`.opencode/rules/code-styles.md` & `programming-style.md`)
- **Member variables:** `mFoo` (prefix `m` + PascalCase, e.g., `mHost`, `mBlockObjectPath`, `mSshSession`).
- **Local variables & parameters:** `fooBar` (camelCase).
- **Functions & methods:** `fooBar()` (camelCase).
- **Class & struct names:** `FooBar` (PascalCase).
- **Constants & enum values:** `kFooBar` (prefix `k` + PascalCase, e.g., `kChunkSizeBytes`, `TransferState::kActive`) or `ALL_CAPS` for preprocessor macros.
- **Namespaces:** `aesaidm` (lowercase).
- **Files:** `FooBar.h` / `FooBar.cpp` (PascalCase).
- **File Consolidation Rule:** **Do not extract code into new files unless explicitly asked or the code is reused by 2+ files.** Single-use helpers and private structs must be co-located in the file that uses them.
- **No Default Parameter Values on Public APIs:** Never use default parameter values in public method/function declarations (e.g., `explicit MainController(QObject *parent)` — caller must explicitly pass `nullptr` at call sites).
- **Strong Typing & `auto` Discipline:** Avoid `auto` when the concrete type is not immediately obvious from the right-hand side, and never use `auto` for function return types. Create proper `struct` and `enum class` types instead of `QVariant` maps or `void*`.
- **Single-Purpose Functions:** Write simple single-purpose functions — no multi-mode behavior and no boolean flag parameters that switch internal logic.
- **Memory & Casts:** Use RAII (`std::unique_ptr`, custom deleters for `ssh_session`/`sftp_session`/`ssh_key`) — no raw `new`/`delete` or manual `malloc`/`free`. Use `nullptr`, `override`, `final`, `enum class`, and explicit `static_cast`/`reinterpret_cast` (never C-style casts).
- **Include Order:** Group includes at the top of every file in order: (1) project headers, (2) third-party (`<QObject>`, `<libssh/libssh.h>`), (3) standard library (`<memory>`, `<vector>`).
- **No Unnecessary `this->`:** Omit `this->` unless resolving a name collision.

### 2.2. Qt & QML Guidelines (`.opencode/rules/qt-guidelines.md`)
- Use `Q_OBJECT` in all `QObject` subclasses and `Q_DISABLE_COPY` on all classes that manage resources or have identity.
- Signals must be emitted with **typed parameters** — avoid untyped `QVariant`. Use `Q_DECLARE_METATYPE` for custom structs passed across queued connections.
- Always use Qt's function-pointer `connect(&sender, &Sender::signalName, &receiver, &Receiver::slotName)` syntax (never `SIGNAL()`/`SLOT()` macros).
- **Threading:** Never block the GUI thread. Worker objects must live in their own thread or run via `QThreadPool`/`QtConcurrent`. Do not move `QObject`s that have children across threads. Communicate with the GUI thread via queued signals/slots or `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- **QML:**
  - Keep QML files focused; extract reusable visual primitives into `src/qml/components/`.
  - Prefer declarative `property` bindings over imperative JavaScript assignments.
  - Use `Connections` for cross-component signal handling rather than direct signal handlers on foreign objects.
  - Use C++ `QAbstractListModel` subclasses for dynamic table/list data (`DataSources`, `FileTransfers`, `PcNodes`).

### 2.3. Error Handling & Logging (`.opencode/rules/error-handling.md` & `code-quality.md`)
- **No Fallbacks Unless Requested:** Do **not** invent silent fallbacks (such as password fallback when SSH key unlocking fails, or silent fake USB directories). Raise/report errors explicitly with clear, actionable messages.
- **External Service Calls (SSH/SFTP & UDisks2 D-Bus):** Use bounded retries (e.g., 2 retries with structured warning logs), then report the last error with full context (node ID, host, port, path, and libssh/D-Bus error code/message).
- **Structured Logging:** Do not interpolate dynamic values into single unstructured strings when logging; log structured key-value fields (e.g., `node="PC-02" host="192.168.10.102" op="sftp_open" path="/var/aesa/recordings" err="..."`).
- **Zero Build/Lint Warnings:** Fix all compiler/build issues at the root cause; never delete functionality to silence an error.

### 2.4. Testing, Workflow & Changelog (`.opencode/rules/testing.md`, `workflow.md`, `changelog-versioning.md`)
- **Testing Philosophy:** Prefer **integration, end-to-end, or smoke tests** that validate real behavior (real config parsing, real date filtering on disk, real SFTP/local transfer and SHA-256 verification). Avoid fragile mock graphs. GUI components must only be tested end-to-end (never write unit tests for QML/view classes).
- **Git & Workflow:** Work on a feature branch (unless instructed otherwise), keep changes focused, and use non-interactive git commands (`git --no-pager diff`).
- **Changelog & Versioning:** After all features and tests pass, record all created/modified files and features in `changelog.md` under `## [0.1.0] - 2026-11-18` using standard categories (`Features`, `Fixes`, `Chores`, `Docs`). **Never set version `1.0.0`** without explicit user approval.

---

## 3. Technology Stack & Qt 6.7.2 Environment

- **OS:** Ubuntu Linux
- **Language:** C++17
- **Qt SDK Path:** `~/Qt/6.7.2/gcc_64` (`/home/trungnb/Qt/6.7.2/gcc_64`)
  - Qt Modules: `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `DBus`, `Concurrent`, `Network`, `Test`
- **SSH/SFTP Library:** `libssh` (`libssh-dev`)
- **USB Management:** `Qt6::DBus` communicating with Ubuntu's system `org.freedesktop.UDisks2` service + `QStorageInfo`

### CMake Configuration for `~/Qt/6.7.2/gcc_64`
Do **not** install `apt` `qt6-*` packages. Configure `CMakeLists.txt` to use `~/Qt/6.7.2/gcc_64` automatically and embed its `lib` directory in `RPATH`:
```cmake
cmake_minimum_required(VERSION 3.21)
project(aesa-idm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_PREFIX_PATH AND EXISTS "$ENV{HOME}/Qt/6.7.2/gcc_64")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.7.2/gcc_64")
endif()

set(CMAKE_INSTALL_RPATH "$ENV{HOME}/Qt/6.7.2/gcc_64/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus Concurrent Network Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH REQUIRED IMPORTED_TARGET libssh)
```
## 4. Repository Structure (Co-located per `.opencode/rules/code-styles.md`)

To obey `.opencode/rules/code-styles.md` Rule #1 (*"Do not extract code into new files unless explicitly asked or the code is reused by 2+ files"*), keep the file tree compact and cohesive:

```text
aesa-idm/
├── CMakeLists.txt
├── changelog.md
├── plan.md
├── config/
│   └── config.example.json
├── requirements/
│   ├── phase2b-mockup-comparison.png
│   └── aesa-idm-gui-mockup.png
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── AppTypes.h            # Shared structs & enum classes (NodeConfig, DataSourceRule, UsbDiskInfo, TransferEvent)
│   │   ├── ConfigLoader.h        # Loads/validates config.json & merges per-node path overrides
│   │   ├── ConfigLoader.cpp
│   │   ├── FileMatcher.h         # Pure functions for date_subfolder, mtime, and filename_date matching
│   │   ├── FileMatcher.cpp
│   │   ├── UsbService.h          # QtDBus UDisks2 mount/unmount/eject + QStorageInfo + UsbDeviceModel
│   │   ├── UsbService.cpp
│   │   ├── SshTransferSession.h  # RAII libssh + SFTP client (key unlock, remote scan, chunked download + SHA256)
│   │   ├── SshTransferSession.cpp
│   │   ├── ExportCoordinator.h   # QObject controller + QAbstractListModels (Nodes, DataSources, Transfers) + QThreadPool
│   │   └── ExportCoordinator.cpp
│   └── qml/
│       ├── Theme.qml             # Singleton/shared color & font constants matching requirements/aesa-idm-gui-mockup.png
│       ├── Main.qml              # 4-zone layout (TopBar, NodeStrip, Split Workspace, StatusBar)
│       └── components/
│           ├── NodePillCard.qml  # Reusable pill card for the 7 PCs in the secondary strip
│           └── ActionButton.qml  # Reusable primary/secondary/tab button matching the mockup
└── tests/
    └── tst_EndToEndExport.cpp    # Integration & E2E test validating config, date filtering, transfer & SHA-256 manifest
```

---

## 6. Configuration Schema (`config/config.example.json`)

All 7 PCs collect the same 4 conceptual categories (`logs`, `csv`, `screenshots`, `recordings`), stored in different directories on disk, with optional per-PC overrides:

```json
{
  "export_folder_prefix": "AESA_Export_",
  "max_concurrent_nodes": 4,
  "connection_timeout_sec": 5,
  "max_retries": 2,
  "ssh": {
    "port": 22,
    "username": "operator",
    "private_key_path": "/home/operator/.ssh/id_ed25519"
  },
  "data_sources": [
    {
      "id": "logs",
      "label": "System & App Logs",
      "enabled_by_default": true,
      "path": "/var/log/aesa",
      "recursive": true,
      "filter_mode": "mtime",
      "date_format": "yyyy-MM-dd",
      "extensions": ["*.log", "*.txt", "*.gz"],
      "exclude_patterns": ["*.part", "*.tmp", "*.lock"]
    },
    {
      "id": "csv",
      "label": "CSV Data Files",
      "enabled_by_default": true,
      "path": "/opt/aesa/data/csv",
      "recursive": true,
      "filter_mode": "date_subfolder",
      "date_format": "yyyy-MM-dd",
      "extensions": ["*.csv"],
      "exclude_patterns": ["*.part", "*.tmp"]
    },
    {
      "id": "screenshots",
      "label": "Screenshots",
      "enabled_by_default": true,
      "path": "/home/operator/Pictures/aesa_screenshots",
      "recursive": true,
      "filter_mode": "filename_date",
      "date_format": "yyyyMMdd",
      "extensions": ["*.png", "*.jpg", "*.jpeg", "*.bmp"],
      "exclude_patterns": ["*.part", "*.tmp"]
    },
    {
      "id": "recordings",
      "label": "Screen Recordings",
      "enabled_by_default": true,
      "path": "/var/aesa/recordings",
      "recursive": true,
      "filter_mode": "mtime",
      "date_format": "yyyy-MM-dd",
      "extensions": ["*.mp4", "*.mkv", "*.avi"],
      "exclude_patterns": ["*.part", "*.tmp", "*.active"]
    }
  ],
  "nodes": [
    { "id": "PC-01", "name": "PC-01 (Local)", "host": "127.0.0.1", "is_local": true },
    { "id": "PC-02", "name": "PC-02", "host": "192.168.10.102", "is_local": false },
    { "id": "PC-03", "name": "PC-03", "host": "192.168.10.103", "is_local": false },
    { "id": "PC-04", "name": "PC-04", "host": "192.168.10.104", "is_local": false },
    { "id": "PC-05", "name": "PC-05", "host": "192.168.10.105", "is_local": false },
    { "id": "PC-06", "name": "PC-06", "host": "192.168.10.106", "is_local": false },
    {
      "id": "PC-07",
      "name": "PC-07",
      "host": "192.168.10.107",
      "is_local": false,
      "data_sources_override": [
        {
          "id": "recordings",
          "path": "/mnt/external_ssd/recordings"
        }
      ]
    }
  ]
}
```

---

## 7. Backend Component Specifications

### 7.1. `UsbService` (Native C++ USB Mounting via `Qt6::DBus` + `UDisks2`)
- **D-Bus Service:** `"org.freedesktop.UDisks2"`, Object Manager path `"/org/freedesktop/UDisks2"`, interface `"org.freedesktop.DBus.ObjectManager"`.
- **Discovery (`refreshDevices()`):**
  - Query `GetManagedObjects` on `/org/freedesktop/UDisks2`.
  - Inspect block objects (`/org/freedesktop/UDisks2/block_devices/*`) implementing both `org.freedesktop.UDisks2.Block` and `org.freedesktop.UDisks2.Filesystem`.
  - Check the associated `org.freedesktop.UDisks2.Drive` object (`Drive` property): include devices where `Removable == true` or `ConnectionBus == "usb"`.
  - Read `IdLabel`, `IdType` (`exfat`, `vfat`, `ext4`, `ntfs`), `Device`, `Size`, and `MountPoints`.
  - When mounted, use `QStorageInfo(mountPoint)` to get `bytesAvailable()`.
- **Mounting (`mountPartition(const QString &blockObjectPath)`):**
  - Call `Mount(QVariantMap{})` on `org.freedesktop.UDisks2.Filesystem` with retry on transient D-Bus error (up to `kMaxRetries = 2`), logging structured warnings, then returning a typed `Result<QString, ErrorInfo>`.
- **Safe Unmount & Eject (`ejectPartition(const QString &blockObjectPath)`):**
  - Run `::sync()` on a worker thread, call `Unmount(QVariantMap{})` on `org.freedesktop.UDisks2.Filesystem`, and call `PowerOff(QVariantMap{})` on `org.freedesktop.UDisks2.Drive`.
- **Hotplug Detection:**
  - Connect to `InterfacesAdded` and `InterfacesRemoved` on `/org/freedesktop/UDisks2` so plugging in a USB updates the UI immediately.

### 7.2. `FileMatcher` (Pure Functions for Date & Pattern Filtering)
- Follow `.opencode/rules/programming-style.md` Rule #2 & #3: implement `FileMatcher` as pure functions inside `namespace aesaidm` taking explicit `const DataSourceRule &rule`, `const QDate &targetDate`, and file metadata (`fileName`, `relativePath`, `mtimeEpochSec`, `fileSizeBytes`).
- Exclude any file matching `rule.mExcludePatterns` (`*.part`, `*.tmp`, `*.lock`, `*.active`).
- Match extension against `rule.mExtensions`.
- Apply `FilterMode`:
  - `FilterMode::kDateSubfolder`: relative path starts with or contains directory segment `targetDate.toString(rule.mDateFormat)`.
  - `FilterMode::kMtime`: `mtimeEpochSec` is within `[QDateTime(targetDate, QTime(0,0,0)), QDateTime(targetDate, QTime(23,59,59,999))]`.
  - `FilterMode::kFilenameDate`: `fileName.contains(targetDate.toString(rule.mDateFormat))`.

### 7.3. `SshTransferSession` (RAII `libssh` + SFTP Client)
- Manage `ssh_session`, `sftp_session`, and `ssh_key` using `std::unique_ptr` with custom deleters (`SshSessionDeleter`, `SftpSessionDeleter`, `SshKeyDeleter`) — zero raw `new`/`delete` or manual cleanup leaks.
- **Key Unlock (`unlockPrivateKey(const QString &keyPath, const QString &passphrase)`):**
  - Calls `ssh_pki_import_privkey_file(keyPath.toUtf8().constData(), passphrase.toUtf8().constData(), nullptr, nullptr, &rawKey)`.
  - If unlocking fails, immediately return an explicit `ErrorInfo` (`ErrorCode::kInvalidPassphrase`) with structured context — **no silent fallback**.
- **Connection with Retries:**
  - Attempt connection up to `mMaxRetries` times with `mConnectionTimeoutSec` timeout. Log structured warnings on retry; on final failure, return `ErrorInfo` with host, port, and `ssh_get_error(session)`.
- **Chunked SFTP Download + On-the-Fly SHA-256:**
  - Stream remote file in `kChunkSizeBytes = 131072` (128 KB) chunks into `<usbDestPath>.part`.
  - Feed each chunk into `QCryptographicHash(QCryptographicHash::Sha256)`.
  - Flush and atomically rename `<usbDestPath>.part` to `<usbDestPath>`.

### 7.4. `ExportCoordinator` (Models, Thread Pool & Manifest)
- Exposes typed `QAbstractListModel`s to QML:
  - `NodeListModel` (7 PC pills in the top strip)
  - `DataSourceTableModel` (28 node/source rows in the left `Data Sources` table)
  - `TransferLogModel` (live streaming rows in the top-right `File Transfers` table)
- Pre-flight check verifies:
  1. USB partition is mounted and writable.
  2. Total matched bytes across reachable nodes fits within `QStorageInfo::bytesAvailable()`.
  3. If USB filesystem is `vfat`/`FAT32`, no matched file exceeds `4,294,967,295` bytes.
- Writes `<usbMountPoint>/AESA_Export_<yyyy-MM-dd>/manifest.sha256` and `export_summary.json`, then invokes `::sync()` on a background worker before marking the export complete.

---

## 8. Implementation Milestones

### Milestone 1: Project Skeleton & Config/Matcher Core
1. Create `CMakeLists.txt` targeting `~/Qt/6.7.2/gcc_64` and `libssh`.
2. Implement `AppTypes.h`, `ConfigLoader.h/.cpp`, and `FileMatcher.h/.cpp` following all `.opencode/rules/` naming (`mFoo`, `kFooBar`, no default parameters on public APIs, RAII, strong types).
3. Create `config/config.example.json`.

### Milestone 2: Native C++ USB Service & SSH/SFTP Engine
1. Implement `UsbService.h/.cpp` using `Qt6::DBus` (`org.freedesktop.UDisks2`) and `QStorageInfo`.
2. Implement `SshTransferSession.h/.cpp` using `libssh`/`sftp` with RAII smart pointers, retry-with-warning logic, and chunked SHA-256 streaming.
3. Implement `ExportCoordinator.h/.cpp` managing the 7 PCs (`PC-01` local copy + `PC-02`..`PC-07` SFTP copy) on `QThreadPool`.

### Milestone 3: QML GUI Matching `requirements/aesa-idm-gui-mockup.png`
1. Implement `Theme.qml`, `NodePillCard.qml`, `ActionButton.qml`, and `Main.qml` reproducing the exact dark palette, fonts, and 4-zone layout of [requirements/aesa-idm-gui-mockup.png](file:///home/trungnb/workspace/my-work/aesa-idm/requirements/aesa-idm-gui-mockup.png).
2. Wire `main.cpp` to register models and controllers with the QML engine.

### Milestone 4: End-to-End Verification & Changelog
1. Implement `tests/tst_EndToEndExport.cpp` exercising real config loading, real filesystem date filtering (`date_subfolder`, `mtime`, `filename_date`), local export to a target mount directory, and `manifest.sha256` verification.
2. Build with zero warnings/errors and run `ctest`:
   ```bash
   cmake -B build -DCMAKE_PREFIX_PATH="$HOME/Qt/6.7.2/gcc_64" -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure
   ```
3. Record all changes in `changelog.md` under `0.1.0` with date `2026-11-18` per `.opencode/rules/changelog-versioning.md`.