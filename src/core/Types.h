#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

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
    Skipped,
    CopyingRecordings = 9
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
    bool supportsPictures = true;
    bool supportsRecordings = true;
    QString sshUser;
};

struct PathsConfig {
    QString logs = QStringLiteral(".app");
    QString screenshots = QStringLiteral("Pictures/lightshot");
    QString recordings = QStringLiteral("recordings");
};

struct LogsConfig {
    bool includeModifiedAfterEnd = true;
};

struct UiConfig {
    QString theme = QStringLiteral("light");
};

struct AppConfig {
    QString sshUser = QStringLiteral("user");
    QString sshKeyPath;
    QString knownHostsPath;
    int maxParallel = 2;
    PathsConfig paths;
    LogsConfig logs;
    UiConfig ui;
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
    QVector<FileEntry> recordings;
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
    QVector<FileEntry> recordings;
    qint64 logsBytes = 0;
    qint64 shotsBytes = 0;
    qint64 recordingsBytes = 0;

    qint64 totalBytes() const { return logsBytes + shotsBytes + recordingsBytes; }
};

struct UsbDrive {
    QString device;
    QString label;
    QString mountPoint;
    qint64 freeBytes = 0;
    qint64 totalBytes = 0;
};

struct SpaceEstimate {
    qint64 neededBytes = 0;
    qint64 requiredWithMarginBytes = 0;
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
    int recordingsFilesPlanned = 0;
    int recordingsFilesCopied = 0;
    qint64 recordingsBytesCopied = 0;
    int logsRsyncExitCode = -1;
    int shotsRsyncExitCode = -1;
    int recordingsRsyncExitCode = -1;
    QStringList errors;
};

inline QString resolveSshUser(const PcConfig &pc, const QString &defaultSshUser) {
    return pc.sshUser.isEmpty() ? defaultSshUser : pc.sshUser;
}

inline QVector<LogSourceConfig> resolveLogSources(const PcConfig &pc,
                                                  const PathsConfig &defaultPaths) {
    if (!pc.logSources.isEmpty()) {
        return pc.logSources;
    }
    return {LogSourceConfig{QStringLiteral("logs"),
                            defaultPaths.logs,
                            QString(),
                            TimestampSource::Mtime}};
}

inline QString pcStateText(PcState state) {
    switch (state) {
    case PcState::Unknown:         return QStringLiteral("Unknown");
    case PcState::Checking:        return QStringLiteral("Checking");
    case PcState::Online:          return QStringLiteral("Online");
    case PcState::Offline:         return QStringLiteral("Offline");
    case PcState::AuthFailed:      return QStringLiteral("Key rejected");
    case PcState::HostKeyMismatch: return QStringLiteral("Host key changed");
    }
    return QStringLiteral("Unknown");
}

inline QString pcStateMessage(PcState state) {
    switch (state) {
    case PcState::Offline:
        return QStringLiteral("Unreachable. Check power and network, then rescan.");
    case PcState::AuthFailed:
        return QStringLiteral("Key rejected by PC. Verify the authorized_keys entry, then rescan.");
    case PcState::HostKeyMismatch:
        return QStringLiteral("Host key changed! Contact your administrator to verify the PC fingerprint.");
    default:
        return {};
    }
}

inline QString jobStateText(JobState state) {
    switch (state) {
    case JobState::Queued:             return QStringLiteral("Queued");
    case JobState::CopyingLogs:        return QStringLiteral("Copying logs");
    case JobState::CopyingScreenshots: return QStringLiteral("Copying screenshots");
    case JobState::CopyingRecordings:  return QStringLiteral("Copying recordings");
    case JobState::Verifying:          return QStringLiteral("Verifying");
    case JobState::Done:               return QStringLiteral("Done");
    case JobState::DoneWithWarnings:   return QStringLiteral("Done with warnings");
    case JobState::Failed:             return QStringLiteral("Failed");
    case JobState::Cancelled:          return QStringLiteral("Cancelled");
    case JobState::Skipped:            return QStringLiteral("Skipped");
    }
    return QStringLiteral("Unknown");
}

} // namespace dc

Q_DECLARE_METATYPE(dc::PcState)
Q_DECLARE_METATYPE(dc::JobState)
Q_DECLARE_METATYPE(dc::TimestampSource)
Q_DECLARE_METATYPE(dc::LogSourceConfig)
Q_DECLARE_METATYPE(dc::FileEntry)
Q_DECLARE_METATYPE(dc::PcListing)
Q_DECLARE_METATYPE(dc::UsbDrive)

