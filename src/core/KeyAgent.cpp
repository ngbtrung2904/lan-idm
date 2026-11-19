#include "KeyAgent.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>
#include <utility>

#ifdef Q_OS_LINUX
#include <csignal>
#include <strings.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace dc {

namespace {

constexpr int kSocketPollIntervalMs = 15;
constexpr int kAgentStartTimeoutMs = 2000;
constexpr int kKillTimeoutMs = 500;
constexpr int kAgentSyncWaitMs = 1000;

std::atomic<int> agentInstanceCounter{0};

void secureWipeBytes(void *ptr, size_t len) {
    if (!ptr || len == 0) {
        return;
    }
#ifdef Q_OS_LINUX
    ::explicit_bzero(ptr, len);
#else
    volatile unsigned char *p = static_cast<volatile unsigned char *>(ptr);
    while (len--) {
        *p++ = 0;
    }
#endif
}

void wipeByteArray(QByteArray &ba) {
    if (!ba.isEmpty()) {
        ba.detach();
        secureWipeBytes(ba.data(), static_cast<size_t>(ba.size()));
    }
    ba.clear();
}

void wipeString(QString &str) {
    if (!str.isEmpty()) {
        str.detach();
        secureWipeBytes(str.data(), static_cast<size_t>(str.size()) * sizeof(QChar));
    }
    str.clear();
}

void terminateProcessSync(QProcess *proc, QObject *receiver, int waitMs) {
    if (!proc) {
        return;
    }
    proc->disconnect(receiver);
    if (proc->state() != QProcess::NotRunning) {
        proc->terminate();
        if (!proc->waitForFinished(waitMs)) {
            proc->kill();
            proc->waitForFinished(kKillTimeoutMs);
        }
    }
}

} // namespace

KeyAgent::KeyAgent(QObject *parent)
    : QObject(parent)
    , mSocketPath(buildSocketPath()) {
    mSocketPollTimer.setInterval(kSocketPollIntervalMs);
    connect(&mSocketPollTimer, &QTimer::timeout, this, &KeyAgent::pollAgentSocket);
}

KeyAgent::~KeyAgent() {
    lockSync();
}

QString KeyAgent::socketPath() const {
    return mSocketPath;
}

bool KeyAgent::isUnlocked() const {
    return mUnlocked;
}

QString KeyAgent::buildSocketPath() {
    QString baseDir = qEnvironmentVariable("XDG_RUNTIME_DIR").trimmed();
    if (baseDir.isEmpty() || !QDir(baseDir).exists()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    }
    if (baseDir.isEmpty() || !QDir(baseDir).exists()) {
        baseDir = QDir::tempPath();
    }

    const qint64 pid = QCoreApplication::applicationPid();
    const int seq = agentInstanceCounter.fetch_add(1);
    const QString fileName = (seq == 0)
                                 ? QStringLiteral("datacollector-agent-%1.sock").arg(pid)
                                 : QStringLiteral("datacollector-agent-%1-%2.sock").arg(pid).arg(seq);
    return QDir(baseDir).filePath(fileName);
}

QString KeyAgent::findAskpassScript() {
    QStringList candidates;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidates << QDir(appDir).filePath(QStringLiteral("askpass.sh"));
        candidates << QDir(appDir).filePath(QStringLiteral("../scripts/askpass.sh"));
    }
#ifdef DC_SOURCE_DIR
    candidates << QDir(QStringLiteral(DC_SOURCE_DIR))
                      .filePath(QStringLiteral("scripts/askpass.sh"));
#endif
    candidates << QStringLiteral("/usr/lib/data-collector/askpass.sh");

    for (const QString &candidate : candidates) {
        const QFileInfo fi(candidate);
        if (fi.exists() && fi.isFile() && fi.isExecutable()) {
            return fi.canonicalFilePath();
        }
    }
    return {};
}

