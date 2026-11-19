#include "TransferEngine.h"

#include "Manifest.h"
#include "PathSanitizer.h"
#include "SshOptions.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QRunnable>
#include <QTemporaryDir>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <utility>

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace dc {

namespace {

constexpr int kMaxStderrChars = 64 * 1024;
constexpr int kCancelKillIntervalMs = 3000;
constexpr int kKillTimeoutMs = 500;
constexpr int kUsbPauseKillTimeoutMs = 1000;
constexpr qint64 kEtaWindowMs = 10000;
constexpr qint64 kMinEtaDeltaMs = 500;

void configureRsyncChildProcess(QProcess *proc) {
#ifdef Q_OS_LINUX
    proc->setChildProcessModifier([]() {
        ::signal(SIGTERM, SIG_DFL);
        ::setpgid(0, 0);
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    });
#else
    Q_UNUSED(proc);
#endif
}

void signalProcessGroup(QProcess *proc, int sig) {
#ifdef Q_OS_LINUX
    if (!proc) {
        return;
    }
    const qint64 pid = proc->processId();
    if (pid > 0) {
        const pid_t childPgid = ::getpgid(static_cast<pid_t>(pid));
        if (childPgid > 0 && childPgid != ::getpgrp()) {
            ::kill(-childPgid, sig);
        }
    }
#else
    Q_UNUSED(proc);
    Q_UNUSED(sig);
#endif
}

void terminateSync(QProcess *proc, QObject *receiver) {
    if (!proc) {
        return;
    }
    proc->disconnect(receiver);
    if (proc->state() != QProcess::NotRunning) {
#ifdef Q_OS_LINUX
        signalProcessGroup(proc, SIGTERM);
#endif
        proc->terminate();
        if (!proc->waitForFinished(kKillTimeoutMs)) {
#ifdef Q_OS_LINUX
            signalProcessGroup(proc, SIGKILL);
#endif
            proc->kill();
            proc->waitForFinished(kKillTimeoutMs);
        }
    }
}

double rateUnitMultiplier(const QString &unit) {
    if (unit == QLatin1String("B/s")) {
        return 1.0;
    }
    if (unit == QLatin1String("kB/s") || unit == QLatin1String("KB/s")) {
        return 1024.0;
    }
    if (unit == QLatin1String("MB/s")) {
        return 1024.0 * 1024.0;
    }
    if (unit == QLatin1String("GB/s")) {
        return 1024.0 * 1024.0 * 1024.0;
    }
    if (unit == QLatin1String("TB/s")) {
        return 1024.0 * 1024.0 * 1024.0 * 1024.0;
    }
    return 1.0;
}

} // namespace

// ---------------------------------------------------------------------------
// ProgressLine
// ---------------------------------------------------------------------------

ProgressLine ProgressLine::parseOutputLine(const QString &line) {
    ProgressLine out;

    QString cleaned = line;
    while (cleaned.endsWith(QLatin1Char('\r')) || cleaned.endsWith(QLatin1Char('\n'))) {
        cleaned.chop(1);
    }

    const QString trimmed = cleaned.trimmed();
    if (trimmed.isEmpty()) {
        return out;
    }

    static const QRegularExpression kProgressRegex(
        QStringLiteral("^\\s*([\\d,]+)\\s+(\\d+)%\\s+([\\d.]+)([kKMGT]?B/s)\\s+(\\d+:\\d+:\\d+)"));

    const QRegularExpressionMatch match = kProgressRegex.match(cleaned);
    if (match.hasMatch()) {
        QString bytesStr = match.captured(1);
        bytesStr.remove(QLatin1Char(','));
        bool bytesOk = false;
        const qint64 bytes = bytesStr.toLongLong(&bytesOk);

        bool pctOk = false;
        const int pct = match.captured(2).toInt(&pctOk);

        bool rateOk = false;
        const double rateVal = match.captured(3).toDouble(&rateOk);
        const QString unit = match.captured(4);

        if (bytesOk && pctOk && rateOk) {
            out.isProgress = true;
            out.bytesDone = qMax<qint64>(0, bytes);
            out.percent = qBound(0, pct, 100);
            out.rateBytesPerSec = qMax(0.0, rateVal * rateUnitMultiplier(unit));
            return out;
        }
    }

    // Ignore directory lines (ending with '/')
    if (trimmed.endsWith(QLatin1Char('/'))) {
        return out;
    }

    out.currentFile = cleaned;
    return out;
}

// ---------------------------------------------------------------------------
// TransferEngine
// ---------------------------------------------------------------------------

TransferEngine::TransferEngine(QObject *parent)
    : QObject(parent) {
    mCancelKillTimer.setSingleShot(true);
    mCancelKillTimer.setInterval(kCancelKillIntervalMs);
    connect(&mCancelKillTimer, &QTimer::timeout, this, [this]() {
        for (InternalJob &job : mJobs) {
            if (job.process && job.process->state() != QProcess::NotRunning) {
#ifdef Q_OS_LINUX
                signalProcessGroup(job.process.get(), SIGKILL);
#endif
                job.process->kill();
            }
        }
    });
}

TransferEngine::~TransferEngine() {
    shutdownNow();
}

ProgressLine TransferEngine::parseOutputLine(const QString &line) {
    return ProgressLine::parseOutputLine(line);
}

EnginePhase TransferEngine::phase() const {
    return mPhase;
}

QString TransferEngine::sessionDir() const {
    return mSessionDir;
}

int TransferEngine::jobCount() const {
    return static_cast<int>(mJobs.size());
}

JobStatus TransferEngine::jobAt(int index) const {
    if (index < 0 || index >= static_cast<int>(mJobs.size())) {
        return {};
    }
    return mJobs.at(static_cast<size_t>(index)).status;
}

QVector<JobStatus> TransferEngine::jobs() const {
    QVector<JobStatus> list;
    list.reserve(static_cast<int>(mJobs.size()));
    for (const InternalJob &job : mJobs) {
        list.append(job.status);
    }
    return list;
}

int TransferEngine::overallPercent() const {
    return mOverallPercent;
}

qint64 TransferEngine::copiedBytes() const {
    return mCopiedBytes;
}

qint64 TransferEngine::totalBytes() const {
    return mTotalBytes;
}

double TransferEngine::combinedRate() const {
    return mCombinedRate;
}

int TransferEngine::etaSeconds() const {
    return mEtaSeconds;
}

QStringList TransferEngine::warnings() const {
    return mWarnings;
}

QString TransferEngine::normalizeRemoteDir(const QString &relDir) {
    QString trimmed = relDir.trimmed();
    while (trimmed.startsWith(QLatin1Char('/'))) {
        trimmed.remove(0, 1);
    }
    if (trimmed.isEmpty()) {
        return QStringLiteral("./");
    }
    if (!trimmed.endsWith(QLatin1Char('/'))) {
        trimmed.append(QLatin1Char('/'));
    }
    return trimmed;
}

QString TransferEngine::phaseToManifestStatus(EnginePhase phase) {
    switch (phase) {
    case EnginePhase::Idle:                 return QStringLiteral("idle");
    case EnginePhase::Running:              return QStringLiteral("running");
    case EnginePhase::Finished:             return QStringLiteral("finished");
    case EnginePhase::FinishedWithWarnings: return QStringLiteral("finished_with_warnings");
    case EnginePhase::UsbRemoved:           return QStringLiteral("usb_removed");
    case EnginePhase::Cancelling:           return QStringLiteral("running");
    case EnginePhase::Cancelled:            return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

QString TransferEngine::classifyRsyncFailure(int exitCode,
                                             bool normalExit,
                                             const QString &stderrText,
                                             bool diskFullDetected) {
    if (diskFullDetected
        || stderrText.contains(QStringLiteral("No space left on device"), Qt::CaseInsensitive)
        || stderrText.contains(QStringLiteral("write failed"), Qt::CaseInsensitive)) {
        return QStringLiteral("USB drive is full. Free space or use a larger USB drive, then retry.");
    }
    if (normalExit && (exitCode == 30 || exitCode == 255)) {
        return QStringLiteral("Lost connection to PC. Check power and network, then click Retry.");
    }
    if (!normalExit) {
        return QStringLiteral("Transfer interrupted unexpectedly. Click Retry to try again.");
    }
    return QStringLiteral("File transfer failed. Check the PC and USB drive, then click Retry.");
}

void TransferEngine::scanDirectoryStats(const QString &dirPath, int *fileCount, qint64 *totalBytes) {
    int count = 0;
    qint64 bytes = 0;

    if (QDir(dirPath).exists()) {
        QDirIterator it(dirPath, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        const QString cleanRoot = QDir::cleanPath(dirPath);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            const QString rel = QDir(cleanRoot).relativeFilePath(fi.absoluteFilePath());
            const QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            if (parts.contains(QStringLiteral(".partial"))) {
                continue;
            }
            ++count;
            bytes += qMax<qint64>(0, fi.size());
        }
    }

    if (fileCount) {
        *fileCount = count;
    }
    if (totalBytes) {
        *totalBytes = bytes;
    }
}

void TransferEngine::retireProcessAsync(std::unique_ptr<QProcess> proc, int killAfterMs) {
    if (!proc) {
        return;
    }
    QProcess *raw = proc.get();
    raw->disconnect(this);
    mStoppingProcesses.push_back(std::move(proc));

    if (raw->state() == QProcess::NotRunning) {
        QMetaObject::invokeMethod(
            this, [this, raw]() { eraseStoppingProcess(raw); }, Qt::QueuedConnection);
        return;
    }

    connect(raw, &QProcess::finished, this, [this, raw](int, QProcess::ExitStatus) {
        QMetaObject::invokeMethod(
            this, [this, raw]() { eraseStoppingProcess(raw); }, Qt::QueuedConnection);
    });
#ifdef Q_OS_LINUX
    signalProcessGroup(raw, SIGTERM);
#endif
    raw->terminate();
    QPointer<QProcess> guard(raw);
    QTimer::singleShot(killAfterMs, this, [guard]() {
        if (guard && guard->state() != QProcess::NotRunning) {
#ifdef Q_OS_LINUX
            signalProcessGroup(guard.data(), SIGKILL);
#endif
            guard->kill();
        }
    });
}

void TransferEngine::eraseStoppingProcess(QProcess *proc) {
    const auto it = std::find_if(mStoppingProcesses.begin(), mStoppingProcesses.end(),
                                 [proc](const std::unique_ptr<QProcess> &item) {
                                     return item.get() == proc;
                                 });
    if (it != mStoppingProcesses.end()) {
        mStoppingProcesses.erase(it);
    }
}

void TransferEngine::startSession(const AppConfig &config,
                                  const QString &agentSocket,
                                  const QString &operatorId,
                                  const TimeWindow &window,
                                  const QString &sessionDir,
                                  const QVector<TransferJobSpec> &jobs,
                                  const QString &appVersion) {
    shutdownNow();

    mConfig = config;
    mAgentSocket = agentSocket;
    mOperatorId = operatorId;
    mAppVersion = appVersion;
    mWindow = window;
    mSessionDir = sessionDir;
    mStartedAt = QDateTime::currentDateTime();
    mFinishedAt = QDateTime();
    mWarnings.clear();
    mEtaSamples.clear();
    mEtaTimer.start();

    mTempDir = std::make_unique<QTemporaryDir>();

    QDir().mkpath(mSessionDir);

    mJobs.clear();
    mJobs.reserve(static_cast<size_t>(jobs.size()));
    for (const TransferJobSpec &spec : jobs) {
        InternalJob job;
        job.spec = spec;
        job.status.pcId = spec.pc.id;
        job.status.host = spec.pc.host;
        job.status.result.pcId = spec.pc.id;
        job.status.result.host = spec.pc.host;
        job.status.result.logsFilesPlanned = spec.files.logs.size();
        job.status.result.shotsFilesPlanned = spec.files.screenshots.size();

        if (spec.skipped) {
            job.status.state = JobState::Skipped;
            job.status.result.state = JobState::Skipped;
            job.status.errorMessage = spec.skipReason;
            job.status.totalBytes = 0;
            job.status.copiedBytes = 0;
            job.status.percent = 0;
        } else {
            job.status.state = JobState::Queued;
            job.status.result.state = JobState::Queued;
            job.status.totalBytes = spec.files.totalBytes();
            job.status.copiedBytes = 0;
            job.status.percent = 0;
        }
        mJobs.push_back(std::move(job));
    }

    emit warningsChanged();
    recomputeOverallProgress(false);
    setPhase(EnginePhase::Running);
    writeManifest();

    scheduleNext();
    checkSessionFinished();
}

int TransferEngine::activeJobCount() const {
    int count = 0;
    for (const InternalJob &job : mJobs) {
        if (job.status.state == JobState::CopyingLogs
            || job.status.state == JobState::CopyingScreenshots
            || job.status.state == JobState::Verifying) {
            ++count;
        }
    }
    return count;
}

void TransferEngine::scheduleNext() {
    if (mPhase != EnginePhase::Running) {
        return;
    }

    const int limit = qMax(1, mConfig.maxParallel);
    while (activeJobCount() < limit) {
        int nextIdx = -1;
        for (int i = 0; i < static_cast<int>(mJobs.size()); ++i) {
            if (mJobs.at(static_cast<size_t>(i)).status.state == JobState::Queued) {
                nextIdx = i;
                break;
            }
        }
        if (nextIdx < 0) {
            break;
        }
        startJob(nextIdx);
    }
}

void TransferEngine::startJob(int index) {
    InternalJob &job = mJobs[static_cast<size_t>(index)];
    job.logsProgressBytes = 0;
    job.shotsProgressBytes = 0;
    job.stdoutBuffer.clear();
    job.stderrText.clear();
    job.diskFullDetected = false;
    job.status.copiedBytes = 0;
    job.status.percent = 0;
    job.status.rateBytesPerSec = 0.0;
    job.status.currentFile.clear();
    job.status.errorMessage.clear();
    job.status.result.logsFilesCopied = 0;
    job.status.result.logsBytesCopied = 0;
    job.status.result.shotsFilesCopied = 0;
    job.status.result.shotsBytesCopied = 0;
    job.status.result.logsRsyncExitCode = -1;
    job.status.result.shotsRsyncExitCode = -1;
    job.status.result.errors.clear();

    if (!job.spec.files.logs.isEmpty()) {
        startStage(index, true);
    } else {
        job.status.result.logsRsyncExitCode = 0;
        if (!job.spec.files.screenshots.isEmpty()) {
            startStage(index, false);
        } else {
            job.status.result.shotsRsyncExitCode = 0;
            verifyJob(index);
        }
    }
}

void TransferEngine::startStage(int index, bool isLogs) {
    if (mPhase != EnginePhase::Running) {
        return;
    }

    InternalJob &job = mJobs[static_cast<size_t>(index)];
    const JobState stageState = isLogs ? JobState::CopyingLogs : JobState::CopyingScreenshots;
    job.status.state = stageState;
    job.status.result.state = stageState;
    job.status.rateBytesPerSec = 0.0;
    job.status.currentFile.clear();
    job.stdoutBuffer.clear();
    job.stderrText.clear();
    job.diskFullDetected = false;

    emit jobUpdated(index);
    recomputeOverallProgress(false);

    if (!mTempDir || !mTempDir->isValid()) {
        failJob(index,
                QStringLiteral("Could not prepare the file transfer list. Check available disk space and click Retry."),
                QStringLiteral("Failed to create temporary directory for transfer list."));
        return;
    }

    const QString stageName = isLogs ? QStringLiteral("logs") : QStringLiteral("screenshots");
    const QString listFile = QDir(mTempDir->path())
                                 .filePath(QStringLiteral("%1_%2.list").arg(job.spec.pc.id, stageName));
    const QVector<FileEntry> &entries = isLogs ? job.spec.files.logs : job.spec.files.screenshots;

    QString listErr;
    if (!PathSanitizer::writeFilesFromList(listFile, entries, &listErr)) {
        failJob(index,
                QStringLiteral("Could not prepare the file transfer list. Check available disk space and click Retry."),
                listErr);
        return;
    }

    const QString destDir = QDir(mSessionDir)
                                .filePath(QStringLiteral("%1/%2").arg(job.spec.pc.id, stageName));
    if (!QDir().mkpath(destDir)) {
        failJob(index,
                QStringLiteral("Could not create the destination folder on the USB drive. Check that the drive is connected and writable, then click Retry."),
                QStringLiteral("Failed to create destination folder: %1").arg(destDir));
        return;
    }

    SshParams sshParams;
    sshParams.port = job.spec.pc.port;
    sshParams.sshKeyPath = mConfig.sshKeyPath;
    sshParams.knownHostsPath = mConfig.knownHostsPath;
    sshParams.agentSocket = mAgentSocket;
    const QString sshCmd = SshOptions::buildRsyncSshCommand(sshParams);

    const QString remoteRelDir = isLogs ? mConfig.paths.logs : mConfig.paths.screenshots;
    const QString remoteSpec = QStringLiteral("%1@%2:%3")
                                   .arg(mConfig.sshUser,
                                        job.spec.pc.host,
                                        normalizeRemoteDir(remoteRelDir));
    const QString destWithSlash = destDir.endsWith(QLatin1Char('/'))
                                      ? destDir
                                      : (destDir + QLatin1Char('/'));

    job.process = std::make_unique<QProcess>();
    QProcess *proc = job.process.get();
    proc->setStandardInputFile(QProcess::nullDevice());
    configureRsyncChildProcess(proc);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, index, proc]() {
        handleProcessOutput(index, proc);
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, index, proc]() {
        if (index < 0 || index >= static_cast<int>(mJobs.size())
            || mJobs.at(static_cast<size_t>(index)).process.get() != proc) {
            return;
        }
        appendStderrChunk(index, QString::fromUtf8(proc->readAllStandardError()));
    });
    connect(proc, &QProcess::finished, this,
            [this, index, proc, isLogs](int exitCode, QProcess::ExitStatus exitStatus) {
                handleProcessFinished(index, proc, isLogs, exitCode,
                                      exitStatus == QProcess::NormalExit);
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, index, proc, isLogs](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) {
                    return;
                }
                handleProcessFailedToStart(index, proc, isLogs);
            });

    proc->start(QStringLiteral("rsync"),
                {QStringLiteral("-t"),
                 QStringLiteral("--files-from=%1").arg(listFile),
                 QStringLiteral("--from0"),
                 QStringLiteral("--partial-dir=.partial"),
                 QStringLiteral("--info=progress2,name1"),
                 QStringLiteral("--no-inc-recursive"),
                 QStringLiteral("-e"),
                 sshCmd,
                 QStringLiteral("--"),
                 remoteSpec,
                 destWithSlash});
}

