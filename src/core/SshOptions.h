#pragma once

#include <QString>
#include <QStringList>

namespace dc {

struct SshParams {
    int port = 22;
    QString sshKeyPath;
    QString knownHostsPath;
    QString agentSocket;
    int connectTimeoutSec = 5;
    int serverAliveIntervalSec = 10;
    int serverAliveCountMax = 3;
};

class SshOptions {
public:
    static QStringList buildSshArgs(const SshParams &params);
    static QString buildRsyncSshCommand(const SshParams &params);
    static QString quoteForRsyncE(const QString &arg);
};

} // namespace dc