void KeyAgent::clearPendingUnlock() {
    mSocketPollTimer.stop();
    wipeByteArray(mPendingPassUtf8);
    mPendingKeyPath.clear();
    mPendingAskpassPath.clear();
}

void KeyAgent::retireProcessAsync(std::unique_ptr<QProcess> proc, const QString &socketToRemove) {
    if (!proc) {
        return;
    }
    QProcess *raw = proc.get();
    raw->disconnect(this);
    mStoppingProcesses.push_back(std::move(proc));

    if (!socketToRemove.isEmpty()) {
        mStoppingSockets.append(socketToRemove);
    }

    if (raw->state() == QProcess::NotRunning) {
        if (!socketToRemove.isEmpty() && QFileInfo::exists(socketToRemove)) {
            QFile::remove(socketToRemove);
        }
        QMetaObject::invokeMethod(
            this, [this, raw]() { eraseStoppingProcess(raw); }, Qt::QueuedConnection);
        return;
    }

    connect(raw, &QProcess::finished, this, [this, raw, socketToRemove](int, QProcess::ExitStatus) {
        if (!socketToRemove.isEmpty() && QFileInfo::exists(socketToRemove)) {
            QFile::remove(socketToRemove);
        }
        QMetaObject::invokeMethod(
            this, [this, raw]() { eraseStoppingProcess(raw); }, Qt::QueuedConnection);
    });
    raw->terminate();
    QPointer<QProcess> guard(raw);
    QTimer::singleShot(kKillTimeoutMs, this, [guard]() {
        if (guard && guard->state() != QProcess::NotRunning) {
            guard->kill();
        }
    });
}

void KeyAgent::eraseStoppingProcess(QProcess *proc) {
    const auto it = std::find_if(mStoppingProcesses.begin(), mStoppingProcesses.end(),
                                 [proc](const std::unique_ptr<QProcess> &item) {
                                     return item.get() == proc;
                                 });
    if (it != mStoppingProcesses.end()) {
        mStoppingProcesses.erase(it);
    }
}

void KeyAgent::startAgentAsync() {
    if (mAgentProcess) {
        retireProcessAsync(std::move(mAgentProcess), QString());
    }

    if (QFileInfo::exists(mSocketPath)) {
        QFile::remove(mSocketPath);
    }

    mAgentProcess = std::make_unique<QProcess>();
    mAgentProcess->setStandardInputFile(QProcess::nullDevice());

#ifdef Q_OS_LINUX
    mAgentProcess->setChildProcessModifier([]() {
        ::signal(SIGTERM, SIG_DFL);
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    });
#endif

    QProcess *agent = mAgentProcess.get();
    connect(agent, &QProcess::finished, this,
            [this, agent](int /*exitCode*/, QProcess::ExitStatus /*exitStatus*/) {
                if (agent != mAgentProcess.get()) {
                    return;
                }
                const bool wasWaiting = mSocketPollTimer.isActive();
                retireProcessAsync(std::move(mAgentProcess), QString());
                mUnlocked = false;

                if (wasWaiting) {
                    clearPendingUnlock();
                    emit unlockFailed(QStringLiteral(
                        "Could not start the key agent. Restart Data Collector or contact your system administrator."));
                }
            });

    connect(agent, &QProcess::errorOccurred, this,
            [this, agent](QProcess::ProcessError err) {
                if (agent != mAgentProcess.get() || err != QProcess::FailedToStart) {
                    return;
                }
                const bool wasWaiting = mSocketPollTimer.isActive();
                retireProcessAsync(std::move(mAgentProcess), QString());
                mUnlocked = false;

                if (wasWaiting) {
                    clearPendingUnlock();
                    emit unlockFailed(QStringLiteral(
                        "Could not start the key agent. Restart Data Collector or contact your system administrator."));
                }
            });

    mAgentWaitTimer.start();
    mSocketPollTimer.start();
    mAgentProcess->start(QStringLiteral("ssh-agent"),
                         {QStringLiteral("-D"), QStringLiteral("-a"), mSocketPath});
}