void TransferEngine::appendStderrChunk(int index, const QString &chunk) {
    if (index < 0 || index >= static_cast<int>(mJobs.size()) || chunk.isEmpty()) {
        return;
    }
    InternalJob &job = mJobs[static_cast<size_t>(index)];
    if (!job.diskFullDetected
        && (chunk.contains(QStringLiteral("No space left on device"), Qt::CaseInsensitive)
            || chunk.contains(QStringLiteral("write failed"), Qt::CaseInsensitive))) {
        job.diskFullDetected = true;
    }
    const int remaining = kMaxStderrChars - job.stderrText.size();
    if (remaining > 0) {
        if (chunk.size() <= remaining) {
            job.stderrText.append(chunk);
        } else {
            job.stderrText.append(chunk.left(remaining));
        }
    }
}

void TransferEngine::handleProcessOutput(int index, QProcess *proc) {
    if (index < 0 || index >= static_cast<int>(mJobs.size())) {
        return;
    }
    InternalJob &job = mJobs[static_cast<size_t>(index)];
    if (job.process.get() != proc) {
        return;
    }

    job.stdoutBuffer.append(proc->readAllStandardOutput());
    bool updated = false;

    int start = 0;
    const int len = job.stdoutBuffer.size();
    for (int i = 0; i < len; ++i) {
        const char c = job.stdoutBuffer.at(i);
        if (c == '\r' || c == '\n') {
            if (i > start) {
                const QString line = QString::fromUtf8(job.stdoutBuffer.constData() + start, i - start);
                const ProgressLine parsed = ProgressLine::parseOutputLine(line);
                if (parsed.isProgress) {
                    if (job.status.state == JobState::CopyingLogs) {
                        job.logsProgressBytes = parsed.bytesDone;
                    } else if (job.status.state == JobState::CopyingScreenshots) {
                        job.shotsProgressBytes = parsed.bytesDone;
                    }
                    qint64 combined = job.logsProgressBytes + job.shotsProgressBytes;
                    if (job.status.totalBytes > 0) {
                        combined = qMin(combined, job.status.totalBytes);
                        job.status.percent = qBound(0, static_cast<int>(combined * 100 / job.status.totalBytes), 100);
                    } else {
                        job.status.percent = parsed.percent;
                    }
                    job.status.copiedBytes = combined;
                    job.status.rateBytesPerSec = parsed.rateBytesPerSec;
                    updated = true;
                } else if (!parsed.currentFile.isEmpty()) {
                    job.status.currentFile = parsed.currentFile;
                    updated = true;
                }
            }
            start = i + 1;
        }
    }

    if (start > 0) {
        job.stdoutBuffer.remove(0, start);
    }

    if (updated) {
        emit jobUpdated(index);
        recomputeOverallProgress(true);
    }
}

