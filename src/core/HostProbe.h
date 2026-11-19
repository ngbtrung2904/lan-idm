#pragma once

#include "SshOptions.h"
#include "Types.h"

#include <QObject>
#include <QString>

#include <map>
#include <memory>
#include <vector>

class QProcess;

namespace dc {

class HostProbe : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(HostProbe)

public:
    explicit HostProbe(QObject *parent);
    ~HostProbe() override;

    static PcState classifyProbeResult(int exitCode, const QString &stderrText);

    void probe(const PcConfig &pc, const QString &sshUser, const SshParams &sshParams);
    void cancel();
    bool isRunning() const;

signals:
    void probeFinished(const QString &pcId, dc::PcState state, const QString &detail);

private:
    void retireProcessAsync(std::unique_ptr<QProcess> proc);
    void eraseStoppingProcess(QProcess *proc);

    std::map<QString, std::unique_ptr<QProcess>> mProcesses;
    std::vector<std::unique_ptr<QProcess>> mStoppingProcesses;
};

} // namespace dc
