#include "StubControllers.h"
#include "core/ConfigStore.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTime>

#include <algorithm>
#include <cmath>

namespace dc {

namespace {
constexpr qint64 kMb = 1000LL * 1000LL;
constexpr qint64 kGb = 1000LL * 1000LL * 1000LL;
constexpr qint64 kFallbackLocalFreeBytes = 250LL * kGb;
constexpr qint64 kFallbackLocalTotalBytes = 500LL * kGb;
constexpr int kMaxFailuresBeforeThrottle = 5;
constexpr int kThrottleDurationSeconds = 30;

void queryStubLocalDiskSpace(const QString &path, qint64 *outFree, qint64 *outTotal) {
    QString probe = QDir::cleanPath(path);
    while (!probe.isEmpty() && !QDir(probe).exists()) {
        const QString parent = QFileInfo(probe).path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }
    if (!probe.isEmpty() && QDir(probe).exists()) {
        const QStorageInfo storage(probe);
        if (storage.isValid() && storage.isReady() && storage.bytesAvailable() > 0) {
            if (outFree != nullptr) {
                *outFree = storage.bytesAvailable();
            }
            if (outTotal != nullptr) {
                *outTotal = storage.bytesTotal();
            }
            return;
        }
    }
    if (outFree != nullptr) {
        *outFree = kFallbackLocalFreeBytes;
    }
    if (outTotal != nullptr) {
        *outTotal = kFallbackLocalTotalBytes;
    }
}
} // namespace

// ============================================================================
// StubPcModel
// ============================================================================

StubPcModel::StubPcModel(QObject *parent)
    : QAbstractListModel(parent) {}

int StubPcModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return mItems.size();
}

QVariant StubPcModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= mItems.size()) {
        return {};
    }
    const Item &it = mItems.at(index.row());
    switch (role) {
    case IdRole:            return it.id;
    case HostRole:          return it.host;
    case StateRole:         return static_cast<int>(it.state);
    case StateTextRole:     return pcStateText(it.state);
    case StatusMessageRole: return pcStateMessage(it.state);
    case LogsBytesRole:          return it.logsBytes;
    case LogsCountRole:          return it.logsCount;
    case ShotsBytesRole:         return it.shotsBytes;
    case ShotsCountRole:         return it.shotsCount;
    case RecordingsBytesRole:    return it.recordingsBytes;
    case RecordingsCountRole:    return it.recordingsCount;
    case SupportsPicturesRole:   return it.supportsPictures;
    case SupportsRecordingsRole: return it.supportsRecordings;
    case TotalBytesRole:         return it.totalBytes();
    case SelectedRole:           return it.selected;
    case SelectableRole:         return it.selectable();
    default:                     return {};
    }
}

QHash<int, QByteArray> StubPcModel::roleNames() const {
    return {
        {IdRole, "id"},
        {HostRole, "host"},
        {StateRole, "state"},
        {StateTextRole, "stateText"},
        {StatusMessageRole, "statusMessage"},
        {LogsBytesRole, "logsBytes"},
        {LogsCountRole, "logsCount"},
        {ShotsBytesRole, "shotsBytes"},
        {ShotsCountRole, "shotsCount"},
        {RecordingsBytesRole, "recordingsBytes"},
        {RecordingsCountRole, "recordingsCount"},
        {SupportsPicturesRole, "supportsPictures"},
        {SupportsRecordingsRole, "supportsRecordings"},
        {TotalBytesRole, "totalBytes"},
        {SelectedRole, "selected"},
        {SelectableRole, "selectable"}
    };
}

void StubPcModel::setItems(const QVector<Item> &items) {
    beginResetModel();
    mItems = items;
    endResetModel();
}

void StubPcModel::notifyAllChanged() {
    if (!mItems.isEmpty()) {
        emit dataChanged(index(0), index(mItems.size() - 1));
    }
}

void StubPcModel::notifyRowChanged(int row) {
    if (row >= 0 && row < mItems.size()) {
        emit dataChanged(index(row), index(row));
    }
}

// ============================================================================
// StubDriveModel
// ============================================================================

StubDriveModel::StubDriveModel(QObject *parent)
    : QAbstractListModel(parent) {}

int StubDriveModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return mItems.size();
}