void TransferEngine::handleProcessFinished(int index, QProcess *proc, bool isLogs,
                                           int exitCode, bool normalExit) {
    if (index < 0 || index >= static_cast<int>(mJobs.size())) {
        return;
    }
    InternalJob &job = mJobs[static_cast<size_t>(index)];
    if (job.process.get() != proc) {
        return;
    }

    // Drain any remaining output
    handleProcessOutput(index, proc);
    appendStderrChunk(index, QString::fromUtf8(proc->readAllStandardError()));
    retireProcessAsync(std::move(job.process), kKillTimeoutMs);

    if (mPhase == EnginePhase::Cancelling) {
        bool anyProcessLeft = false;
        for (const InternalJob &j : mJobs) {
            if (j.process != nullptr) {
                anyProcessLeft = true;
                break;
            }
        }
        if (!anyProcessLeft) {
            finalizeCancel();
        }
        return;
    }
    if (mPhase == EnginePhase::UsbRemoved) {
        return;
    }

    if (isLogs) {
        job.status.result.logsRsyncExitCode = normalExit ? exitCode : -1;
    } else {
        job.status.result.shotsRsyncExitCode = normalExit ? exitCode : -1;
    }

    const bool isPartialWarning = normalExit && (exitCode == 23 || exitCode == 24);
    const bool isOk = normalExit && (exitCode == 0);

    if (!isOk && !isPartialWarning) {
        const QString userMsg = classifyRsyncFailure(exitCode, normalExit,
                                                     job.stderrText.trimmed(),
                                                     job.diskFullDetected);
        const QString firstLine = job.stderrText.trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
        const QString techDetail = firstLine.isEmpty()
                                       ? QStringLiteral("rsync exit code %1").arg(exitCode)
                                       : QStringLiteral("rsync exit code %1: %2").arg(exitCode).arg(firstLine);
        failJob(index, userMsg, techDetail);
        return;
    }

    if (isPartialWarning) {
        const QString kind = isLogs ? QStringLiteral("logs") : QStringLiteral("screenshots");
        const QString msg = QStringLiteral("%1: partial transfer of %2 (rsync exit %3)")
                                .arg(job.spec.pc.id, kind)
                                .arg(exitCode);
        job.status.result.errors.append(msg);
        addWarning(msg);
    }

    if (isLogs) {
        job.logsProgressBytes = job.spec.files.logsBytes;
        if (!job.spec.files.screenshots.isEmpty()) {
            startStage(index, false);
        } else {
            job.status.result.shotsRsyncExitCode = 0;
            verifyJob(index);
        }
    } else {
        job.shotsProgressBytes = job.spec.files.shotsBytes;
        verifyJob(index);
    }
}

