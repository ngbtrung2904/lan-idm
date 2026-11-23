#pragma once

#include "core/Types.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

namespace dc {

class StubPcModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY(StubPcModel)

public:
    struct Item {
        QString id;
        QString host;
        PcState state = PcState::Unknown;
        qint64 logsBytes = 0;
        int logsCount = 0;
        qint64 shotsBytes = 0;
        int shotsCount = 0;
        qint64 recordingsBytes = 0;
        int recordingsCount = 0;
        bool supportsPictures = true;
        bool supportsRecordings = true;
        bool selected = false;

        qint64 totalBytes() const { return logsBytes + shotsBytes + recordingsBytes; }
        bool selectable() const { return state == PcState::Online; }
    };

    enum Roles {
        IdRole = Qt::UserRole + 1,
        HostRole,
        StateRole,
        StateTextRole,
        StatusMessageRole,
        LogsBytesRole,
        LogsCountRole,
        ShotsBytesRole,
        ShotsCountRole,
        RecordingsBytesRole,
        RecordingsCountRole,
        SupportsPicturesRole,
        SupportsRecordingsRole,
        TotalBytesRole,
        SelectedRole,
        SelectableRole
    };

    explicit StubPcModel(QObject *parent);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<Item> &items);
    const QVector<Item> &items() const { return mItems; }
    QVector<Item> &items() { return mItems; }
    void notifyAllChanged();
    void notifyRowChanged(int row);

private:
    QVector<Item> mItems;
};

class StubDriveModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY(StubDriveModel)

public:
    struct Item {
        UsbDrive drive;
        bool selected = false;
    };

    enum Roles {
        DeviceRole = Qt::UserRole + 1,
        LabelRole,
        MountPointRole,
        FreeBytesRole,
        TotalBytesRole,
        SelectedRole
    };

    explicit StubDriveModel(QObject *parent);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<Item> &items);
    const QVector<Item> &items() const { return mItems; }
    void setSelectedIndex(int idx);

private:
    QVector<Item> mItems;
};

class StubJobModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY(StubJobModel)

public:
    struct Item {
        QString id;
        QString host;
        JobState stage = JobState::Queued;
        QString currentFile;
        double rate = 0.0;
        int percent = 0;
        qint64 copiedBytes = 0;
        qint64 totalBytes = 0;
        QString errorMessage;
    };

    enum Roles {
        IdRole = Qt::UserRole + 1,
        HostRole,
        StageRole,
        StageTextRole,
        CurrentFileRole,
        RateRole,
        PercentRole,
        CopiedBytesRole,
        TotalBytesRole,
        StateRole,
        ErrorMessageRole
    };

    explicit StubJobModel(QObject *parent);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QVector<Item> &items);
    const QVector<Item> &items() const { return mItems; }
    QVector<Item> &items() { return mItems; }
    void notifyRowChanged(int row);

private:
    QVector<Item> mItems;
};

class StubAuthController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(StubAuthController)
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

    explicit StubAuthController(QObject *parent);

    int state() const { return static_cast<int>(mState); }
    QString error() const { return mError; }
    QString operatorId() const { return mOperatorId; }
    int throttleSeconds() const { return mThrottleSeconds; }
    QString logsDisplayPath() const { return QStringLiteral("/home/user/.app"); }
    QString screenshotsDisplayPath() const { return QStringLiteral("/home/user/Pictures/lightshot"); }

    Q_INVOKABLE void unlock(const QString &operatorId, const QString &passphrase);
    Q_INVOKABLE void lock();

signals:
    void stateChanged();
    void errorChanged();
    void operatorIdChanged();
    void throttleSecondsChanged();

private:
    AuthState mState = LoggedOut;
    QString mError;
    QString mOperatorId = QStringLiteral("operator01");
    int mFailureCount = 0;
    int mThrottleSeconds = 0;
    QTimer mUnlockTimer;
    QTimer mThrottleTimer;
    QString mPendingOperator;
    QString mPendingPassphrase;
};