QVariant StubDriveModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= mItems.size()) {
        return {};
    }
    const Item &it = mItems.at(index.row());
    switch (role) {
    case DeviceRole:     return it.drive.device;
    case LabelRole:      return it.drive.label;
    case MountPointRole: return it.drive.mountPoint;
    case FreeBytesRole:  return it.drive.freeBytes;
    case TotalBytesRole: return it.drive.totalBytes;
    case SelectedRole:   return it.selected;
    default:             return {};
    }
}

QHash<int, QByteArray> StubDriveModel::roleNames() const {
    return {
        {DeviceRole, "device"},
        {LabelRole, "label"},
        {MountPointRole, "mountPoint"},
        {FreeBytesRole, "freeBytes"},
        {TotalBytesRole, "totalBytes"},
        {SelectedRole, "selected"}
    };
}

void StubDriveModel::setItems(const QVector<Item> &items) {
    beginResetModel();
    mItems = items;
    endResetModel();
}

void StubDriveModel::setSelectedIndex(int idx) {
    for (int i = 0; i < mItems.size(); ++i) {
        mItems[i].selected = (i == idx);
    }
    if (!mItems.isEmpty()) {
        emit dataChanged(index(0), index(mItems.size() - 1), {SelectedRole});
    }
}

// ============================================================================
// StubJobModel
// ============================================================================

StubJobModel::StubJobModel(QObject *parent)
    : QAbstractListModel(parent) {}

int StubJobModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return mItems.size();
}

QVariant StubJobModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= mItems.size()) {
        return {};
    }
    const Item &it = mItems.at(index.row());
    switch (role) {
    case IdRole:           return it.id;
    case HostRole:         return it.host;
    case StageRole:        return static_cast<int>(it.stage);
    case StageTextRole:    return jobStateText(it.stage);
    case CurrentFileRole:  return it.currentFile;
    case RateRole:         return it.rate;
    case PercentRole:      return it.percent;
    case CopiedBytesRole:  return it.copiedBytes;
    case TotalBytesRole:   return it.totalBytes;
    case StateRole:        return static_cast<int>(it.stage);
    case ErrorMessageRole: return it.errorMessage;
    default:               return {};
    }
}