void TransferEngine::handleProcessFailedToStart(int index, QProcess *proc, bool /*isLogs*/) {
    if (index < 0 || index >= static_cast<int>(mJobs.size())) {
        return;
    }
    InternalJob &job = mJobs[static_cast<size_t>(index)];
    if (job.process.get() != proc) {
        return;
    }
    const QString errStr = proc->errorString();
    retireProcessAsync(std::move(job.process), kKillTimeoutMs);

    if (mPhase == EnginePhase::Cancelling) {
        bool anyProcessLeft = false;
        for (const InternalJob &j : mJobs) {
            if (j.process != nullptr) {
                anyProcessLeft = true;
                break;
            }
        }
        if (!anyProcessLeft) {
            finalizeCancel();
        }
        return;
    }
    if (mPhase == EnginePhase::UsbRemoved) {
        return;
    }

    failJob(index,
            QStringLiteral("Could not start the file transfer tool. Contact your system administrator."),
            QStringLiteral("Failed to start rsync: %1").arg(errStr));
}

void TransferEngine::verifyJob(int index) {
    InternalJob &job = mJobs[static_cast<size_t>(index)];
    job.status.state = JobState::Verifying;
    job.status.result.state = JobState::Verifying;
    job.status.rateBytesPerSec = 0.0;
    job.status.currentFile.clear();
    emit jobUpdated(index);
    recomputeOverallProgress(false);

    const QString pcRoot = QDir(mSessionDir).filePath(job.spec.pc.id);
    const QString logsDir = QDir(pcRoot).filePath(QStringLiteral("logs"));
    const QString shotsDir = QDir(pcRoot).filePath(QStringLiteral("screenshots"));
    const quint64 generation = mSessionGeneration;

    const int plannedLogsCount = job.status.result.logsFilesPlanned;
    const int plannedShotsCount = job.status.result.shotsFilesPlanned;
    const qint64 plannedLogsBytes = job.spec.files.logsBytes;
    const qint64 plannedShotsBytes = job.spec.files.shotsBytes;
    const bool hadPriorWarnings = !job.status.result.errors.isEmpty()
                                  || (job.status.result.logsRsyncExitCode != 0)
                                  || (job.status.result.shotsRsyncExitCode != 0);

    QPointer<TransferEngine> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self, generation, index,
                                                            logsDir, shotsDir,
                                                            plannedLogsCount, plannedShotsCount,
                                                            plannedLogsBytes, plannedShotsBytes,
                                                            hadPriorWarnings]() {
        int logsCount = 0;
        qint64 logsBytes = 0;
        scanDirectoryStats(logsDir, &logsCount, &logsBytes);

        int shotsCount = 0;
        qint64 shotsBytes = 0;
        scanDirectoryStats(shotsDir, &shotsCount, &shotsBytes);

        const bool clean = !hadPriorWarnings
                           && logsCount == plannedLogsCount
                           && shotsCount == plannedShotsCount
                           && logsBytes == plannedLogsBytes
                           && shotsBytes == plannedShotsBytes;
        if (clean) {
            QDir(QDir(logsDir).filePath(QStringLiteral(".partial"))).removeRecursively();
            QDir(QDir(shotsDir).filePath(QStringLiteral(".partial"))).removeRecursively();
        }

        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, generation, index, logsCount, logsBytes, shotsCount, shotsBytes]() {
                if (!self) {
                    return;
                }
                self->finishVerifyJob(generation, index, logsCount, logsBytes, shotsCount, shotsBytes);
            },
            Qt::QueuedConnection);
    }));
}

