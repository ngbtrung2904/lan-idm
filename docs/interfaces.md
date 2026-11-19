# Data Collector — Interface Contracts (v1)

This document is the single source of truth between backend (`src/core/`, `src/app/`) and frontend (`src/qml/`).
Do not change any type, property, role, signal, or `Q_INVOKABLE` without updating this file first.

---

## 1. Core C++17 Types (`src/core/Types.h`)

Namespace: `dc`

```cpp
namespace dc {

enum class PcState {
    Unknown = 0,
    Checking,
    Online,
    Offline,
    AuthFailed,
    HostKeyMismatch
};

enum class JobState {
    Queued = 0,
    CopyingLogs,
    CopyingScreenshots,
    Verifying,
    Done,
    DoneWithWarnings,
    Failed,
    Cancelled,
    Skipped
};

enum class TimestampSource {
    Mtime = 0,
    Filename
};

struct LogSourceConfig {
    QString name;
    QString remotePath;
    QString destSubdir;
    TimestampSource timestampSource = TimestampSource::Mtime;
};

struct PcConfig {
    QString id;
    QString host;
    int port = 22;
    QVector<LogSourceConfig> logSources;
};

struct PathsConfig {
    QString logs = QStringLiteral(".app");
    QString screenshots = QStringLiteral("screenshot");
};

struct LogsConfig {
    bool includeModifiedAfterEnd = true;
};

struct AppConfig {
    QString sshUser = QStringLiteral("user");
    QString sshKeyPath;
    QString knownHostsPath;
    int maxParallel = 2;
    PathsConfig paths;
    LogsConfig logs;
    QVector<PcConfig> pcs;
};

struct FileEntry {
    QString relPath;
    qint64 size = 0;
    QDateTime mtime;
    int sourceIndex = 0;
};

struct PcListing {
    QString pcId;
    QVector<FileEntry> logs;
    QVector<FileEntry> screenshots;
};

struct TimeWindow {
    QDateTime start;
    QDateTime end;
    bool isValid() const {
        return start.isValid() && end.isValid() && start < end;
    }
};

struct FilteredPc {
    QString pcId;
    QVector<FileEntry> logs;
    QVector<FileEntry> screenshots;
    qint64 logsBytes = 0;
    qint64 shotsBytes = 0;
    qint64 totalBytes() const { return logsBytes + shotsBytes; }
};

struct UsbDrive {
    QString device;      // e.g. "/dev/sdb1"
    QString label;       // e.g. "Kingston 64 GB" (fallback to mountPoint basename)
    QString mountPoint;  // e.g. "/media/user/USB"
    qint64 freeBytes = 0;
    qint64 totalBytes = 0;
};

struct SpaceEstimate {
    qint64 neededBytes = 0;
    qint64 requiredWithMarginBytes = 0; // neededBytes + 2% margin
    qint64 freeBytes = 0;
    bool fits = false;
    int fittingPcCount = 0;
};

struct PcTransferResult {
    QString pcId;
    QString host;
    JobState state = JobState::Queued;
    int logsFilesPlanned = 0;
    int logsFilesCopied = 0;
    qint64 logsBytesCopied = 0;
    int shotsFilesPlanned = 0;
    int shotsFilesCopied = 0;
    qint64 shotsBytesCopied = 0;
    int logsRsyncExitCode = -1;
    int shotsRsyncExitCode = -1;
    QStringList errors;
};

} // namespace dc
```

---

## 2. QML Controller Contracts

Exposed to QML as context properties:
- `authController` (`AuthController*`)
- `sourcesController` (`SourcesController*`)
- `transferController` (`TransferController*`)

Both stub controllers (`src/app/stubs/`) and real controllers (`src/app/`) expose the exact same Q_OBJECT interface (shared base/interface or identical Q_PROPERTY / Q_INVOKABLE / Q_ENUM definitions).

### 2.1 `AuthController`

Enums (`Q_ENUM`):
- `AuthState`: `LoggedOut = 0`, `Unlocking = 1`, `LoggedIn = 2`

Properties:
- `int state` (READ, NOTIFY `stateChanged`) — `AuthState` value
- `QString error` (READ, NOTIFY `errorChanged`) — empty when no error
- `QString operatorId` (READ, NOTIFY `operatorIdChanged`)
- `int throttleSeconds` (READ, NOTIFY `throttleSecondsChanged`) — >0 during 30 s lockout after 5 consecutive failures
- `QString logsDisplayPath` (READ, CONSTANT) — e.g. `/home/user/.app` (derived from `config.sshUser` + `config.paths.logs`)
- `QString screenshotsDisplayPath` (READ, CONSTANT) — e.g. `/home/user/screenshot`

Invokables:
- `Q_INVOKABLE void unlock(const QString &operatorId, const QString &passphrase)`
- `Q_INVOKABLE void lock()`

Signals:
- `stateChanged()`
- `errorChanged()`
- `operatorIdChanged()`
- `throttleSecondsChanged()`

---

### 2.2 `SourcesController`

Enums (`Q_ENUM`):
- `DriveState`: `None = 0`, `Ready = 1`, `Multiple = 2`

Models (`QAbstractListModel*`):
1. `pcModel` — Roles:
   - `id` (QString, e.g. `"pc01"`)
   - `host` (QString, e.g. `"192.168.10.11"`)
   - `state` (int, `dc::PcState`)
   - `stateText` (QString, e.g. `"Online"`, `"Offline"`, `"Checking"`, `"Key rejected"`, `"Host key changed"`)
   - `statusMessage` (QString, e.g. `"Unreachable. Check power and network, then rescan."`)
   - `logsBytes` (qint64)
   - `logsCount` (int)
   - `shotsBytes` (qint64)
   - `shotsCount` (int)
   - `totalBytes` (qint64)
   - `selected` (bool)
   - `selectable` (bool — true only when `state == PcState::Online`)