class StubSourcesController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(StubSourcesController)
    Q_PROPERTY(QAbstractListModel *pcModel READ pcModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *drives READ drives CONSTANT)
    Q_PROPERTY(QDateTime windowStart READ windowStart WRITE setWindowStart NOTIFY windowChanged)
    Q_PROPERTY(QDateTime windowEnd READ windowEnd WRITE setWindowEnd NOTIFY windowChanged)
    Q_PROPERTY(bool windowValid READ windowValid NOTIFY windowChanged)
    Q_PROPERTY(QString activePreset READ activePreset NOTIFY windowChanged)
    Q_PROPERTY(int totalPcCount READ totalPcCount NOTIFY selectionChanged)
    Q_PROPERTY(int onlinePcCount READ onlinePcCount NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(bool allOnlineSelected READ allOnlineSelected NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectedBytes READ selectedBytes NOTIFY selectionChanged)
    Q_PROPERTY(int driveState READ driveState NOTIFY drivesChanged)
    Q_PROPERTY(QVariantMap currentDrive READ currentDrive NOTIFY drivesChanged)
    Q_PROPERTY(qint64 freeBytes READ freeBytes NOTIFY drivesChanged)
    Q_PROPERTY(bool useLocalDisk READ useLocalDisk NOTIFY drivesChanged)
    Q_PROPERTY(QString localDiskPath READ localDiskPath NOTIFY drivesChanged)
    Q_PROPERTY(qint64 localDiskFreeBytes READ localDiskFreeBytes NOTIFY drivesChanged)
    Q_PROPERTY(bool fits READ fits NOTIFY estimateChanged)
    Q_PROPERTY(int fittingPcCount READ fittingPcCount NOTIFY estimateChanged)
    Q_PROPERTY(QString destinationPreview READ destinationPreview NOTIFY drivesChanged)
    Q_PROPERTY(bool includePictures READ includePictures WRITE setIncludePictures NOTIFY mediaOptionsChanged)
    Q_PROPERTY(bool includeRecordings READ includeRecordings WRITE setIncludeRecordings NOTIFY mediaOptionsChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY canStartChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    enum DriveState {
        None = 0,
        Ready = 1,
        Multiple = 2
    };
    Q_ENUM(DriveState)

    explicit StubSourcesController(QObject *parent);

    QAbstractListModel *pcModel() { return &mPcModel; }
    const StubPcModel &pcModelRef() const { return mPcModel; }
    QAbstractListModel *drives() { return &mDriveModel; }

    QDateTime windowStart() const { return mWindowStart; }
    void setWindowStart(const QDateTime &dt);
    QDateTime windowEnd() const { return mWindowEnd; }
    void setWindowEnd(const QDateTime &dt);
    bool windowValid() const;
    QString activePreset() const { return mActivePreset; }

    int totalPcCount() const;
    int onlinePcCount() const;
    int selectedCount() const;
    bool allOnlineSelected() const;
    qint64 selectedBytes() const;

    int driveState() const;
    QVariantMap currentDrive() const;
    qint64 freeBytes() const;
    bool useLocalDisk() const { return mUseLocalDisk; }
    QString localDiskPath() const { return mLocalDiskPath; }
    qint64 localDiskFreeBytes() const { return mLocalDiskFreeBytes; }
    bool fits() const { return mFits; }
    int fittingPcCount() const { return mFittingPcCount; }
    QString destinationPreview() const;
    bool includePictures() const { return mIncludePictures; }
    void setIncludePictures(bool include);
    bool includeRecordings() const { return mIncludeRecordings; }
    void setIncludeRecordings(bool include);
    bool canStart() const;
    bool busy() const { return mBusy; }

    Q_INVOKABLE void rescanPcs();
    Q_INVOKABLE void rescanDrives();
    Q_INVOKABLE void setPcSelected(int index, bool selected);
    Q_INVOKABLE void selectAll(bool selected);
    Q_INVOKABLE void applyPreset(const QString &name);
    Q_INVOKABLE void selectDrive(int index);
    Q_INVOKABLE void selectLocalDisk(const QString &path);
    Q_INVOKABLE void selectUsbMode();
    Q_INVOKABLE void startCopy();

signals:
    void windowChanged();
    void selectionChanged();
    void drivesChanged();
    void mediaOptionsChanged();
    void estimateChanged();
    void canStartChanged();
    void busyChanged();
    void copyRequested();
    void startError(const QString &message);

private:
    void populateMockData();
    void applyMediaOptionsToModel();
    void recalculateEstimate();

    StubPcModel mPcModel;
    QVector<StubPcModel::Item> mBasePcs;
    StubDriveModel mDriveModel;
    QDateTime mWindowStart;
    QDateTime mWindowEnd;
    QString mActivePreset = QStringLiteral("7d");
    int mSelectedDriveIndex = 0;
    bool mUseLocalDisk = false;
    QString mLocalDiskPath;
    qint64 mLocalDiskFreeBytes = 0;
    qint64 mLocalDiskTotalBytes = 0;
    bool mIncludePictures = true;
    bool mIncludeRecordings = false;
    bool mFits = true;
    int mFittingPcCount = 0;
    bool mBusy = false;
    QTimer mRescanTimer;
};

class StubTransferController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(StubTransferController)
    Q_PROPERTY(QAbstractListModel *jobModel READ jobModel CONSTANT)
    Q_PROPERTY(int overallPercent READ overallPercent NOTIFY progressChanged)
    Q_PROPERTY(qint64 copiedBytes READ copiedBytes NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY progressChanged)
    Q_PROPERTY(double rate READ rate NOTIFY progressChanged)
    Q_PROPERTY(int etaSeconds READ etaSeconds NOTIFY progressChanged)
    Q_PROPERTY(int phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY warningsChanged)
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY sessionChanged)
    Q_PROPERTY(QString windowSummary READ windowSummary NOTIFY sessionChanged)
    Q_PROPERTY(int selectedPcCount READ selectedPcCount NOTIFY sessionChanged)
    Q_PROPERTY(bool isLocalDestination READ isLocalDestination NOTIFY sessionChanged)
    Q_PROPERTY(bool canEject READ canEject NOTIFY phaseChanged)
    Q_PROPERTY(QString ejectStatus READ ejectStatus NOTIFY ejectStatusChanged)
    Q_PROPERTY(bool ejectError READ ejectError NOTIFY ejectStatusChanged)

