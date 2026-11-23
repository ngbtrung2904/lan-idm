#include "TransferController.h"
#include "SourcesController.h"
#include "core/UsbMonitor.h"

#include <QDir>

namespace dc {

// ============================================================================
// JobListModel
// ============================================================================

JobListModel::JobListModel(QObject *parent)
    : QAbstractListModel(parent) {}

int JobListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return mItems.size();
}

QVariant JobListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= mItems.size()) {
        return {};
    }
    const JobStatus &it = mItems.at(index.row());
    switch (role) {
    case IdRole:           return it.pcId;
    case HostRole:         return it.host;
    case StageRole:        return static_cast<int>(it.state);
    case StageTextRole:    return jobStateText(it.state);
    case CurrentFileRole:  return it.currentFile;
    case RateRole:         return it.rateBytesPerSec;
    case PercentRole:      return it.percent;
    case CopiedBytesRole:  return it.copiedBytes;
    case TotalBytesRole:   return it.totalBytes;
    case StateRole:        return static_cast<int>(it.state);
    case ErrorMessageRole: return it.errorMessage;
    default:               return {};
    }
}

QHash<int, QByteArray> JobListModel::roleNames() const {
    return {
        {IdRole, "id"},
        {HostRole, "host"},
        {StageRole, "stage"},
        {StageTextRole, "stageText"},
        {CurrentFileRole, "currentFile"},
        {RateRole, "rate"},
        {PercentRole, "percent"},
        {CopiedBytesRole, "copiedBytes"},
        {TotalBytesRole, "totalBytes"},
        {StateRole, "state"},
        {ErrorMessageRole, "errorMessage"}
    };
}

void JobListModel::setJobs(const QVector<JobStatus> &jobs) {
    beginResetModel();
    mItems = jobs;
    endResetModel();
}

void JobListModel::updateJob(int row, const JobStatus &status) {
    if (row < 0 || row >= mItems.size()) {
        return;
    }
    mItems[row] = status;
    emit dataChanged(index(row), index(row));
}

// ============================================================================
// TransferController
// ============================================================================

TransferController::TransferController(TransferEngine *engine,
                                       UsbMonitor *usbMonitor,
                                       QObject *parent)
    : QObject(parent)
    , mEngine(engine)
    , mUsbMonitor(usbMonitor)
    , mJobModel(this) {
    if (mEngine != nullptr) {
        connect(mEngine, &TransferEngine::phaseChanged,
                this, &TransferController::onEnginePhaseChanged);
        connect(mEngine, &TransferEngine::jobUpdated,
                this, &TransferController::onEngineJobUpdated);
        connect(mEngine, &TransferEngine::overallProgressChanged,
                this, &TransferController::progressChanged);
        connect(mEngine, &TransferEngine::warningsChanged,
                this, &TransferController::warningsChanged);
    }

    if (mUsbMonitor != nullptr) {
        connect(mUsbMonitor, &UsbMonitor::driveRemoved,
                this, &TransferController::onUsbDriveRemoved);
        connect(mUsbMonitor, &UsbMonitor::ejectFinished,
                this, &TransferController::onUsbEjectFinished);
    }
}

int TransferController::overallPercent() const {
    return (mEngine != nullptr) ? mEngine->overallPercent() : 0;
}

qint64 TransferController::copiedBytes() const {
    return (mEngine != nullptr) ? mEngine->copiedBytes() : 0;
}

qint64 TransferController::totalBytes() const {
    return (mEngine != nullptr) ? mEngine->totalBytes() : 0;
}

double TransferController::rate() const {
    return (mEngine != nullptr) ? mEngine->combinedRate() : 0.0;
}

int TransferController::etaSeconds() const {
    return (mEngine != nullptr) ? mEngine->etaSeconds() : -1;
}

int TransferController::phase() const {
    return (mEngine != nullptr) ? static_cast<int>(mapPhase(mEngine->phase())) : static_cast<int>(Idle);
}

QStringList TransferController::warnings() const {
    return (mEngine != nullptr) ? mEngine->warnings() : QStringList{};
}

bool TransferController::canEject() const {
    if (mIsLocalDestination || mEngine == nullptr) {
        return false;
    }
    const Phase p = mapPhase(mEngine->phase());
    return p == Finished || p == FinishedWithWarnings || p == Cancelled || p == Idle;
}

