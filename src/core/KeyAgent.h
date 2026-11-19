#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <vector>

class QProcess;

namespace dc {

class KeyAgent : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(KeyAgent)

public:
    explicit KeyAgent(QObject *parent);
    ~KeyAgent() override;

    void unlock(const QString &keyPath, const QString &passphrase);
    void lock();
    void lockSync();

    QString socketPath() const;
    bool isUnlocked() const;

    static QString findAskpassScript();

signals:
    void unlocked();
    void unlockFailed(const QString &reason);

private:
    void startAgentAsync();
    void pollAgentSocket();
    void launchAddProcess();
    void clearPendingUnlock();
    void cleanupAddProcess(bool syncWait);
    void retireProcessAsync(std::unique_ptr<QProcess> proc, const QString &socketToRemove);
    void eraseStoppingProcess(QProcess *proc);
    static QString buildSocketPath();
    static QString classifyAddError(int exitCode, const QString &stderrText);

    QString mSocketPath;
    std::unique_ptr<QProcess> mAgentProcess;
    std::unique_ptr<QProcess> mAddProcess;
    std::vector<std::unique_ptr<QProcess>> mStoppingProcesses;
    QStringList mStoppingSockets;
    QTimer mSocketPollTimer;
    QElapsedTimer mAgentWaitTimer;
    QString mPendingKeyPath;
    QString mPendingAskpassPath;
    QByteArray mPendingPassUtf8;
    bool mUnlocked = false;
};

} // namespace dc