void TransferEngine::finishVerifyJob(quint64 generation, int index,
                                     int logsCount, qint64 logsBytes,
                                     int shotsCount, qint64 shotsBytes) {
    if (generation != mSessionGeneration || mPhase != EnginePhase::Running) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(mJobs.size())) {
        return;
    }
    InternalJob &job = mJobs[static_cast<size_t>(index)];
    if (job.status.state != JobState::Verifying) {
        return;
    }

    job.status.result.logsFilesCopied = logsCount;
    job.status.result.logsBytesCopied = logsBytes;
    job.status.result.shotsFilesCopied = shotsCount;
    job.status.result.shotsBytesCopied = shotsBytes;

    bool hasWarnings = !job.status.result.errors.isEmpty()
                       || (job.status.result.logsRsyncExitCode != 0)
                       || (job.status.result.shotsRsyncExitCode != 0);

    if (logsCount != job.status.result.logsFilesPlanned) {
        const QString msg = QStringLiteral("%1: expected %2 log files, verified %3")
                                .arg(job.spec.pc.id)
                                .arg(job.status.result.logsFilesPlanned)
                                .arg(logsCount);
        job.status.result.errors.append(msg);
        addWarning(msg);
        hasWarnings = true;
    }
    if (shotsCount != job.status.result.shotsFilesPlanned) {
        const QString msg = QStringLiteral("%1: expected %2 screenshot files, verified %3")
                                .arg(job.spec.pc.id)
                                .arg(job.status.result.shotsFilesPlanned)
                                .arg(shotsCount);
        job.status.result.errors.append(msg);
        addWarning(msg);
        hasWarnings = true;
    }
    if (logsBytes != job.spec.files.logsBytes) {
        const QString msg = QStringLiteral("%1: log bytes mismatch (expected %2, verified %3)")
                                .arg(job.spec.pc.id)
                                .arg(job.spec.files.logsBytes)
                                .arg(logsBytes);
        job.status.result.errors.append(msg);
        addWarning(msg);
        hasWarnings = true;
    }
    if (shotsBytes != job.spec.files.shotsBytes) {
        const QString msg = QStringLiteral("%1: screenshot bytes mismatch (expected %2, verified %3)")
                                .arg(job.spec.pc.id)
                                .arg(job.spec.files.shotsBytes)
                                .arg(shotsBytes);
        job.status.result.errors.append(msg);
        addWarning(msg);
        hasWarnings = true;
    }

    if (!hasWarnings) {
        job.status.state = JobState::Done;
        job.status.result.state = JobState::Done;
        job.status.copiedBytes = job.status.totalBytes;
        job.status.percent = 100;
    } else {
        job.status.state = JobState::DoneWithWarnings;
        job.status.result.state = JobState::DoneWithWarnings;
        job.status.copiedBytes = qMin(job.status.totalBytes, logsBytes + shotsBytes);
        job.status.percent = 100;
        job.status.errorMessage = QStringLiteral("Some files could not be verified. Check warnings or click Retry.");
    }

    emit jobUpdated(index);
    recomputeOverallProgress(true);
    writeManifest();
    scheduleNext();
    checkSessionFinished();
}