QHash<int, QByteArray> StubJobModel::roleNames() const {
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

void StubJobModel::setItems(const QVector<Item> &items) {
    beginResetModel();
    mItems = items;
    endResetModel();
}

void StubJobModel::notifyRowChanged(int row) {
    if (row >= 0 && row < mItems.size()) {
        emit dataChanged(index(row), index(row));
    }
}

// ============================================================================
// StubAuthController
// ============================================================================

StubAuthController::StubAuthController(QObject *parent)
    : QObject(parent) {
    mUnlockTimer.setSingleShot(true);
    mUnlockTimer.setInterval(300);
    connect(&mUnlockTimer, &QTimer::timeout, this, [this]() {
        if (mPendingPassphrase == QStringLiteral("wrong")) {
            ++mFailureCount;
            mState = LoggedOut;
            if (mFailureCount >= kMaxFailuresBeforeThrottle) {
                mThrottleSeconds = kThrottleDurationSeconds;
                mError = QStringLiteral("Too many failed attempts. Please wait 30 seconds.");
                mThrottleTimer.start();
                emit throttleSecondsChanged();
            } else {
                mError = QStringLiteral("Incorrect passphrase. Check your key passphrase and try again.");
            }
            emit errorChanged();
            emit stateChanged();
        } else {
            mFailureCount = 0;
            mOperatorId = mPendingOperator;
            mError.clear();
            mState = LoggedIn;
            emit operatorIdChanged();
            emit errorChanged();
            emit stateChanged();
        }
        mPendingPassphrase.clear();
    });

    mThrottleTimer.setInterval(1000);
    connect(&mThrottleTimer, &QTimer::timeout, this, [this]() {
        if (mThrottleSeconds > 0) {
            --mThrottleSeconds;
            emit throttleSecondsChanged();
            if (mThrottleSeconds == 0) {
                mThrottleTimer.stop();
                mFailureCount = 0;
                mError.clear();
                emit errorChanged();
            } else {
                mError = QStringLiteral("Too many failed attempts. Please wait %1 seconds.")
                             .arg(mThrottleSeconds);
                emit errorChanged();
            }
        }
    });
}

void StubAuthController::unlock(const QString &operatorId, const QString &passphrase) {
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

    mError.clear();
    emit errorChanged();
    mPendingOperator = trimmedOp;
    mPendingPassphrase = passphrase;
    mState = Unlocking;
    emit stateChanged();
    mUnlockTimer.start();
}

void StubAuthController::lock() {
    mUnlockTimer.stop();
    mPendingPassphrase.clear();
    mError.clear();
    mState = LoggedOut;
    emit errorChanged();
    emit stateChanged();
}

// ============================================================================
// StubSourcesController
// ============================================================================

StubSourcesController::StubSourcesController(QObject *parent)
    : QObject(parent)
    , mPcModel(this)
    , mDriveModel(this) {
    mWindowStart = QDateTime(QDate(2026, 11, 10), QTime(8, 0, 0));
    mWindowEnd = QDateTime(QDate(2026, 11, 17), QTime(17, 0, 0));
    populateMockData();
    recalculateEstimate();

    mRescanTimer.setSingleShot(true);
    mRescanTimer.setInterval(450);
    connect(&mRescanTimer, &QTimer::timeout, this, [this]() {
        mBusy = false;
        emit busyChanged();
        recalculateEstimate();
    });
}

void StubSourcesController::populateMockData() {
    mBasePcs = {
        {QStringLiteral("pc01"), QStringLiteral("192.168.10.11"), PcState::Online,  38 * kMb, 42, 4710 * kMb, 1208, 1450 * kMb, 14, true,  true,  true},
        {QStringLiteral("pc02"), QStringLiteral("192.168.10.12"), PcState::Online,  41 * kMb, 45, 4660 * kMb, 1196, 1320 * kMb, 12, true,  true,  true},
        {QStringLiteral("pc03"), QStringLiteral("192.168.10.13"), PcState::Online,  36 * kMb, 40, 4820 * kMb, 1231, 1680 * kMb, 16, true,  true,  true},
        {QStringLiteral("pc04"), QStringLiteral("192.168.10.14"), PcState::Online,  44 * kMb, 47, 4590 * kMb, 1178, 1210 * kMb, 11, true,  true,  true},
        {QStringLiteral("pc05"), QStringLiteral("192.168.10.15"), PcState::Offline, 0,        0,  0,          0,    0,          0,  true,  true,  false},
        {QStringLiteral("pc06"), QStringLiteral("192.168.10.16"), PcState::Online,  39 * kMb, 43, 4740 * kMb, 1215, 1540 * kMb, 15, true,  true,  true},
        {QStringLiteral("pc07"), QStringLiteral("192.168.10.17"), PcState::Online,  52 * kMb, 64, 0,          0,    0,          0,  false, false, true}
    };
    mPcModel.setItems(mBasePcs);
    applyMediaOptionsToModel();

    QVector<StubDriveModel::Item> drives = {
        {{QStringLiteral("/dev/sdb1"), QStringLiteral("Kingston 64 GB"), QStringLiteral("/media/usb"),
          static_cast<qint64>(58.2 * kGb), 64 * kGb}, true},
        {{QStringLiteral("/dev/sdc1"), QStringLiteral("SanDisk 16 GB"), QStringLiteral("/media/usb1"),
          static_cast<qint64>(14.9 * kGb), 16 * kGb}, false}
    };
    mDriveModel.setItems(drives);
    mSelectedDriveIndex = 0;
    mUseLocalDisk = false;
    mLocalDiskPath = ConfigStore::expandTilde(QStringLiteral("~/DataCollector"));
    queryStubLocalDiskSpace(mLocalDiskPath, &mLocalDiskFreeBytes, &mLocalDiskTotalBytes);
}

void StubSourcesController::applyMediaOptionsToModel() {
    QVector<StubPcModel::Item> &items = mPcModel.items();
    const int count = std::min(items.size(), mBasePcs.size());
    for (int i = 0; i < count; ++i) {
        const StubPcModel::Item &base = mBasePcs.at(i);
        const bool picOn = mIncludePictures && base.supportsPictures;
        const bool recOn = mIncludeRecordings && base.supportsRecordings;
        items[i].shotsBytes = picOn ? base.shotsBytes : 0;
        items[i].shotsCount = picOn ? base.shotsCount : 0;
        items[i].recordingsBytes = recOn ? base.recordingsBytes : 0;
        items[i].recordingsCount = recOn ? base.recordingsCount : 0;
    }
    mPcModel.notifyAllChanged();
}

void StubSourcesController::setIncludePictures(bool include) {
    if (mIncludePictures == include) {
        return;
    }
    mIncludePictures = include;
    applyMediaOptionsToModel();
    emit mediaOptionsChanged();
    emit selectionChanged();
    recalculateEstimate();
}

void StubSourcesController::setIncludeRecordings(bool include) {
    if (mIncludeRecordings == include) {
        return;
    }
    mIncludeRecordings = include;
    applyMediaOptionsToModel();
    emit mediaOptionsChanged();
    emit selectionChanged();
    recalculateEstimate();
}

void StubSourcesController::setWindowStart(const QDateTime &dt) {
    if (mWindowStart == dt) {
        return;
    }
    mWindowStart = dt;
    mActivePreset.clear();
    emit windowChanged();
    emit canStartChanged();
}

void StubSourcesController::setWindowEnd(const QDateTime &dt) {
    if (mWindowEnd == dt) {
        return;
    }
    mWindowEnd = dt;
    mActivePreset.clear();
    emit windowChanged();
    emit canStartChanged();
}

bool StubSourcesController::windowValid() const {
    return mWindowStart.isValid() && mWindowEnd.isValid() && mWindowStart < mWindowEnd;
}

int StubSourcesController::totalPcCount() const {
    return mPcModel.items().size();
}

int StubSourcesController::onlinePcCount() const {
    int count = 0;
    for (const StubPcModel::Item &it : mPcModel.items()) {
        if (it.selectable()) {
            ++count;
        }
    }
    return count;
}

int StubSourcesController::selectedCount() const {
    int count = 0;
    for (const StubPcModel::Item &it : mPcModel.items()) {
        if (it.selectable() && it.selected) {
            ++count;
        }
    }
    return count;
}

bool StubSourcesController::allOnlineSelected() const {
    const int online = onlinePcCount();
    return online > 0 && selectedCount() == online;
}

qint64 StubSourcesController::selectedBytes() const {
    qint64 sum = 0;
    for (const StubPcModel::Item &it : mPcModel.items()) {
        if (it.selectable() && it.selected) {
            sum += it.totalBytes();
        }
    }
    return sum;
}

int StubSourcesController::driveState() const {
    if (mUseLocalDisk) {
        return (!mLocalDiskPath.isEmpty() && mLocalDiskFreeBytes > 0) ? Ready : None;
    }
    const int n = mDriveModel.items().size();
    if (n == 0) {
        return None;
    }
    if (n == 1) {
        return Ready;
    }
    return Multiple;
}

QVariantMap StubSourcesController::currentDrive() const {
    if (mUseLocalDisk) {
        return {
            {QStringLiteral("device"), QString()},
            {QStringLiteral("label"), QStringLiteral("Local disk")},
            {QStringLiteral("mountPoint"), mLocalDiskPath},
            {QStringLiteral("freeBytes"), mLocalDiskFreeBytes},
            {QStringLiteral("totalBytes"), mLocalDiskTotalBytes},
            {QStringLiteral("isLocal"), true}
        };
    }
    const QVector<StubDriveModel::Item> &list = mDriveModel.items();
    if (mSelectedDriveIndex < 0 || mSelectedDriveIndex >= list.size()) {
        return {};
    }
    const UsbDrive &d = list.at(mSelectedDriveIndex).drive;
    return {
        {QStringLiteral("device"), d.device},
        {QStringLiteral("label"), d.label},
        {QStringLiteral("mountPoint"), d.mountPoint},
        {QStringLiteral("freeBytes"), d.freeBytes},
        {QStringLiteral("totalBytes"), d.totalBytes},
        {QStringLiteral("isLocal"), false}
    };
}

qint64 StubSourcesController::freeBytes() const {
    if (mUseLocalDisk) {
        return mLocalDiskFreeBytes;
    }
    const QVector<StubDriveModel::Item> &list = mDriveModel.items();
    if (mSelectedDriveIndex < 0 || mSelectedDriveIndex >= list.size()) {
        return 0;
    }
    return list.at(mSelectedDriveIndex).drive.freeBytes;
}

QString StubSourcesController::destinationPreview() const {
    if (mUseLocalDisk) {
        if (mLocalDiskPath.isEmpty()) {
            return QStringLiteral("not set");
        }
        QString base = mLocalDiskPath;
        while (base.endsWith(QLatin1Char('/')) && base.size() > 1) {
            base.chop(1);
        }
        return base + QStringLiteral("/collect_<timestamp>/");
    }
    const QVector<StubDriveModel::Item> &list = mDriveModel.items();
    if (mSelectedDriveIndex < 0 || mSelectedDriveIndex >= list.size()) {
        return QStringLiteral("not set");
    }
    QString mount = list.at(mSelectedDriveIndex).drive.mountPoint;
    while (mount.endsWith(QLatin1Char('/')) && mount.size() > 1) {
        mount.chop(1);
    }
    return mount + QStringLiteral("/collect_<timestamp>/");
}

bool StubSourcesController::canStart() const {
    return windowValid() && selectedCount() > 0 && driveState() != None && mFits && !mBusy;
}

void StubSourcesController::rescanPcs() {
    if (mBusy) {
        return;
    }
    mBusy = true;
    emit busyChanged();
    emit canStartChanged();
    mRescanTimer.start();
}

void StubSourcesController::rescanDrives() {
    emit drivesChanged();
    recalculateEstimate();
}

void StubSourcesController::setPcSelected(int index, bool selected) {
    QVector<StubPcModel::Item> &items = mPcModel.items();
    if (index < 0 || index >= items.size() || !items[index].selectable()) {
        return;
    }
    if (items[index].selected == selected) {
        return;
    }
    items[index].selected = selected;
    mPcModel.notifyRowChanged(index);
    emit selectionChanged();
    recalculateEstimate();
}

void StubSourcesController::selectAll(bool selected) {
    QVector<StubPcModel::Item> &items = mPcModel.items();
    for (StubPcModel::Item &it : items) {
        if (it.selectable()) {
            it.selected = selected;
        }
    }
    mPcModel.notifyAllChanged();
    emit selectionChanged();
    recalculateEstimate();
}

void StubSourcesController::applyPreset(const QString &name) {
    const QDateTime now = QDateTime::currentDateTime();
    if (name == QStringLiteral("24h")) {
        mWindowEnd = now;
        mWindowStart = now.addSecs(-24 * 3600);
        mActivePreset = name;
    } else if (name == QStringLiteral("7d")) {
        mWindowEnd = now;
        mWindowStart = now.addDays(-7);
        mActivePreset = name;
    } else if (name == QStringLiteral("today")) {
        mWindowStart = QDateTime(now.date(), QTime(0, 0, 0));
        mWindowEnd = now;
        mActivePreset = name;
    } else {
        return;
    }
    emit windowChanged();
    emit canStartChanged();
}

void StubSourcesController::selectDrive(int index) {
    const QVector<StubDriveModel::Item> &list = mDriveModel.items();
    if (index < 0 || index >= list.size()) {
        return;
    }
    mUseLocalDisk = false;
    mSelectedDriveIndex = index;
    mDriveModel.setSelectedIndex(index);
    emit drivesChanged();
    recalculateEstimate();
}

void StubSourcesController::selectLocalDisk(const QString &path) {
    QString target = path.trimmed();
    if (target.isEmpty()) {
        target = mLocalDiskPath;
    } else {
        target = QDir::cleanPath(ConfigStore::expandTilde(target));
    }
    mUseLocalDisk = true;
    mLocalDiskPath = target;
    mSelectedDriveIndex = -1;
    mDriveModel.setSelectedIndex(-1);
    queryStubLocalDiskSpace(mLocalDiskPath, &mLocalDiskFreeBytes, &mLocalDiskTotalBytes);
    emit drivesChanged();
    recalculateEstimate();
}

void StubSourcesController::selectUsbMode() {
    mUseLocalDisk = false;
    mSelectedDriveIndex = mDriveModel.items().isEmpty() ? -1 : 0;
    mDriveModel.setSelectedIndex(mSelectedDriveIndex);
    emit drivesChanged();
    recalculateEstimate();
}

void StubSourcesController::startCopy() {
    if (!canStart()) {
        emit startError(QStringLiteral("Cannot start copy. Check time range, selected PCs, and USB space."));
        return;
    }
    emit copyRequested();
}

void StubSourcesController::recalculateEstimate() {
    const qint64 free = freeBytes();
    const qint64 needed = selectedBytes();
    const qint64 withMargin = static_cast<qint64>(std::ceil(static_cast<double>(needed) * 1.02));
    mFits = (driveState() != None) && (withMargin <= free);

    QVector<qint64> sizes;
    for (const StubPcModel::Item &it : mPcModel.items()) {
        if (it.selectable() && it.selected) {
            sizes.push_back(it.totalBytes());
        }
    }
    std::sort(sizes.begin(), sizes.end());
    qint64 accum = 0;
    int fitCount = 0;
    for (qint64 s : sizes) {
        const qint64 nextWithMargin = static_cast<qint64>(std::ceil(static_cast<double>(accum + s) * 1.02));
        if (nextWithMargin <= free) {
            accum += s;
            ++fitCount;
        } else {
            break;
        }
    }
    mFittingPcCount = fitCount;

    emit estimateChanged();
    emit canStartChanged();
}

// ============================================================================
// StubTransferController
// ============================================================================

StubTransferController::StubTransferController(QObject *parent)
    : QObject(parent)
    , mJobModel(this) {
    mTickTimer.setInterval(250);
    connect(&mTickTimer, &QTimer::timeout, this, &StubTransferController::onTick);
}

bool StubTransferController::canEject() const {
    if (mIsLocalDestination) {
        return false;
    }
    return mPhase == Finished || mPhase == FinishedWithWarnings || mPhase == Cancelled;
}

void StubTransferController::startFromSources(const StubSourcesController *sources) {
    mTickTimer.stop();
    mWarnings.clear();
    mEjectStatus.clear();
    mEjectError = false;
    mTickCounter = 0;
    mIsLocalDestination = sources->useLocalDisk();

    const QVariantMap drive = sources->currentDrive();
    QString mount = drive.value(QStringLiteral("mountPoint"), QStringLiteral("/media/usb")).toString();
    while (mount.endsWith(QLatin1Char('/')) && mount.size() > 1) {
        mount.chop(1);
    }
    mSessionPath = mount + QStringLiteral("/collect_")
                   + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    mWindowSummary = sources->windowStart().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                     + QStringLiteral(" to ")
                     + sources->windowEnd().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    mSelectedPcCount = sources->selectedCount();

    QVector<StubJobModel::Item> jobs;
    int activeStarted = 0;
    for (const StubPcModel::Item &pc : sources->pcModelRef().items()) {
        StubJobModel::Item job;
        job.id = pc.id;
        job.host = pc.host;
        if (!pc.selectable() || !pc.selected) {
            job.stage = JobState::Skipped;
            job.totalBytes = 0;
            job.copiedBytes = 0;
            job.percent = 0;
        } else {
            job.totalBytes = pc.totalBytes();
            if (activeStarted < 2) {
                job.stage = JobState::CopyingLogs;
                job.rate = (activeStarted == 0) ? (52.0 * kMb) : (34.0 * kMb);
                job.currentFile = QStringLiteral("app_2026-11-14.log");
                ++activeStarted;
            } else {
                job.stage = JobState::Queued;
            }
        }
        jobs.push_back(job);
    }
    mJobModel.setItems(jobs);

    emit warningsChanged();
    emit ejectStatusChanged();
    emit sessionChanged();

    recomputeTotals();
    setPhase(Running);
    mTickTimer.start();
}

void StubTransferController::cancel() {
    if (mPhase != Running && mPhase != UsbRemoved) {
        return;
    }
    mTickTimer.stop();
    QVector<StubJobModel::Item> &items = mJobModel.items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].stage == JobState::CopyingLogs
            || items[i].stage == JobState::CopyingScreenshots
            || items[i].stage == JobState::CopyingRecordings
            || items[i].stage == JobState::Verifying
            || items[i].stage == JobState::Queued) {
            items[i].stage = JobState::Cancelled;
            items[i].rate = 0.0;
            items[i].currentFile.clear();
            mJobModel.notifyRowChanged(i);
        }
    }
    recomputeTotals();
    setPhase(Cancelled);
}