void TransferController::startSession(const SourcesController *sources,
                                      const QString &operatorId,
                                      const QString &agentSocket) {
    if (mEngine == nullptr || sources == nullptr) {
        return;
    }

    mEjectStatus.clear();
    mEjectError = false;
    emit ejectStatusChanged();

    mActiveDrive = sources->preparedDrive();
    mIsLocalDestination = sources->preparedIsLocalDisk();
    mSessionPath = sources->preparedSessionDir();
    mWindowSummary = sources->windowStart().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                     + QStringLiteral(" to ")
                     + sources->windowEnd().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    const QVector<TransferJobSpec> &specs = sources->preparedJobs();
    QVector<JobStatus> initialJobs;
    initialJobs.reserve(specs.size());
    int selectedCount = 0;

    for (const TransferJobSpec &spec : specs) {
        JobStatus st;
        st.pcId = spec.pc.id;
        st.host = spec.pc.host;
        if (spec.skipped) {
            st.state = JobState::Skipped;
            st.totalBytes = 0;
            st.copiedBytes = 0;
            st.percent = 0;
        } else {
            ++selectedCount;
            st.state = JobState::Queued;
            st.totalBytes = spec.files.totalBytes();
            st.copiedBytes = 0;
            st.percent = 0;
        }
        initialJobs.push_back(st);
    }

    mSelectedPcCount = selectedCount;
    mJobModel.setJobs(initialJobs);
    emit sessionChanged();

#ifdef APP_VERSION
    const QString version = QStringLiteral(APP_VERSION);
#else
    const QString version = QStringLiteral("0.1.0");
#endif

    mEngine->startSession(sources->config(),
                          agentSocket,
                          operatorId,
                          sources->timeWindow(),
                          mSessionPath,
                          specs,
                          version);

    // Sync any immediate job state updates without resetting the model
    const int count = mEngine->jobCount();
    for (int i = 0; i < count; ++i) {
        mJobModel.updateJob(i, mEngine->jobAt(i));
    }

    emit progressChanged();
    emit warningsChanged();
    emit phaseChanged();
}

void TransferController::shutdownNow() {
    if (mEngine != nullptr) {
        mEngine->shutdownNow();
    }
}

void TransferController::cancel() {
    if (mEngine != nullptr) {
        mEngine->cancel();
    }
}

void TransferController::retry() {
    if (mEngine == nullptr) {
        return;
    }

    if (mIsLocalDestination) {
        mEngine->retryAfterUsbReturned();
        return;
    }

    if (mUsbMonitor != nullptr) {
        mUsbMonitor->rescan();
        const QVector<UsbDrive> drives = mUsbMonitor->drives();
        bool driveMounted = false;
        for (const UsbDrive &d : drives) {
            if (!mActiveDrive.mountPoint.isEmpty() && d.mountPoint == mActiveDrive.mountPoint) {
                driveMounted = true;
                break;
            }
        }
        if (!driveMounted) {
            return;
        }
    }

    if (!mActiveDrive.mountPoint.isEmpty() && !QDir(mActiveDrive.mountPoint).exists()) {
        return;
    }

    mEngine->retryAfterUsbReturned();
}

void TransferController::retryPc(const QString &pcId) {
    if (mEngine != nullptr) {
        mEngine->retryPc(pcId);
    }
}

void TransferController::eject() {
    if (!canEject() || mUsbMonitor == nullptr) {
        return;
    }
    if (mActiveDrive.mountPoint.isEmpty()) {
        mEjectError = true;
        mEjectStatus = QStringLiteral("No USB drive is available to eject.");
        emit ejectStatusChanged();
        return;
    }

    mEjectError = false;
    mEjectStatus = QStringLiteral("Ejecting USB drive...");
    emit ejectStatusChanged();

    mUsbMonitor->ejectDrive(mActiveDrive.device, mActiveDrive.mountPoint);
}

void TransferController::resetToSources() {
    const int p = phase();
    if (p == Running || p == Cancelling) {
        return;
    }
    emit backToSourcesRequested();
}

TransferController::Phase TransferController::mapPhase(EnginePhase p) {
    switch (p) {
    case EnginePhase::Idle:                 return Idle;
    case EnginePhase::Running:              return Running;
    case EnginePhase::Finished:             return Finished;
    case EnginePhase::FinishedWithWarnings: return FinishedWithWarnings;
    case EnginePhase::UsbRemoved:           return UsbRemoved;
    case EnginePhase::Cancelling:           return Cancelling;
    case EnginePhase::Cancelled:            return Cancelled;
    }
    return Idle;
}

void TransferController::onEnginePhaseChanged(dc::EnginePhase /*phase*/) {
    emit phaseChanged();
}

void TransferController::onEngineJobUpdated(int row) {
    if (mEngine == nullptr) {
        return;
    }
    mJobModel.updateJob(row, mEngine->jobAt(row));
}

void TransferController::onUsbDriveRemoved(const QString &mountPoint) {
    if (mIsLocalDestination || mEngine == nullptr) {
        return;
    }
    if (mEngine->phase() == EnginePhase::Running
        && !mActiveDrive.mountPoint.isEmpty()
        && mountPoint == mActiveDrive.mountPoint) {
        mEngine->pauseForUsbRemoved();
    }
}

void TransferController::onUsbEjectFinished(bool ok, const QString &message) {
    mEjectError = !ok;
    mEjectStatus = message.isEmpty()
                       ? (ok ? QStringLiteral("Safe to remove USB drive.")
                             : QStringLiteral("Could not eject USB drive."))
                       : message;
    emit ejectStatusChanged();
}

} // namespace dc
