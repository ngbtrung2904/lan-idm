#include "AuthController.h"
#include "core/KeyAgent.h"

namespace dc {

namespace {
constexpr int kMaxFailuresBeforeThrottle = 5;
constexpr int kThrottleDurationSeconds = 30;
constexpr int kThrottleTickIntervalMs = 1000;
} // namespace

AuthController::AuthController(const AppConfig &config,
                               KeyAgent *keyAgent,
                               QObject *parent)
    : QObject(parent)
    , mConfig(config)
    , mKeyAgent(keyAgent)
    , mLogsDisplayPath(buildDisplayPath(config.sshUser, config.paths.logs))
    , mScreenshotsDisplayPath(buildDisplayPath(config.sshUser, config.paths.screenshots)) {
    if (mKeyAgent != nullptr) {
        connect(mKeyAgent, &KeyAgent::unlocked,
                this, &AuthController::onKeyAgentUnlocked);
        connect(mKeyAgent, &KeyAgent::unlockFailed,
                this, &AuthController::onKeyAgentUnlockFailed);
    }

    mThrottleTimer.setInterval(kThrottleTickIntervalMs);
    connect(&mThrottleTimer, &QTimer::timeout,
            this, &AuthController::onThrottleTick);
}

QString AuthController::buildDisplayPath(const QString &sshUser, const QString &relPath) {
    QString cleaned = relPath;
    while (cleaned.startsWith(QLatin1Char('/'))) {
        cleaned.remove(0, 1);
    }
    return QStringLiteral("/home/%1/%2").arg(sshUser, cleaned);
}

void AuthController::unlock(const QString &operatorId, const QString &passphrase) {
    if (mState == Unlocking || mThrottleSeconds > 0) {
        return;
    }

    const QString trimmedOp = operatorId.trimmed();
    if (trimmedOp.isEmpty()) {
        mError = QStringLiteral("Enter an operator ID.");
        emit errorChanged();
        return;
    }

    if (passphrase.isEmpty()) {
        mError = QStringLiteral("Enter your passphrase.");
        emit errorChanged();
        return;
    }

    if (mKeyAgent == nullptr) {
        mError = QStringLiteral("SSH key agent is unavailable.");
        emit errorChanged();
        return;
    }

    if (!mError.isEmpty()) {
        mError.clear();
        emit errorChanged();
    }

    mPendingOperatorId = trimmedOp;
    mState = Unlocking;
    emit stateChanged();

    mKeyAgent->unlock(mConfig.sshKeyPath, passphrase);
}

void AuthController::lock() {
    if (mKeyAgent != nullptr) {
        mKeyAgent->lock();
    }

    mPendingOperatorId.clear();

    const bool hadError = !mError.isEmpty();
    const bool hadOperator = !mOperatorId.isEmpty();
    const bool stateDiff = (mState != LoggedOut);

    mError.clear();
    mOperatorId.clear();
    mState = LoggedOut;

    if (hadError) {
        emit errorChanged();
    }
    if (hadOperator) {
        emit operatorIdChanged();
    }
    if (stateDiff) {
        emit stateChanged();
    }
}

void AuthController::lockSync() {
    if (mKeyAgent != nullptr) {
        mKeyAgent->lockSync();
    }

    mPendingOperatorId.clear();

    const bool hadError = !mError.isEmpty();
    const bool hadOperator = !mOperatorId.isEmpty();
    const bool stateDiff = (mState != LoggedOut);

    mError.clear();
    mOperatorId.clear();
    mState = LoggedOut;

    if (hadError) {
        emit errorChanged();
    }
    if (hadOperator) {
        emit operatorIdChanged();
    }
    if (stateDiff) {
        emit stateChanged();
    }
}

void AuthController::onKeyAgentUnlocked() {
    mFailureCount = 0;
    if (mThrottleSeconds > 0) {
        mThrottleTimer.stop();
        mThrottleSeconds = 0;
        emit throttleSecondsChanged();
    }

    if (mOperatorId != mPendingOperatorId) {
        mOperatorId = mPendingOperatorId;
        emit operatorIdChanged();
    }
    mPendingOperatorId.clear();

    if (!mError.isEmpty()) {
        mError.clear();
        emit errorChanged();
    }

    if (mState != LoggedIn) {
        mState = LoggedIn;
        emit stateChanged();
    }
}

void AuthController::onKeyAgentUnlockFailed(const QString &reason) {
    mPendingOperatorId.clear();
    ++mFailureCount;
    mState = LoggedOut;

    if (mFailureCount >= kMaxFailuresBeforeThrottle) {
        mThrottleSeconds = kThrottleDurationSeconds;
        mError = QStringLiteral("Too many failed attempts. Please wait %1 seconds.")
                     .arg(mThrottleSeconds);
        mThrottleTimer.start();
        emit throttleSecondsChanged();
    } else if (!reason.trimmed().isEmpty()) {
        mError = reason.trimmed();
    } else {
        mError = QStringLiteral("Incorrect passphrase. Check your key passphrase and try again.");
    }

    emit errorChanged();
    emit stateChanged();
}

void AuthController::onThrottleTick() {
    if (mThrottleSeconds <= 0) {
        mThrottleTimer.stop();
        return;
    }

    --mThrottleSeconds;
    emit throttleSecondsChanged();

    if (mThrottleSeconds == 0) {
        mThrottleTimer.stop();
        mFailureCount = 0;
        if (!mError.isEmpty()) {
            mError.clear();
            emit errorChanged();
        }
    } else {
        mError = QStringLiteral("Too many failed attempts. Please wait %1 seconds.")
                     .arg(mThrottleSeconds);
        emit errorChanged();
    }
}

} // namespace dc