public:
    enum Phase {
        Idle = 0,
        Running = 1,
        Finished = 2,
        FinishedWithWarnings = 3,
        UsbRemoved = 4,
        Cancelling = 5,
        Cancelled = 6
    };
    Q_ENUM(Phase)

    explicit StubTransferController(QObject *parent);

    QAbstractListModel *jobModel() { return &mJobModel; }
    int overallPercent() const { return mOverallPercent; }
    qint64 copiedBytes() const { return mCopiedBytes; }
    qint64 totalBytes() const { return mTotalBytes; }
    double rate() const { return mRate; }
    int etaSeconds() const { return mEtaSeconds; }
    int phase() const { return static_cast<int>(mPhase); }
    QStringList warnings() const { return mWarnings; }
    QString sessionPath() const { return mSessionPath; }
    QString windowSummary() const { return mWindowSummary; }
    int selectedPcCount() const { return mSelectedPcCount; }
    bool isLocalDestination() const { return mIsLocalDestination; }
    bool canEject() const;
    QString ejectStatus() const { return mEjectStatus; }
    bool ejectError() const { return mEjectError; }

    void startFromSources(const StubSourcesController *sources);

    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void retryPc(const QString &pcId);
    Q_INVOKABLE void eject();
    Q_INVOKABLE void resetToSources();

signals:
    void progressChanged();
    void phaseChanged();
    void warningsChanged();
    void sessionChanged();
    void ejectStatusChanged();
    void backToSourcesRequested();

private:
    void onTick();
    void recomputeTotals();
    void setPhase(Phase p);

    StubJobModel mJobModel;
    int mOverallPercent = 0;
    qint64 mCopiedBytes = 0;
    qint64 mTotalBytes = 0;
    double mRate = 0.0;
    int mEtaSeconds = -1;
    Phase mPhase = Idle;
    QStringList mWarnings;
    QString mSessionPath;
    QString mWindowSummary;
    int mSelectedPcCount = 0;
    bool mIsLocalDestination = false;
    QString mEjectStatus;
    bool mEjectError = false;
    int mTickCounter = 0;
    QTimer mTickTimer;
};

} // namespace dc
