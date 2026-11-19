#include "SshOptions.h"

namespace dc {

QStringList SshOptions::buildSshArgs(const SshParams &params) {
    QStringList args;
    args.reserve(22);

    const int port = (params.port >= 1 && params.port <= 65535) ? params.port : 22;
    const int connectTimeout = params.connectTimeoutSec > 0 ? params.connectTimeoutSec : 5;
    const int aliveInterval = params.serverAliveIntervalSec > 0 ? params.serverAliveIntervalSec : 10;
    const int aliveCountMax = params.serverAliveCountMax > 0 ? params.serverAliveCountMax : 3;

    args << QStringLiteral("-p") << QString::number(port);

    if (!params.sshKeyPath.isEmpty()) {
        args << QStringLiteral("-i") << params.sshKeyPath;
    }

    args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    args << QStringLiteral("-o") << QStringLiteral("ConnectTimeout=%1").arg(connectTimeout);
    args << QStringLiteral("-o") << QStringLiteral("ServerAliveInterval=%1").arg(aliveInterval);
    args << QStringLiteral("-o") << QStringLiteral("ServerAliveCountMax=%1").arg(aliveCountMax);
    args << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=yes");

    if (!params.knownHostsPath.isEmpty()) {
        args << QStringLiteral("-o")
             << QStringLiteral("UserKnownHostsFile=%1").arg(params.knownHostsPath);
    }

    args << QStringLiteral("-o") << QStringLiteral("IdentitiesOnly=yes");
    args << QStringLiteral("-o")
         << QStringLiteral("IdentityAgent=%1")
                .arg(params.agentSocket.isEmpty() ? QStringLiteral("none") : params.agentSocket);

    return args;
}

QString SshOptions::quoteForRsyncE(const QString &arg) {
    bool needsQuoting = arg.isEmpty();
    bool hasSingleQuote = false;

    for (const QChar ch : arg) {
        if (ch.isSpace() || ch == QLatin1Char('"') || ch == QLatin1Char('\\')) {
            needsQuoting = true;
        } else if (ch == QLatin1Char('\'')) {
            needsQuoting = true;
            hasSingleQuote = true;
        }
    }

    if (!needsQuoting) {
        return arg;
    }

    if (!hasSingleQuote) {
        return QLatin1Char('\'') + arg + QLatin1Char('\'');
    }

    QString escaped;
    escaped.reserve(arg.size() + 4);
    escaped.append(QLatin1Char('"'));
    for (const QChar ch : arg) {
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\\')) {
            escaped.append(QLatin1Char('\\'));
        }
        escaped.append(ch);
    }
    escaped.append(QLatin1Char('"'));
    return escaped;
}

QString SshOptions::buildRsyncSshCommand(const SshParams &params) {
    QStringList tokens;
    const QStringList sshArgs = buildSshArgs(params);
    tokens.reserve(sshArgs.size() + 1);
    tokens.append(QStringLiteral("ssh"));
    for (const QString &arg : sshArgs) {
        tokens.append(quoteForRsyncE(arg));
    }
    return tokens.join(QLatin1Char(' '));
}

} // namespace dc
