#pragma once

#include "core/Types.h"

#include <QObject>
#include <QString>
#include <QTimer>

namespace dc {

class KeyAgent;

class AuthController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(AuthController)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString operatorId READ operatorId NOTIFY operatorIdChanged)
    Q_PROPERTY(int throttleSeconds READ throttleSeconds NOTIFY throttleSecondsChanged)
    Q_PROPERTY(QString logsDisplayPath READ logsDisplayPath CONSTANT)
    Q_PROPERTY(QString screenshotsDisplayPath READ screenshotsDisplayPath CONSTANT)

public:
    enum AuthState {
        LoggedOut = 0,
        Unlocking = 1,
        LoggedIn = 2
    };
    Q_ENUM(AuthState)

    explicit AuthController(const AppConfig &config,
                            KeyAgent *keyAgent,
                            QObject *parent);

    int state() const { return static_cast<int>(mState); }
    QString error() const { return mError; }
    QString operatorId() const { return mOperatorId; }
    int throttleSeconds() const { return mThrottleSeconds; }
    QString logsDisplayPath() const { return mLogsDisplayPath; }
    QString screenshotsDisplayPath() const { return mScreenshotsDisplayPath; }

    KeyAgent *keyAgent() const { return mKeyAgent; }

    Q_INVOKABLE void unlock(const QString &operatorId, const QString &passphrase);
    Q_INVOKABLE void lock();
    void lockSync();

signals:
    void stateChanged();
    void errorChanged();
    void operatorIdChanged();
    void throttleSecondsChanged();

private:
    void onKeyAgentUnlocked();
    void onKeyAgentUnlockFailed(const QString &reason);
    void onThrottleTick();

    static QString buildDisplayPath(const QString &sshUser, const QString &relPath);

    AppConfig mConfig;
    KeyAgent *mKeyAgent = nullptr;

    AuthState mState = LoggedOut;
    QString mError;
    QString mOperatorId;
    QString mPendingOperatorId;
    QString mLogsDisplayPath;
    QString mScreenshotsDisplayPath;

    int mFailureCount = 0;
    int mThrottleSeconds = 0;
    QTimer mThrottleTimer;
};

} // namespace dc
