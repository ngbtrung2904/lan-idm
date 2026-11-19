#include "RsyncLister.h"
#include "PathSanitizer.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QTime>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace dc {

namespace {

constexpr int kKillTimeoutMs = 500;

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

bool parseAsciiInt(const char *s, int len, int &out) {
    int val = 0;
    for (int i = 0; i < len; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        val = val * 10 + (c - '0');
    }
    out = val;
    return true;
}

void stopProcessSync(QProcess *proc, QObject *receiver) {
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

} // namespace

RsyncLister::RsyncLister(QObject *parent)
    : QObject(parent) {}

RsyncLister::~RsyncLister() {
    for (auto &entry : mJobs) {
        stopProcessSync(entry.second.logsProc.get(), this);
        stopProcessSync(entry.second.shotsProc.get(), this);
    }
    mJobs.clear();

    for (std::unique_ptr<QProcess> &proc : mStoppingProcesses) {
        stopProcessSync(proc.get(), this);
    }
    mStoppingProcesses.clear();
}

bool RsyncLister::isRunning() const {
    return !mJobs.empty();
}

QString RsyncLister::normalizeRemoteDir(const QString &relDir) {
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

QVector<FileEntry> RsyncLister::parseListOutput(const QByteArray &stdoutData) {
    QVector<FileEntry> entries;
    if (stdoutData.isEmpty()) {
        return entries;
    }

    entries.reserve(stdoutData.count('\n') + 1);

    constexpr int kDstTransitionDay = std::numeric_limits<int>::min();
    QHash<int, int> offsetCache;

    int start = 0;
    const int totalLen = stdoutData.size();
    const char *raw = stdoutData.constData();

    while (start < totalLen) {
        int end = stdoutData.indexOf('\n', start);
        if (end < 0) {
            end = totalLen;
        }
        int lineLen = end - start;
        if (lineLen > 0 && raw[start + lineLen - 1] == '\r') {
            --lineLen;
        }

        if (lineLen >= 32 && raw[start] == '-') {
            const char *p = raw + start;

            // 1. Permission token (must start with '-' and have length >= 10)
            int pos = 0;
            while (pos < lineLen && p[pos] != ' ' && p[pos] != '\t') {
                ++pos;
            }
            if (pos >= 10) {
                // Skip spaces before size
                while (pos < lineLen && (p[pos] == ' ' || p[pos] == '\t')) {
                    ++pos;
                }

                // 2. Size token (strip commas if present)
                qint64 size = 0;
                bool sizeOk = false;
                bool sizeInvalid = false;
                while (pos < lineLen && p[pos] != ' ' && p[pos] != '\t') {
                    const char ch = p[pos++];
                    if (ch == ',') {
                        continue;
                    }
                    if (ch >= '0' && ch <= '9') {
                        const int digit = ch - '0';
                        if (size > (std::numeric_limits<qint64>::max() - digit) / 10) {
                            sizeInvalid = true;
                            break;
                        }
                        size = size * 10 + digit;
                        sizeOk = true;
                    } else {
                        sizeInvalid = true;
                        break;
                    }
                }

                if (!sizeInvalid && sizeOk && size >= 0) {
                    // Skip spaces before date
                    while (pos < lineLen && (p[pos] == ' ' || p[pos] == '\t')) {
                        ++pos;
                    }

                    // 3. Date token (yyyy/MM/dd)
                    const int dateStart = pos;
                    while (pos < lineLen && p[pos] != ' ' && p[pos] != '\t') {
                        ++pos;
                    }
                    const int dateLen = pos - dateStart;

                    // Skip spaces before time
                    while (pos < lineLen && (p[pos] == ' ' || p[pos] == '\t')) {
                        ++pos;
                    }

                    // 4. Time token (HH:mm:ss)
                    const int timeStart = pos;
                    while (pos < lineLen && p[pos] != ' ' && p[pos] != '\t') {
                        ++pos;
                    }
                    const int timeLen = pos - timeStart;

                    // 5. Exactly one space before relative path
                    if (dateLen == 10 && timeLen == 8
                        && pos < lineLen && p[pos] == ' '
                        && p[dateStart + 4] == '/' && p[dateStart + 7] == '/'
                        && p[timeStart + 2] == ':' && p[timeStart + 5] == ':') {
                        int year = 0;
                        int month = 0;
                        int day = 0;
                        int hour = 0;
                        int minute = 0;
                        int second = 0;
                        if (parseAsciiInt(p + dateStart, 4, year)
                            && parseAsciiInt(p + dateStart + 5, 2, month)
                            && parseAsciiInt(p + dateStart + 8, 2, day)
                            && parseAsciiInt(p + timeStart, 2, hour)
                            && parseAsciiInt(p + timeStart + 3, 2, minute)
                            && parseAsciiInt(p + timeStart + 6, 2, second)) {
                            const QDate date(year, month, day);
                            const QTime time(hour, minute, second);
                            if (date.isValid() && time.isValid()) {
                                const QString relPath = QString::fromUtf8(p + pos + 1, lineLen - pos - 1);
                                if (PathSanitizer::isSafeRelativePath(relPath)) {
                                    const int dateKey = (year << 9) | (month << 5) | day;
                                    const auto cacheIt = offsetCache.constFind(dateKey);
                                    int cachedOffset = 0;
                                    if (cacheIt != offsetCache.constEnd()) {
                                        cachedOffset = cacheIt.value();
                                    } else {
                                        const int offStart = QDateTime(date, QTime(0, 0, 0), Qt::LocalTime).offsetFromUtc();
                                        const int offEnd = QDateTime(date, QTime(23, 59, 59), Qt::LocalTime).offsetFromUtc();
                                        if (offStart != offEnd) {
                                            cachedOffset = kDstTransitionDay;
                                        } else {
                                            cachedOffset = QDateTime(date, QTime(12, 0, 0), Qt::LocalTime).offsetFromUtc();
                                        }
                                        offsetCache.insert(dateKey, cachedOffset);
                                    }

                                    const QDateTime mtime = (cachedOffset == kDstTransitionDay)
                                                                ? QDateTime(date, time, Qt::LocalTime)
                                                                : QDateTime(date, time, Qt::OffsetFromUTC, cachedOffset);
                                    if (mtime.isValid()) {
                                        FileEntry entry;
                                        entry.relPath = relPath;
                                        entry.size = size;
                                        entry.mtime = mtime;
                                        entries.append(std::move(entry));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        start = end + 1;
    }

    return entries;
}

void RsyncLister::retireProcessAsync(std::unique_ptr<QProcess> proc) {
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
    QTimer::singleShot(kKillTimeoutMs, this, [guard]() {
        if (guard && guard->state() != QProcess::NotRunning) {
#ifdef Q_OS_LINUX
            signalProcessGroup(guard.data(), SIGKILL);
#endif
            guard->kill();
        }
    });
}

void RsyncLister::eraseStoppingProcess(QProcess *proc) {
    const auto it = std::find_if(mStoppingProcesses.begin(), mStoppingProcesses.end(),
                                 [proc](const std::unique_ptr<QProcess> &item) {
                                     return item.get() == proc;
                                 });
    if (it != mStoppingProcesses.end()) {
        mStoppingProcesses.erase(it);
    }
}

void RsyncLister::listPc(const PcConfig &pc,
                         const QString &sshUser,
                         const QString &logsRelDir,
                         const QString &shotsRelDir,
                         const SshParams &sshParams) {
    cleanupJob(pc.id, false);

    SshParams params = sshParams;
    params.port = pc.port;
    const QString sshCmd = SshOptions::buildRsyncSshCommand(params);

    PendingJob job;
    job.listing.pcId = pc.id;
    mJobs.emplace(pc.id, std::move(job));

    const QString logsRemote = QStringLiteral("%1@%2:%3")
                                   .arg(sshUser, pc.host, normalizeRemoteDir(logsRelDir));
    const QString shotsRemote = QStringLiteral("%1@%2:%3")
                                    .arg(sshUser, pc.host, normalizeRemoteDir(shotsRelDir));

    startDirProcess(pc.id, true, sshCmd, logsRemote);
    startDirProcess(pc.id, false, sshCmd, shotsRemote);
}

void RsyncLister::startDirProcess(const QString &pcId,
                                  bool isLogs,
                                  const QString &sshCmd,
                                  const QString &remoteSpec) {
    const auto it = mJobs.find(pcId);
    if (it == mJobs.end()) {
        return;
    }

    std::unique_ptr<QProcess> procOwner = std::make_unique<QProcess>();
    QProcess *proc = procOwner.get();
    proc->setStandardInputFile(QProcess::nullDevice());
    configureRsyncChildProcess(proc);

    if (isLogs) {
        it->second.logsProc = std::move(procOwner);
    } else {
        it->second.shotsProc = std::move(procOwner);
    }

    connect(proc, &QProcess::finished, this,
            [this, proc, pcId, isLogs](int exitCode, QProcess::ExitStatus exitStatus) {
                const auto jobIt = mJobs.find(pcId);
                if (jobIt == mJobs.end()) {
                    return;
                }
                std::unique_ptr<QProcess> &slot = isLogs ? jobIt->second.logsProc : jobIt->second.shotsProc;
                if (slot.get() != proc) {
                    return;
                }
                std::unique_ptr<QProcess> finishedProc = std::move(slot);

                const QByteArray stdoutData = finishedProc->readAllStandardOutput();
                const QString stderrText = QString::fromUtf8(finishedProc->readAllStandardError()).trimmed();
                retireProcessAsync(std::move(finishedProc));

                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    const QString kind = isLogs ? QStringLiteral("logs") : QStringLiteral("screenshots");
                    const QString errMsg = stderrText.isEmpty()
                                               ? QStringLiteral("Failed listing %1 (rsync exit %2)")
                                                     .arg(kind)
                                                     .arg(exitCode)
                                               : QStringLiteral("Failed listing %1: %2")
                                                     .arg(kind, stderrText);
                    cleanupJob(pcId, false);
                    emit listingFailed(pcId, errMsg);
                    return;
                }

                if (isLogs) {
                    jobIt->second.listing.logs = parseListOutput(stdoutData);
                    jobIt->second.logsDone = true;
                } else {
                    jobIt->second.listing.screenshots = parseListOutput(stdoutData);
                    jobIt->second.shotsDone = true;
                }

                if (jobIt->second.logsDone && jobIt->second.shotsDone) {
                    const PcListing completed = jobIt->second.listing;
                    mJobs.erase(jobIt);
                    emit listingFinished(pcId, completed);
                }
            });

    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, pcId, isLogs](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) {
                    return;
                }
                const auto jobIt = mJobs.find(pcId);
                if (jobIt == mJobs.end()) {
                    return;
                }
                std::unique_ptr<QProcess> &slot = isLogs ? jobIt->second.logsProc : jobIt->second.shotsProc;
                if (slot.get() != proc) {
                    return;
                }
                std::unique_ptr<QProcess> failedProc = std::move(slot);
                const QString errStr = failedProc->errorString();
                retireProcessAsync(std::move(failedProc));

                cleanupJob(pcId, false);
                emit listingFailed(pcId, QStringLiteral("Failed to start rsync: %1").arg(errStr));
            });

    proc->start(QStringLiteral("rsync"),
                {QStringLiteral("-r"),
                 QStringLiteral("--list-only"),
                 QStringLiteral("--no-human-readable"),
                 QStringLiteral("-e"),
                 sshCmd,
                 QStringLiteral("--"),
                 remoteSpec});
}

void RsyncLister::cleanupJob(const QString &pcId, bool syncWait) {
    const auto it = mJobs.find(pcId);
    if (it == mJobs.end()) {
        return;
    }
    std::unique_ptr<QProcess> logsProc = std::move(it->second.logsProc);
    std::unique_ptr<QProcess> shotsProc = std::move(it->second.shotsProc);
    mJobs.erase(it);

    if (syncWait) {
        stopProcessSync(logsProc.get(), this);
        stopProcessSync(shotsProc.get(), this);
    } else {
        retireProcessAsync(std::move(logsProc));
        retireProcessAsync(std::move(shotsProc));
    }
}

void RsyncLister::cancel() {
    QStringList ids;
    ids.reserve(static_cast<int>(mJobs.size()));
    for (const auto &entry : mJobs) {
        ids.append(entry.first);
    }
    for (const QString &id : ids) {
        cleanupJob(id, false);
    }
}

} // namespace dc