void StubTransferController::retry() {
    if (mPhase == UsbRemoved) {
        setPhase(Running);
        mTickTimer.start();
    }
}

void StubTransferController::retryPc(const QString &pcId) {
    QVector<StubJobModel::Item> &items = mJobModel.items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].id == pcId && items[i].stage == JobState::Failed) {
            items[i].stage = JobState::CopyingScreenshots;
            items[i].errorMessage.clear();
            items[i].rate = 44.0 * kMb;
            mJobModel.notifyRowChanged(i);
            setPhase(Running);
            mTickTimer.start();
            break;
        }
    }
}

void StubTransferController::eject() {
    if (!canEject()) {
        return;
    }
    mEjectError = false;
    mEjectStatus = QStringLiteral("Safe to remove USB drive.");
    emit ejectStatusChanged();
}

void StubTransferController::resetToSources() {
    if (mPhase == Running || mPhase == Cancelling) {
        return;
    }
    mTickTimer.stop();
    setPhase(Idle);
    emit backToSourcesRequested();
}

void StubTransferController::onTick() {
    ++mTickCounter;
    QVector<StubJobModel::Item> &items = mJobModel.items();
    int activeCount = 0;

    for (int i = 0; i < items.size(); ++i) {
        StubJobModel::Item &job = items[i];
        if (job.stage == JobState::CopyingLogs
            || job.stage == JobState::CopyingScreenshots
            || job.stage == JobState::CopyingRecordings) {
            ++activeCount;
            // Advance ~220 MB per 250 ms tick in demo mode so full run takes ~12-15 seconds
            const qint64 step = (i % 2 == 0) ? (240 * kMb) : (175 * kMb);
            job.copiedBytes = std::min(job.totalBytes, job.copiedBytes + step);
            job.percent = job.totalBytes > 0
                              ? static_cast<int>((job.copiedBytes * 100) / job.totalBytes)
                              : 100;

            if (job.percent >= 100) {
                job.stage = JobState::Done;
                job.rate = 0.0;
                job.currentFile.clear();
                job.percent = 100;
            } else if (job.percent >= 5) {
                job.stage = JobState::CopyingScreenshots;
                job.rate = (i % 2 == 0) ? (52.0 * kMb) : (34.0 * kMb);
                job.currentFile = QStringLiteral("2026-11-%1_%2-%3-%4.png")
                                      .arg(10 + (mTickCounter % 8), 2, 10, QLatin1Char('0'))
                                      .arg(8 + (mTickCounter % 10), 2, 10, QLatin1Char('0'))
                                      .arg((mTickCounter * 7) % 60, 2, 10, QLatin1Char('0'))
                                      .arg((mTickCounter * 13) % 60, 2, 10, QLatin1Char('0'));
            } else {
                job.stage = JobState::CopyingLogs;
                job.rate = 28.0 * kMb;
                job.currentFile = QStringLiteral("app_2026-11-14.log");
            }
            mJobModel.notifyRowChanged(i);
        }
    }

    // Start queued jobs up to maxParallel = 2
    for (int i = 0; i < items.size() && activeCount < 2; ++i) {
        if (items[i].stage == JobState::Queued) {
            items[i].stage = JobState::CopyingLogs;
            items[i].rate = 38.0 * kMb;
            items[i].currentFile = QStringLiteral("app_2026-11-10.log");
            mJobModel.notifyRowChanged(i);
            ++activeCount;
        }
    }

    recomputeTotals();

    if (activeCount == 0) {
        mTickTimer.stop();
        setPhase(mWarnings.isEmpty() ? Finished : FinishedWithWarnings);
    }
}

void StubTransferController::recomputeTotals() {
    qint64 copied = 0;
    qint64 total = 0;
    double combinedRate = 0.0;

    for (const StubJobModel::Item &job : mJobModel.items()) {
        if (job.stage != JobState::Skipped) {
            copied += job.copiedBytes;
            total += job.totalBytes;
            combinedRate += job.rate;
        }
    }

    mCopiedBytes = copied;
    mTotalBytes = total;
    mOverallPercent = total > 0 ? static_cast<int>((copied * 100) / total) : 0;
    mRate = combinedRate;

    if (combinedRate > 1.0 && total > copied) {
        mEtaSeconds = static_cast<int>((total - copied) / combinedRate);
    } else {
        mEtaSeconds = -1;
    }

    emit progressChanged();
}

void StubTransferController::setPhase(Phase p) {
    if (mPhase == p) {
        return;
    }
    mPhase = p;
    emit phaseChanged();
}

} // namespace dc
