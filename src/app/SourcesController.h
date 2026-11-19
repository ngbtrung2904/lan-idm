#pragma once

#include "core/HostProbe.h"
#include "core/RsyncLister.h"
#include "core/TransferEngine.h"
#include "core/Types.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace dc {

class KeyAgent;
class UsbMonitor;

class PcListModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY(PcListModel)

public:
    struct Item {
        QString id;
        QString host;
        int port = 22;
        PcState state = PcState::Unknown;
        QString statusDetail;
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

    explicit PcListModel(QObject *parent);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void initializePcs(const QVector<PcConfig> &pcs);
    int indexOfPc(const QString &pcId) const;

    const QVector<Item> &items() const { return mItems; }
    QVector<Item> &items() { return mItems; }

    void notifyRowChanged(int row);
    void notifyAllChanged();

private:
    QVector<Item> mItems;
};

class DriveListModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY(DriveListModel)

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

    explicit DriveListModel(QObject *parent);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void syncDrives(const QVector<UsbDrive> &drives, int selectedIndex);
    void updateFreeBytes(int row, qint64 freeBytes);
    void setSelectedIndex(int idx);

    const QVector<Item> &items() const { return mItems; }

private:
    QVector<Item> mItems;
};

class SourcesController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(SourcesController)
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

    explicit SourcesController(const AppConfig &config,
                               KeyAgent *keyAgent,
                               UsbMonitor *usbMonitor,
                               QObject *parent);
    ~SourcesController() override;

    QAbstractListModel *pcModel() { return &mPcModel; }
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
    bool fits() const { return mFits; }
    int fittingPcCount() const { return mFittingPcCount; }
    QString destinationPreview() const;
    bool includePictures() const { return mIncludePictures; }
    void setIncludePictures(bool include);
    bool includeRecordings() const { return mIncludeRecordings; }
    void setIncludeRecordings(bool include);
    bool canStart() const;
    bool busy() const { return mBusy; }

    const AppConfig &config() const { return mConfig; }
    TimeWindow timeWindow() const { return TimeWindow{mWindowStart, mWindowEnd}; }
    QString preparedSessionDir() const { return mPreparedSessionDir; }
    const QVector<TransferJobSpec> &preparedJobs() const { return mPreparedJobs; }
    UsbDrive preparedDrive() const { return mPreparedDrive; }

    void cancelPending();

    Q_INVOKABLE void rescanPcs();
    Q_INVOKABLE void rescanDrives();
    Q_INVOKABLE void setPcSelected(int index, bool selected);
    Q_INVOKABLE void selectAll(bool selected);
    Q_INVOKABLE void applyPreset(const QString &name);
    Q_INVOKABLE void selectDrive(int index);
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
    SshParams buildSshParams(int port) const;
    void dispatchNextProbes();
    void onProbeFinished(const QString &pcId, dc::PcState state, const QString &detail);
    void onListingFinished(const QString &pcId, const dc::PcListing &listing);
    void onListingFailed(const QString &pcId, const QString &error);
    void applyFilterToRow(int row);
    void reapplyFilterAll();
    void onUsbDrivesChanged();
    void onUsbFreeSpaceUpdated(qint64 freeBytes);
    void recalculateEstimate();
    void updateBusyState();

    AppConfig mConfig;
    KeyAgent *mKeyAgent = nullptr;
    UsbMonitor *mUsbMonitor = nullptr;

    PcListModel mPcModel;
    DriveListModel mDriveModel;

    HostProbe mHostProbe;
    RsyncLister mRsyncLister;

    QDateTime mWindowStart;
    QDateTime mWindowEnd;
    QString mActivePreset;

    QVector<int> mProbeQueue;
    int mActiveProbes = 0;
    int mActiveListings = 0;

    QSet<QString> mUserDeselectedPcs;
    QHash<QString, PcListing> mListings;
    QHash<QString, FilteredPc> mFiltered;

    int mSelectedDriveIndex = -1;
    bool mIncludePictures = true;
    bool mIncludeRecordings = false;
    bool mFits = false;
    int mFittingPcCount = 0;
    bool mBusy = false;

    QString mPreparedSessionDir;
    QVector<TransferJobSpec> mPreparedJobs;
    UsbDrive mPreparedDrive;
};

} // namespace dc
