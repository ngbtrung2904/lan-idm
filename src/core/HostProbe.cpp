#include "HostProbe.h"

#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <algorithm>
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

void terminateProcessSync(QProcess *proc, QObject *receiver) {
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

HostProbe::HostProbe(QObject *parent)
    : QObject(parent) {}

HostProbe::~HostProbe() {
    for (auto &entry : mProcesses) {
        terminateProcessSync(entry.second.get(), this);
    }
    mProcesses.clear();

    for (std::unique_ptr<QProcess> &proc : mStoppingProcesses) {
        terminateProcessSync(proc.get(), this);
    }
    mStoppingProcesses.clear();
}

PcState HostProbe::classifyProbeResult(int exitCode, const QString &stderrText) {
    if (exitCode == 0) {
        return PcState::Online;
    }

    if (stderrText.contains(QStringLiteral("REMOTE HOST IDENTIFICATION HAS CHANGED"), Qt::CaseInsensitive)
        || stderrText.contains(QStringLiteral("Host key verification failed"), Qt::CaseInsensitive)) {
        return PcState::HostKeyMismatch;
    }

    if (stderrText.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive)) {
        return PcState::AuthFailed;
    }

    return PcState::Offline;
}

bool HostProbe::isRunning() const {
    return !mProcesses.empty();
}

void HostProbe::retireProcessAsync(std::unique_ptr<QProcess> proc) {
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

void HostProbe::eraseStoppingProcess(QProcess *proc) {
    auto it = std::find_if(mStoppingProcesses.begin(), mStoppingProcesses.end(),
                           [proc](const std::unique_ptr<QProcess> &item) {
                               return item.get() == proc;
                           });
    if (it != mStoppingProcesses.end()) {
        mStoppingProcesses.erase(it);
    }
}

void HostProbe::probe(const PcConfig &pc, const QString &sshUser, const SshParams &sshParams) {
    const auto existing = mProcesses.find(pc.id);
    if (existing != mProcesses.end()) {
        std::unique_ptr<QProcess> old = std::move(existing->second);
        mProcesses.erase(existing);
        retireProcessAsync(std::move(old));
    }

    SshParams params = sshParams;
    params.port = pc.port;
    const QString sshCmd = SshOptions::buildRsyncSshCommand(params);
    const QString remoteSpec = QStringLiteral("%1@%2:./").arg(sshUser, pc.host);

    std::unique_ptr<QProcess> procOwner = std::make_unique<QProcess>();
    QProcess *proc = procOwner.get();
    proc->setStandardInputFile(QProcess::nullDevice());
    configureRsyncChildProcess(proc);
    mProcesses.emplace(pc.id, std::move(procOwner));

    const QString pcId = pc.id;
    connect(proc, &QProcess::finished, this,
            [this, proc, pcId](int exitCode, QProcess::ExitStatus exitStatus) {
                const auto it = mProcesses.find(pcId);
                if (it == mProcesses.end() || it->second.get() != proc) {
                    return;
                }
                std::unique_ptr<QProcess> finishedProc = std::move(it->second);
                mProcesses.erase(it);
                const QString stderrText = QString::fromUtf8(finishedProc->readAllStandardError()).trimmed();
                retireProcessAsync(std::move(finishedProc));

                const PcState state = (exitStatus == QProcess::NormalExit)
                                          ? classifyProbeResult(exitCode, stderrText)
                                          : PcState::Offline;
                emit probeFinished(pcId, state, stderrText);
            });

    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, pcId](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) {
                    return;
                }
                const auto it = mProcesses.find(pcId);
                if (it == mProcesses.end() || it->second.get() != proc) {
                    return;
                }
                std::unique_ptr<QProcess> failedProc = std::move(it->second);
                mProcesses.erase(it);
                const QString detail = failedProc->errorString();
                retireProcessAsync(std::move(failedProc));
                emit probeFinished(pcId, PcState::Offline, detail);
            });

    proc->start(QStringLiteral("rsync"),
                {QStringLiteral("-d"),
                 QStringLiteral("--list-only"),
                 QStringLiteral("-e"),
                 sshCmd,
                 QStringLiteral("--"),
                 remoteSpec});
}

void HostProbe::cancel() {
    std::map<QString, std::unique_ptr<QProcess>> active;
    active.swap(mProcesses);
    for (auto &entry : active) {
        retireProcessAsync(std::move(entry.second));
    }
}

} // namespace dc