void TransferEngine::failJob(int index, const QString &message, const QString &technicalDetail) {
    InternalJob &job = mJobs[static_cast<size_t>(index)];
    job.status.state = JobState::Failed;
    job.status.result.state = JobState::Failed;
    job.status.rateBytesPerSec = 0.0;
    job.status.currentFile.clear();
    job.status.errorMessage = message;
    job.status.result.errors.append(technicalDetail.isEmpty() ? message : technicalDetail);
    addWarning(QStringLiteral("%1: %2").arg(job.spec.pc.id,
                                            technicalDetail.isEmpty() ? message : technicalDetail));

    emit jobUpdated(index);
    recomputeOverallProgress(false);
    writeManifest();
    scheduleNext();
    checkSessionFinished();
}

void TransferEngine::checkSessionFinished() {
    if (mPhase != EnginePhase::Running) {
        return;
    }

    bool anyWarningsOrFailures = !mWarnings.isEmpty();
    for (const InternalJob &job : mJobs) {
        const JobState st = job.status.state;
        if (st == JobState::Queued
            || st == JobState::CopyingLogs
            || st == JobState::CopyingScreenshots
            || st == JobState::Verifying) {
            return;
        }
        if (st == JobState::DoneWithWarnings || st == JobState::Failed) {
            anyWarningsOrFailures = true;
        }
    }

    mFinishedAt = QDateTime::currentDateTime();
    setPhase(anyWarningsOrFailures ? EnginePhase::FinishedWithWarnings : EnginePhase::Finished);
    writeManifest();
    recomputeOverallProgress(false);
}