void KeyAgent::pollAgentSocket() {
    if (!mAgentProcess) {
        clearPendingUnlock();
        mUnlocked = false;
        emit unlockFailed(QStringLiteral(
            "Could not start the key agent. Restart Data Collector or contact your system administrator."));
        return;
    }

    if (mAgentProcess->state() == QProcess::Running && QFileInfo::exists(mSocketPath)) {
        mSocketPollTimer.stop();
        launchAddProcess();
        return;
    }

    if (mAgentWaitTimer.elapsed() >= kAgentStartTimeoutMs) {
        clearPendingUnlock();
        retireProcessAsync(std::move(mAgentProcess), QString());
        if (QFileInfo::exists(mSocketPath)) {
            QFile::remove(mSocketPath);
        }
        mUnlocked = false;
        emit unlockFailed(QStringLiteral(
            "Could not start the key agent. Restart Data Collector or contact your system administrator."));
    }
}

void KeyAgent::cleanupAddProcess(bool syncWait) {
    if (!mAddProcess) {
        return;
    }
    std::unique_ptr<QProcess> proc = std::move(mAddProcess);
    if (syncWait) {
        terminateProcessSync(proc.get(), this, kKillTimeoutMs);
    } else {
        retireProcessAsync(std::move(proc), QString());
    }
}

QString KeyAgent::classifyAddError(int exitCode, const QString &stderrText) {
    if (stderrText.contains(QStringLiteral("Bad passphrase"), Qt::CaseInsensitive)
        || stderrText.contains(QStringLiteral("incorrect passphrase"), Qt::CaseInsensitive)
        || stderrText.contains(QStringLiteral("Could not unlock"), Qt::CaseInsensitive)) {
        return QStringLiteral("Incorrect passphrase. Check your key passphrase and try again.");
    }
    if (stderrText.contains(QStringLiteral("UNPROTECTED PRIVATE KEY"), Qt::CaseInsensitive)
        || stderrText.contains(QStringLiteral("too open"), Qt::CaseInsensitive)) {
        return QStringLiteral("SSH key permissions are too open. Contact your system administrator to fix key permissions.");
    }
    if (stderrText.contains(QStringLiteral("No such file"), Qt::CaseInsensitive)) {
        return QStringLiteral("SSH key file was not found. Check your configuration or contact your system administrator.");
    }
    if (stderrText.contains(QStringLiteral("invalid format"), Qt::CaseInsensitive)
        || stderrText.contains(QStringLiteral("Error loading key"), Qt::CaseInsensitive)) {
        return QStringLiteral("SSH key file is invalid. Contact your system administrator to check the key file.");
    }
    if (exitCode == 1 && stderrText.isEmpty()) {
        return QStringLiteral("Incorrect passphrase. Check your key passphrase and try again.");
    }
    return QStringLiteral("Could not unlock the SSH key. Check your passphrase and try again.");
}

void KeyAgent::unlock(const QString &keyPath, const QString &passphrase) {
    clearPendingUnlock();

    const QFileInfo keyInfo(keyPath);
    if (keyPath.isEmpty() || !keyInfo.exists() || !keyInfo.isFile()) {
        mUnlocked = false;
        emit unlockFailed(QStringLiteral(
            "SSH key file was not found. Check your configuration or contact your system administrator."));
        return;
    }

    const QString askpassPath = findAskpassScript();
    if (askpassPath.isEmpty()) {
        mUnlocked = false;
        emit unlockFailed(QStringLiteral(
            "Key unlock helper is missing. Reinstall Data Collector or contact your system administrator."));
        return;
    }

    mPendingKeyPath = keyPath;
    mPendingAskpassPath = askpassPath;
    mPendingPassUtf8 = passphrase.toUtf8();
    mPendingPassUtf8.detach();

    if (mAgentProcess && mAgentProcess->state() == QProcess::Running
        && QFileInfo::exists(mSocketPath)) {
        launchAddProcess();
        return;
    }

    startAgentAsync();
}