2. `drives` — Roles:
   - `device` (QString)
   - `label` (QString)
   - `mountPoint` (QString)
   - `freeBytes` (qint64)
   - `totalBytes` (qint64)
   - `selected` (bool)

Properties:
- `QAbstractListModel* pcModel` (READ, CONSTANT)
- `QAbstractListModel* drives` (READ, CONSTANT)
- `QDateTime windowStart` (READ, WRITE, NOTIFY `windowChanged`)
- `QDateTime windowEnd` (READ, WRITE, NOTIFY `windowChanged`)
- `bool windowValid` (READ, NOTIFY `windowChanged`)
- `QString activePreset` (READ, NOTIFY `windowChanged`) — `"24h"`, `"7d"`, `"today"`, or `""`
- `int totalPcCount` (READ, NOTIFY `selectionChanged`)
- `int onlinePcCount` (READ, NOTIFY `selectionChanged`)
- `int selectedCount` (READ, NOTIFY `selectionChanged`)
- `bool allOnlineSelected` (READ, NOTIFY `selectionChanged`)
- `qint64 selectedBytes` (READ, NOTIFY `selectionChanged`)
- `int driveState` (READ, NOTIFY `drivesChanged`) — `DriveState`
- `QVariantMap currentDrive` (READ, NOTIFY `drivesChanged`) — keys: `device`, `label`, `mountPoint`, `freeBytes`, `totalBytes`
- `qint64 freeBytes` (READ, NOTIFY `drivesChanged`)
- `bool fits` (READ, NOTIFY `estimateChanged`)
- `int fittingPcCount` (READ, NOTIFY `estimateChanged`)
- `QString destinationPreview` (READ, NOTIFY `drivesChanged`) — e.g. `/media/usb/collect_<timestamp>/` or `not set`
- `bool canStart` (READ, NOTIFY `canStartChanged`)
- `bool busy` (READ, NOTIFY `busyChanged`)

Invokables:
- `Q_INVOKABLE void rescanPcs()`
- `Q_INVOKABLE void rescanDrives()`
- `Q_INVOKABLE void setPcSelected(int index, bool selected)`
- `Q_INVOKABLE void selectAll(bool selected)`
- `Q_INVOKABLE void applyPreset(const QString &name)` — `"24h"`, `"7d"`, `"today"`
- `Q_INVOKABLE void selectDrive(int index)`
- `Q_INVOKABLE void startCopy()`

Signals:
- `windowChanged()`
- `selectionChanged()`
- `drivesChanged()`
- `estimateChanged()`
- `canStartChanged()`
- `busyChanged()`
- `copyRequested()` — emitted when `startCopy()` passes safety checks and transitions to Progress view
- `startError(const QString &message)` — emitted if pre-start safety check fails

---

### 2.3 `TransferController`

Enums (`Q_ENUM`):
- `Phase`: `Idle = 0`, `Running = 1`, `Finished = 2`, `FinishedWithWarnings = 3`, `UsbRemoved = 4`, `Cancelling = 5`, `Cancelled = 6`

Models (`QAbstractListModel*`):
1. `jobModel` — Roles:
   - `id` (QString)
   - `host` (QString)
   - `stage` (int, `dc::JobState`)
   - `stageText` (QString, e.g. `"Done"`, `"Copying screenshots"`, `"Copying logs"`, `"Verifying"`, `"Queued"`, `"Skipped"`, `"Failed"`, `"Done with warnings"`, `"Cancelled"`)
   - `currentFile` (QString)
   - `rate` (double, bytes/sec)
   - `percent` (int, 0..100)
   - `copiedBytes` (qint64)
   - `totalBytes` (qint64)
   - `state` (int, `dc::JobState`)
   - `errorMessage` (QString)

Properties:
- `QAbstractListModel* jobModel` (READ, CONSTANT)
- `int overallPercent` (READ, NOTIFY `progressChanged`)
- `qint64 copiedBytes` (READ, NOTIFY `progressChanged`)
- `qint64 totalBytes` (READ, NOTIFY `progressChanged`)
- `double rate` (READ, NOTIFY `progressChanged`) — combined bytes/sec
- `int etaSeconds` (READ, NOTIFY `progressChanged`) — -1 if unknown
- `int phase` (READ, NOTIFY `phaseChanged`) — `Phase`
- `QStringList warnings` (READ, NOTIFY `warningsChanged`)
- `QString sessionPath` (READ, NOTIFY `sessionChanged`)
- `QString windowSummary` (READ, NOTIFY `sessionChanged`) — e.g. `"2026-11-10 08:00 to 2026-11-17 17:00"`
- `int selectedPcCount` (READ, NOTIFY `sessionChanged`)
- `bool canEject` (READ, NOTIFY `phaseChanged`)
- `QString ejectStatus` (READ, NOTIFY `ejectStatusChanged`)
- `bool ejectError` (READ, NOTIFY `ejectStatusChanged`)

Invokables:
- `Q_INVOKABLE void cancel()`
- `Q_INVOKABLE void retry()`
- `Q_INVOKABLE void retryPc(const QString &pcId)`
- `Q_INVOKABLE void eject()`
- `Q_INVOKABLE void resetToSources()`

Signals:
- `progressChanged()`
- `phaseChanged()`
- `warningsChanged()`
- `sessionChanged()`
- `ejectStatusChanged()`
- `backToSourcesRequested()`