void TransferEngine::cancel() {
    if (mPhase != EnginePhase::Running && mPhase != EnginePhase::UsbRemoved) {
        return;
    }

    ++mSessionGeneration;
    setPhase(EnginePhase::Cancelling);

    bool anyRunning = false;
    for (InternalJob &job : mJobs) {
        if (job.process && job.process->state() != QProcess::NotRunning) {
            anyRunning = true;
#ifdef Q_OS_LINUX
            signalProcessGroup(job.process.get(), SIGTERM);
#endif
            job.process->terminate();
        }
    }

    if (!anyRunning) {
        finalizeCancel();
        return;
    }

    mCancelKillTimer.start();
}

void TransferEngine::finalizeCancel() {
    mCancelKillTimer.stop();

    for (int i = 0; i < static_cast<int>(mJobs.size()); ++i) {
        InternalJob &job = mJobs[static_cast<size_t>(i)];
        if (job.process) {
            retireProcessAsync(std::move(job.process), kKillTimeoutMs);
        }
        const JobState st = job.status.state;
        if (st == JobState::Queued
            || st == JobState::CopyingLogs
            || st == JobState::CopyingScreenshots
            || st == JobState::Verifying) {
            job.status.state = JobState::Cancelled;
            job.status.result.state = JobState::Cancelled;
            job.status.rateBytesPerSec = 0.0;
            job.status.currentFile.clear();
            emit jobUpdated(i);
        }
    }

    mFinishedAt = QDateTime::currentDateTime();
    setPhase(EnginePhase::Cancelled);
    writeManifest();
    recomputeOverallProgress(false);
}

void TransferEngine::pauseForUsbRemoved() {
    if (mPhase != EnginePhase::Running) {
        return;
    }

    ++mSessionGeneration;
    setPhase(EnginePhase::UsbRemoved);

    for (int i = 0; i < static_cast<int>(mJobs.size()); ++i) {
        InternalJob &job = mJobs[static_cast<size_t>(i)];
        if (job.process) {
            retireProcessAsync(std::move(job.process), kUsbPauseKillTimeoutMs);
        }
        const JobState st = job.status.state;
        if (st == JobState::CopyingLogs
            || st == JobState::CopyingScreenshots
            || st == JobState::Verifying) {
            job.status.state = JobState::Queued;
            job.status.result.state = JobState::Queued;
            job.status.rateBytesPerSec = 0.0;
            job.status.currentFile.clear();
            emit jobUpdated(i);
        }
    }

    writeManifest();
    recomputeOverallProgress(false);
}

void TransferEngine::retryAfterUsbReturned() {
    if (mPhase == EnginePhase::Idle || mPhase == EnginePhase::Cancelling) {
        return;
    }

    bool anyRequeued = false;
    for (int i = 0; i < static_cast<int>(mJobs.size()); ++i) {
        InternalJob &job = mJobs[static_cast<size_t>(i)];
        if (job.status.state == JobState::Failed || job.status.state == JobState::Queued) {
            job.status.state = JobState::Queued;
            job.status.result.state = JobState::Queued;
            job.status.errorMessage.clear();
            job.status.rateBytesPerSec = 0.0;
            job.status.currentFile.clear();
            emit jobUpdated(i);
            anyRequeued = true;
        }
    }

    if (!anyRequeued) {
        return;
    }

    mFinishedAt = QDateTime();
    mEtaSamples.clear();
    mEtaTimer.start();
    setPhase(EnginePhase::Running);
    writeManifest();
    scheduleNext();
    checkSessionFinished();
}