void KeyAgent::launchAddProcess() {
    cleanupAddProcess(false);

    const QString keyPath = mPendingKeyPath;
    const QString askpassPath = mPendingAskpassPath;
    QString passStr = QString::fromUtf8(mPendingPassUtf8);
    wipeByteArray(mPendingPassUtf8);
    mPendingKeyPath.clear();
    mPendingAskpassPath.clear();

    mAddProcess = std::make_unique<QProcess>();
    mAddProcess->setStandardInputFile(QProcess::nullDevice());

#ifdef Q_OS_LINUX
    mAddProcess->setChildProcessModifier([]() {
        ::signal(SIGTERM, SIG_DFL);
        ::setsid();
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    });
#endif

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("DISPLAY"));
    env.insert(QStringLiteral("SSH_AUTH_SOCK"), mSocketPath);
    env.insert(QStringLiteral("SSH_ASKPASS"), askpassPath);
    env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    env.insert(QStringLiteral("DC_SSH_PASSPHRASE"), passStr);
    mAddProcess->setProcessEnvironment(env);

    QProcess *proc = mAddProcess.get();
    connect(proc, &QProcess::finished, this,
            [this, proc](int exitCode, QProcess::ExitStatus exitStatus) {
                if (proc != mAddProcess.get()) {
                    return;
                }
                const QString stderrText = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                retireProcessAsync(std::move(mAddProcess), QString());

                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    mUnlocked = true;
                    emit unlocked();
                } else {
                    mUnlocked = false;
                    emit unlockFailed(classifyAddError(exitCode, stderrText));
                }
            });

    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError err) {
                if (proc != mAddProcess.get() || err != QProcess::FailedToStart) {
                    return;
                }
                retireProcessAsync(std::move(mAddProcess), QString());
                mUnlocked = false;
                emit unlockFailed(QStringLiteral(
                    "Could not start the key unlock tool. Contact your system administrator."));
            });

    mAddProcess->start(QStringLiteral("ssh-add"), {keyPath});

    // Clear passphrase from parent process memory immediately after launching ssh-add.
    env.remove(QStringLiteral("DC_SSH_PASSPHRASE"));
    env.clear();
    if (mAddProcess) {
        mAddProcess->setProcessEnvironment(QProcessEnvironment());
    }
    wipeString(passStr);
}

void KeyAgent::lock() {
    clearPendingUnlock();
    cleanupAddProcess(false);

    const QString sockPath = mSocketPath;
    mSocketPath = buildSocketPath();

    if (mAgentProcess) {
        retireProcessAsync(std::move(mAgentProcess), sockPath);
    }

    if (!sockPath.isEmpty() && QFileInfo::exists(sockPath)) {
        QFile::remove(sockPath);
    }

    mUnlocked = false;
}

void KeyAgent::lockSync() {
    clearPendingUnlock();
    cleanupAddProcess(true);

    if (mAgentProcess) {
        terminateProcessSync(mAgentProcess.get(), this, kAgentSyncWaitMs);
        mAgentProcess.reset();
    }

    for (std::unique_ptr<QProcess> &proc : mStoppingProcesses) {
        terminateProcessSync(proc.get(), this, kKillTimeoutMs);
    }
    mStoppingProcesses.clear();

    for (const QString &sock : std::as_const(mStoppingSockets)) {
        if (!sock.isEmpty() && QFileInfo::exists(sock)) {
            QFile::remove(sock);
        }
    }
    mStoppingSockets.clear();

    if (!mSocketPath.isEmpty() && QFileInfo::exists(mSocketPath)) {
        QFile::remove(mSocketPath);
    }

    mUnlocked = false;
}

} // namespace dc
