#pragma once

#include "core/TransferEngine.h"
#include "core/Types.h"

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dc {

class SourcesController;
class UsbMonitor;

class JobListModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY(JobListModel)

public:
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

    explicit JobListModel(QObject *parent);

    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setJobs(const QVector<JobStatus> &jobs);
    void updateJob(int row, const JobStatus &status);

    const QVector<JobStatus> &items() const { return mItems; }

private:
    QVector<JobStatus> mItems;
};

class TransferController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(TransferController)
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

    explicit TransferController(TransferEngine *engine,
                                UsbMonitor *usbMonitor,
                                QObject *parent);

    QAbstractListModel *jobModel() { return &mJobModel; }
    int overallPercent() const;
    qint64 copiedBytes() const;
    qint64 totalBytes() const;
    double rate() const;
    int etaSeconds() const;
    int phase() const;
    QStringList warnings() const;
    QString sessionPath() const { return mSessionPath; }
    QString windowSummary() const { return mWindowSummary; }
    int selectedPcCount() const { return mSelectedPcCount; }
    bool isLocalDestination() const { return mIsLocalDestination; }
    bool canEject() const;
    QString ejectStatus() const { return mEjectStatus; }
    bool ejectError() const { return mEjectError; }

    void startSession(const SourcesController *sources,
                      const QString &operatorId,
                      const QString &agentSocket);
    void shutdownNow();

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
    static Phase mapPhase(EnginePhase p);
    void onEnginePhaseChanged(dc::EnginePhase phase);
    void onEngineJobUpdated(int row);
    void onUsbDriveRemoved(const QString &mountPoint);
    void onUsbEjectFinished(bool ok, const QString &message);

    TransferEngine *mEngine = nullptr;
    UsbMonitor *mUsbMonitor = nullptr;

    JobListModel mJobModel;
    QString mSessionPath;
    QString mWindowSummary;
    int mSelectedPcCount = 0;

    UsbDrive mActiveDrive;
    bool mIsLocalDestination = false;
    QString mEjectStatus;
    bool mEjectError = false;
};

} // namespace dc