void TransferEngine::retryPc(const QString &pcId) {
    if (mPhase == EnginePhase::Idle || mPhase == EnginePhase::Cancelling) {
        return;
    }

    int targetIdx = -1;
    for (int i = 0; i < static_cast<int>(mJobs.size()); ++i) {
        if (mJobs.at(static_cast<size_t>(i)).spec.pc.id == pcId) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx < 0) {
        return;
    }

    InternalJob &job = mJobs[static_cast<size_t>(targetIdx)];
    if (job.status.state != JobState::Failed && job.status.state != JobState::Cancelled) {
        return;
    }

    const QString warningPrefix = pcId + QLatin1Char(':');
    bool removedWarnings = false;
    for (int i = mWarnings.size() - 1; i >= 0; --i) {
        if (mWarnings.at(i).startsWith(warningPrefix)) {
            mWarnings.removeAt(i);
            removedWarnings = true;
        }
    }
    if (removedWarnings) {
        emit warningsChanged();
    }

    job.status.state = JobState::Queued;
    job.status.result.state = JobState::Queued;
    job.status.copiedBytes = 0;
    job.status.percent = 0;
    job.status.rateBytesPerSec = 0.0;
    job.status.currentFile.clear();
    job.status.errorMessage.clear();
    emit jobUpdated(targetIdx);

    if (mPhase != EnginePhase::Running) {
        mFinishedAt = QDateTime();
        mEtaSamples.clear();
        mEtaTimer.start();
        setPhase(EnginePhase::Running);
    }

    writeManifest();
    recomputeOverallProgress(false);
    scheduleNext();
}

void TransferEngine::shutdownNow() {
    ++mSessionGeneration;
    mCancelKillTimer.stop();
    for (InternalJob &job : mJobs) {
        if (job.process) {
            terminateSync(job.process.get(), this);
            job.process.reset();
        }
    }
    for (std::unique_ptr<QProcess> &proc : mStoppingProcesses) {
        terminateSync(proc.get(), this);
    }
    mStoppingProcesses.clear();
    mTempDir.reset();
}

void TransferEngine::writeManifest() {
    if (mSessionDir.isEmpty()) {
        return;
    }

    ManifestData data;
    data.operatorId = mOperatorId;
    data.appVersion = mAppVersion;
    data.windowStart = mWindow.start;
    data.windowEnd = mWindow.end;
    data.startedAt = mStartedAt;
    data.finishedAt = mFinishedAt;
    data.status = phaseToManifestStatus(mPhase);
    data.pcs.reserve(static_cast<int>(mJobs.size()));
    for (const InternalJob &job : mJobs) {
        data.pcs.append(job.status.result);
    }

    const QString manifestPath = QDir(mSessionDir).filePath(QStringLiteral("manifest.json"));
    QString err;
    if (!Manifest::writeAtomic(manifestPath, data, &err)
        && mPhase != EnginePhase::UsbRemoved) {
        addWarning(QStringLiteral("Could not update manifest.json. Check that the USB drive is connected and writable."));
    }
}

void TransferEngine::setPhase(EnginePhase phase) {
    if (mPhase == phase) {
        return;
    }
    mPhase = phase;
    emit phaseChanged(mPhase);
}

void TransferEngine::addWarning(const QString &warning) {
    if (warning.isEmpty()) {
        return;
    }
    mWarnings.append(warning);
    emit warningsChanged();
}

void TransferEngine::recomputeOverallProgress(bool recordEtaSample) {
    qint64 total = 0;
    qint64 copied = 0;
    double rate = 0.0;
    bool allCompleted = !mJobs.empty();

    for (const InternalJob &job : mJobs) {
        if (job.status.state == JobState::Skipped) {
            continue;
        }
        total += job.status.totalBytes;
        copied += job.status.copiedBytes;
        if (job.status.state == JobState::CopyingLogs
            || job.status.state == JobState::CopyingScreenshots) {
            rate += job.status.rateBytesPerSec;
        }
        if (job.status.state != JobState::Done
            && job.status.state != JobState::DoneWithWarnings) {
            allCompleted = false;
        }
    }

    mTotalBytes = total;
    mCopiedBytes = copied;
    if (allCompleted) {
        mOverallPercent = 100;
    } else {
        mOverallPercent = (total > 0)
                              ? qBound(0, static_cast<int>(copied * 100 / total), 100)
                              : 100;
    }
    mCombinedRate = rate;

    if (mPhase == EnginePhase::Running) {
        const qint64 nowMs = mEtaTimer.elapsed();
        if (recordEtaSample) {
            mEtaSamples.append(qMakePair(nowMs, copied));
        }
        // Keep ~10 second moving window
        const qint64 cutoffMs = nowMs - kEtaWindowMs;
        int pruneCount = 0;
        while (pruneCount + 1 < mEtaSamples.size()
               && mEtaSamples.at(pruneCount).first < cutoffMs) {
            ++pruneCount;
        }
        if (pruneCount > 0) {
            mEtaSamples.remove(0, pruneCount);
        }

        const qint64 remaining = qMax<qint64>(0, total - copied);
        if (remaining == 0) {
            mEtaSeconds = 0;
        } else {
            double avgRate = 0.0;
            if (mEtaSamples.size() >= 2) {
                const qint64 dt = mEtaSamples.last().first - mEtaSamples.first().first;
                const qint64 db = mEtaSamples.last().second - mEtaSamples.first().second;
                if (dt >= kMinEtaDeltaMs && db > 0) {
                    avgRate = static_cast<double>(db) * 1000.0 / static_cast<double>(dt);
                }
            }
            if (avgRate <= 0.0 && rate > 0.0) {
                avgRate = rate;
            }
            mEtaSeconds = (avgRate > 0.0)
                              ? static_cast<int>(std::ceil(static_cast<double>(remaining) / avgRate))
                              : -1;
        }
    } else {
        mEtaSeconds = -1;
    }

    emit overallProgressChanged();
}

} // namespace dc
