#pragma once

#include "Types.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <memory>
#include <vector>

class QProcess;
class QTemporaryDir;

namespace dc {

enum class EnginePhase {
    Idle = 0,
    Running,
    Finished,
    FinishedWithWarnings,
    UsbRemoved,
    Cancelling,
    Cancelled
};

struct TransferJobSpec {
    PcConfig pc;
    FilteredPc files;
    bool skipped = false;
    QString skipReason;
};

struct ProgressLine {
    bool isProgress = false;
    qint64 bytesDone = 0;
    int percent = 0;
    double rateBytesPerSec = 0.0;
    QString currentFile;

    static ProgressLine parseOutputLine(const QString &line);
};

struct JobStatus {
    QString pcId;
    QString host;
    JobState state = JobState::Queued;
    QString currentFile;
    double rateBytesPerSec = 0.0;
    int percent = 0;
    qint64 copiedBytes = 0;
    qint64 totalBytes = 0;
    QString errorMessage;
    PcTransferResult result;
};

class TransferEngine : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(TransferEngine)

public:
    explicit TransferEngine(QObject *parent);
    ~TransferEngine() override;

    static ProgressLine parseOutputLine(const QString &line);

    void startSession(const AppConfig &config,
                      const QString &agentSocket,
                      const QString &operatorId,
                      const TimeWindow &window,
                      const QString &sessionDir,
                      const QVector<TransferJobSpec> &jobs,
                      const QString &appVersion);

    void cancel();
    void pauseForUsbRemoved();
    void retryAfterUsbReturned();
    void retryPc(const QString &pcId);
    void shutdownNow();

    EnginePhase phase() const;
    QString sessionDir() const;
    int jobCount() const;
    JobStatus jobAt(int index) const;
    QVector<JobStatus> jobs() const;

    int overallPercent() const;
    qint64 copiedBytes() const;
    qint64 totalBytes() const;
    double combinedRate() const;
    int etaSeconds() const;
    QStringList warnings() const;

signals:
    void phaseChanged(dc::EnginePhase phase);
    void jobUpdated(int index);
    void overallProgressChanged();
    void warningsChanged();

private:
    struct InternalJob {
        TransferJobSpec spec;
        JobStatus status;
        std::unique_ptr<QProcess> process;
        QByteArray stdoutBuffer;
        QString stderrText;
        bool diskFullDetected = false;
        qint64 logsProgressBytes = 0;
        qint64 shotsProgressBytes = 0;
        QVector<LogSourceConfig> resolvedLogSources;
        int logsSubStageIndex = 0;
        qint64 logsCompletedBytes = 0;
        qint64 currentSubStagePlannedBytes = 0;
        int worstLogsExitCode = 0;
    };

    void scheduleNext();
    void startJob(int index);
    void startNextLogsSubStage(int index);
    void startStage(int index, bool isLogs, int logsSourceIndex, const QVector<FileEntry> &entries);
    void appendStderrChunk(int index, const QString &chunk);
    void handleProcessOutput(int index, QProcess *proc);
    void handleProcessFinished(int index, QProcess *proc, bool isLogs,
                               int exitCode, bool normalExit);
    void handleProcessFailedToStart(int index, QProcess *proc, bool isLogs);
    void verifyJob(int index);
    void finishVerifyJob(quint64 generation, int index,
                         int logsCount, qint64 logsBytes,
                         int shotsCount, qint64 shotsBytes);
    void failJob(int index, const QString &message, const QString &technicalDetail);
    void finalizeCancel();
    void checkSessionFinished();
    void writeManifest();
    void setPhase(EnginePhase phase);
    void recomputeOverallProgress(bool recordEtaSample);
    void addWarning(const QString &warning);
    int activeJobCount() const;
    void retireProcessAsync(std::unique_ptr<QProcess> proc, int killAfterMs);
    void eraseStoppingProcess(QProcess *proc);

    static QString normalizeRemoteDir(const QString &relDir);
    static QString phaseToManifestStatus(EnginePhase phase);
    static QString classifyRsyncFailure(int exitCode,
                                        bool normalExit,
                                        const QString &stderrText,
                                        bool diskFullDetected);
    static void scanDirectoryStats(const QString &dirPath, int *fileCount, qint64 *totalBytes);

    AppConfig mConfig;
    QString mAgentSocket;
    QString mOperatorId;
    QString mAppVersion;
    TimeWindow mWindow;
    QString mSessionDir;
    QDateTime mStartedAt;
    QDateTime mFinishedAt;
    std::unique_ptr<QTemporaryDir> mTempDir;

    EnginePhase mPhase = EnginePhase::Idle;
    quint64 mSessionGeneration = 0;
    std::vector<InternalJob> mJobs;
    std::vector<std::unique_ptr<QProcess>> mStoppingProcesses;
    QStringList mWarnings;

    int mOverallPercent = 0;
    qint64 mCopiedBytes = 0;
    qint64 mTotalBytes = 0;
    double mCombinedRate = 0.0;
    int mEtaSeconds = -1;

    QElapsedTimer mEtaTimer;
    QVector<QPair<qint64, qint64>> mEtaSamples;
    QTimer mCancelKillTimer;
};

} // namespace dc

Q_DECLARE_METATYPE(dc::EnginePhase)
