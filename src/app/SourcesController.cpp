#include "SourcesController.h"
#include "core/ConfigStore.h"
#include "core/KeyAgent.h"
#include "core/SpaceEstimator.h"
#include "core/TimeFilter.h"
#include "core/UsbMonitor.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTime>

namespace dc {

namespace {
constexpr int kMaxParallelProbes = 6;

bool queryLocalDiskSpace(const QString &path, qint64 *outFree, qint64 *outTotal) {
    if (outFree != nullptr) {
        *outFree = 0;
    }
    if (outTotal != nullptr) {
        *outTotal = 0;
    }
    if (path.isEmpty()) {
        return false;
    }

    QString probe = QDir::cleanPath(path);
    while (!probe.isEmpty() && !QDir(probe).exists()) {
        const QString parent = QFileInfo(probe).path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }
    if (probe.isEmpty() || !QDir(probe).exists()) {
        return false;
    }

    const QStorageInfo storage(probe);
    if (!storage.isValid() || !storage.isReady()) {
        return false;
    }

    const qint64 freeBytes = qMax<qint64>(0, storage.bytesAvailable());
    const qint64 totalBytes = qMax<qint64>(0, storage.bytesTotal());
    if (outFree != nullptr) {
        *outFree = freeBytes;
    }
    if (outTotal != nullptr) {
        *outTotal = totalBytes;
    }
    return true;
}
} // namespace

// ============================================================================
// PcListModel
// ============================================================================

PcListModel::PcListModel(QObject *parent)
    : QAbstractListModel(parent) {}

int PcListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return mItems.size();
}

QVariant PcListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= mItems.size()) {
        return {};
    }
    const Item &it = mItems.at(index.row());
    switch (role) {
    case IdRole:            return it.id;
    case HostRole:          return it.host;
    case StateRole:         return static_cast<int>(it.state);
    case StateTextRole:     return pcStateText(it.state);
    case StatusMessageRole:
        return it.statusDetail.isEmpty() ? pcStateMessage(it.state) : it.statusDetail;
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

QHash<int, QByteArray> PcListModel::roleNames() const {
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

void PcListModel::initializePcs(const QVector<PcConfig> &pcs) {
    if (mItems.size() == pcs.size()) {
        for (int i = 0; i < pcs.size(); ++i) {
            mItems[i].id = pcs[i].id;
            mItems[i].host = pcs[i].host;
            mItems[i].port = pcs[i].port;
            mItems[i].state = PcState::Unknown;
            mItems[i].statusDetail.clear();
            mItems[i].logsBytes = 0;
            mItems[i].logsCount = 0;
            mItems[i].shotsBytes = 0;
            mItems[i].shotsCount = 0;
            mItems[i].recordingsBytes = 0;
            mItems[i].recordingsCount = 0;
            mItems[i].supportsPictures = pcs[i].supportsPictures;
            mItems[i].supportsRecordings = pcs[i].supportsRecordings;
            mItems[i].selected = false;
        }
        notifyAllChanged();
        return;
    }

    beginResetModel();
    mItems.clear();
    mItems.reserve(pcs.size());
    for (const PcConfig &pc : pcs) {
        Item item;
        item.id = pc.id;
        item.host = pc.host;
        item.port = pc.port;
        item.state = PcState::Unknown;
        item.supportsPictures = pc.supportsPictures;
        item.supportsRecordings = pc.supportsRecordings;
        mItems.push_back(item);
    }
    endResetModel();
}

int PcListModel::indexOfPc(const QString &pcId) const {
    for (int i = 0; i < mItems.size(); ++i) {
        if (mItems[i].id == pcId) {
            return i;
        }
    }
    return -1;
}

void PcListModel::notifyRowChanged(int row) {
    if (row >= 0 && row < mItems.size()) {
        emit dataChanged(index(row), index(row));
    }
}

void PcListModel::notifyAllChanged() {
    if (!mItems.isEmpty()) {
        emit dataChanged(index(0), index(mItems.size() - 1));
    }
}

// ============================================================================
// DriveListModel
// ============================================================================

DriveListModel::DriveListModel(QObject *parent)
    : QAbstractListModel(parent) {}

int DriveListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return mItems.size();
}

QVariant DriveListModel::data(const QModelIndex &index, int role) const {
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

QHash<int, QByteArray> DriveListModel::roleNames() const {
    return {
        {DeviceRole, "device"},
        {LabelRole, "label"},
        {MountPointRole, "mountPoint"},
        {FreeBytesRole, "freeBytes"},
        {TotalBytesRole, "totalBytes"},
        {SelectedRole, "selected"}
    };
}

void DriveListModel::syncDrives(const QVector<UsbDrive> &drives, int selectedIndex) {
    bool sameOrder = (mItems.size() == drives.size());
    if (sameOrder) {
        for (int i = 0; i < drives.size(); ++i) {
            if (mItems[i].drive.device != drives[i].device
                || mItems[i].drive.mountPoint != drives[i].mountPoint) {
                sameOrder = false;
                break;
            }
        }
    }

    if (sameOrder) {
        for (int i = 0; i < drives.size(); ++i) {
            mItems[i].drive = drives[i];
            mItems[i].selected = (i == selectedIndex);
        }
        if (!mItems.isEmpty()) {
            emit dataChanged(index(0), index(mItems.size() - 1));
        }
        return;
    }

    beginResetModel();
    mItems.clear();
    mItems.reserve(drives.size());
    for (int i = 0; i < drives.size(); ++i) {
        Item item;
        item.drive = drives[i];
        item.selected = (i == selectedIndex);
        mItems.push_back(item);
    }
    endResetModel();
}

void DriveListModel::updateFreeBytes(int row, qint64 freeBytes) {
    if (row < 0 || row >= mItems.size()) {
        return;
    }
    if (mItems[row].drive.freeBytes == freeBytes) {
        return;
    }
    mItems[row].drive.freeBytes = freeBytes;
    emit dataChanged(index(row), index(row), {FreeBytesRole});
}

void DriveListModel::setSelectedIndex(int idx) {
    for (int i = 0; i < mItems.size(); ++i) {
        mItems[i].selected = (i == idx);
    }
    if (!mItems.isEmpty()) {
        emit dataChanged(index(0), index(mItems.size() - 1), {SelectedRole});
    }
}

// ============================================================================
// SourcesController
// ============================================================================

SourcesController::SourcesController(const AppConfig &config,
                                     KeyAgent *keyAgent,
                                     UsbMonitor *usbMonitor,
                                     QObject *parent)
    : QObject(parent)
    , mConfig(config)
    , mKeyAgent(keyAgent)
    , mUsbMonitor(usbMonitor)
    , mPcModel(this)
    , mDriveModel(this)
    , mHostProbe(this)
    , mRsyncLister(this) {
    mWindowEnd = QDateTime::currentDateTime();
    mWindowStart = mWindowEnd.addDays(-1);
    mActivePreset = QStringLiteral("24h");
    mLocalDiskPath = mConfig.localOutputPath.isEmpty()
                         ? ConfigStore::expandTilde(QStringLiteral("~/DataCollector"))
                         : mConfig.localOutputPath;
    queryLocalDiskSpace(mLocalDiskPath, &mLocalDiskFreeBytes, &mLocalDiskTotalBytes);

    mPcModel.initializePcs(mConfig.pcs);

    connect(&mHostProbe, &HostProbe::probeFinished,
            this, &SourcesController::onProbeFinished);
    connect(&mRsyncLister, &RsyncLister::listingFinished,
            this, &SourcesController::onListingFinished);
    connect(&mRsyncLister, &RsyncLister::listingFailed,
            this, &SourcesController::onListingFailed);

    if (mUsbMonitor != nullptr) {
        connect(mUsbMonitor, &UsbMonitor::drivesChanged,
                this, &SourcesController::onUsbDrivesChanged);
        connect(mUsbMonitor, &UsbMonitor::freeSpaceUpdated,
                this, &SourcesController::onUsbFreeSpaceUpdated);
        onUsbDrivesChanged();
    } else {
        recalculateEstimate();
    }
}

SourcesController::~SourcesController() {
    cancelPending();
}

void SourcesController::setWindowStart(const QDateTime &dt) {
    if (mWindowStart == dt) {
        return;
    }
    mWindowStart = dt;
    mActivePreset.clear();
    reapplyFilterAll();
    emit windowChanged();
    emit selectionChanged();
    recalculateEstimate();
}

void SourcesController::setWindowEnd(const QDateTime &dt) {
    if (mWindowEnd == dt) {
        return;
    }
    mWindowEnd = dt;
    mActivePreset.clear();
    reapplyFilterAll();
    emit windowChanged();
    emit selectionChanged();
    recalculateEstimate();
}

bool SourcesController::windowValid() const {
    return timeWindow().isValid();
}

int SourcesController::totalPcCount() const {
    return mPcModel.items().size();
}

int SourcesController::onlinePcCount() const {
    int count = 0;
    for (const PcListModel::Item &it : mPcModel.items()) {
        if (it.selectable()) {
            ++count;
        }
    }
    return count;
}

int SourcesController::selectedCount() const {
    int count = 0;
    for (const PcListModel::Item &it : mPcModel.items()) {
        if (it.selectable() && it.selected) {
            ++count;
        }
    }
    return count;
}

bool SourcesController::allOnlineSelected() const {
    const int online = onlinePcCount();
    return online > 0 && selectedCount() == online;
}

qint64 SourcesController::selectedBytes() const {
    qint64 sum = 0;
    for (const PcListModel::Item &it : mPcModel.items()) {
        if (it.selectable() && it.selected) {
            sum += it.totalBytes();
        }
    }
    return sum;
}

int SourcesController::driveState() const {
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

QVariantMap SourcesController::currentDrive() const {
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
    const QVector<DriveListModel::Item> &list = mDriveModel.items();
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

qint64 SourcesController::freeBytes() const {
    if (mUseLocalDisk) {
        return mLocalDiskFreeBytes;
    }
    const QVector<DriveListModel::Item> &list = mDriveModel.items();
    if (mSelectedDriveIndex < 0 || mSelectedDriveIndex >= list.size()) {
        return 0;
    }
    return list.at(mSelectedDriveIndex).drive.freeBytes;
}

QString SourcesController::destinationPreview() const {
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
    const QVector<DriveListModel::Item> &list = mDriveModel.items();
    if (mSelectedDriveIndex < 0 || mSelectedDriveIndex >= list.size()) {
        return QStringLiteral("not set");
    }
    QString mount = list.at(mSelectedDriveIndex).drive.mountPoint;
    while (mount.endsWith(QLatin1Char('/')) && mount.size() > 1) {
        mount.chop(1);
    }
    return mount + QStringLiteral("/collect_<timestamp>/");
}

void SourcesController::setIncludePictures(bool include) {
    if (mIncludePictures == include) {
        return;
    }
    mIncludePictures = include;
    reapplyFilterAll();
    emit mediaOptionsChanged();
    emit selectionChanged();
    recalculateEstimate();
}

void SourcesController::setIncludeRecordings(bool include) {
    if (mIncludeRecordings == include) {
        return;
    }
    mIncludeRecordings = include;
    reapplyFilterAll();
    emit mediaOptionsChanged();
    emit selectionChanged();
    recalculateEstimate();
}

bool SourcesController::canStart() const {
    return windowValid()
           && selectedCount() > 0
           && driveState() != None
           && mFits
           && !mBusy;
}

void SourcesController::cancelPending() {
    mProbeQueue.clear();
    mActiveProbes = 0;
    mActiveListings = 0;
    mHostProbe.cancel();
    mRsyncLister.cancel();
    updateBusyState();
}

void SourcesController::rescanPcs() {
    cancelPending();
    mListings.clear();
    mFiltered.clear();

    QVector<PcListModel::Item> &items = mPcModel.items();
    mProbeQueue.reserve(items.size());
    for (int i = 0; i < items.size(); ++i) {
        items[i].state = PcState::Checking;
        items[i].statusDetail.clear();
        items[i].logsBytes = 0;
        items[i].logsCount = 0;
        items[i].shotsBytes = 0;
        items[i].shotsCount = 0;
        items[i].recordingsBytes = 0;
        items[i].recordingsCount = 0;
        items[i].selected = false;
        mPcModel.notifyRowChanged(i);
        mProbeQueue.push_back(i);
    }

    updateBusyState();
    emit selectionChanged();
    recalculateEstimate();

    dispatchNextProbes();
}

void SourcesController::rescanDrives() {
    queryLocalDiskSpace(mLocalDiskPath, &mLocalDiskFreeBytes, &mLocalDiskTotalBytes);
    if (mUsbMonitor != nullptr) {
        mUsbMonitor->rescan();
        mUsbMonitor->refreshFreeSpace();
        if (mUseLocalDisk) {
            emit drivesChanged();
            recalculateEstimate();
        }
    } else {
        emit drivesChanged();
        recalculateEstimate();
    }
}

void SourcesController::setPcSelected(int index, bool selected) {
    QVector<PcListModel::Item> &items = mPcModel.items();
    if (index < 0 || index >= items.size() || !items[index].selectable()) {
        return;
    }
    if (items[index].selected == selected) {
        return;
    }
    items[index].selected = selected;
    if (selected) {
        mUserDeselectedPcs.remove(items[index].id);
    } else {
        mUserDeselectedPcs.insert(items[index].id);
    }
    mPcModel.notifyRowChanged(index);
    emit selectionChanged();
    recalculateEstimate();
}

void SourcesController::selectAll(bool selected) {
    QVector<PcListModel::Item> &items = mPcModel.items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].selectable()) {
            items[i].selected = selected;
            if (selected) {
                mUserDeselectedPcs.remove(items[i].id);
            } else {
                mUserDeselectedPcs.insert(items[i].id);
            }
            mPcModel.notifyRowChanged(i);
        }
    }
    emit selectionChanged();
    recalculateEstimate();
}

void SourcesController::applyPreset(const QString &name) {
    const QDateTime now = QDateTime::currentDateTime();
    if (name == QStringLiteral("24h")) {
        mWindowEnd = now;
        mWindowStart = now.addDays(-1);
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

    reapplyFilterAll();
    emit windowChanged();
    emit selectionChanged();
    recalculateEstimate();
}

void SourcesController::selectDrive(int index) {
    const QVector<DriveListModel::Item> &list = mDriveModel.items();
    if (index < 0 || index >= list.size()) {
        return;
    }
    mUseLocalDisk = false;
    mSelectedDriveIndex = index;
    if (mUsbMonitor != nullptr) {
        mUsbMonitor->selectDrive(index);
    }
    mDriveModel.setSelectedIndex(index);
    emit drivesChanged();
    recalculateEstimate();
}

void SourcesController::selectLocalDisk(const QString &path) {
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
    queryLocalDiskSpace(mLocalDiskPath, &mLocalDiskFreeBytes, &mLocalDiskTotalBytes);
    emit drivesChanged();
    recalculateEstimate();
}

void SourcesController::startCopy() {
    if (mBusy) {
        emit startError(QStringLiteral("Still checking PCs. Please wait until scanning finishes."));
        return;
    }

    // 1. Re-read destination free space right before starting
    if (mUseLocalDisk) {
        queryLocalDiskSpace(mLocalDiskPath, &mLocalDiskFreeBytes, &mLocalDiskTotalBytes);
        emit drivesChanged();
    } else if (mUsbMonitor != nullptr) {
        mUsbMonitor->refreshFreeSpace();
        onUsbDrivesChanged();
    }

    // 2. Re-validate time window, selection, destination, and space estimate
    const TimeWindow win = timeWindow();
    if (!win.isValid()) {
        emit startError(QStringLiteral("The selected time range is invalid. Start time must be before end time."));
        return;
    }

    if (selectedCount() <= 0) {
        emit startError(QStringLiteral("Select at least one online PC before starting."));
        return;
    }

    UsbDrive targetDrive;
    if (mUseLocalDisk) {
        if (mLocalDiskPath.isEmpty() || mLocalDiskFreeBytes <= 0) {
            emit startError(QStringLiteral("No valid local destination folder is configured."));
            return;
        }
        targetDrive = UsbDrive{QString(),
                               QStringLiteral("Local disk"),
                               mLocalDiskPath,
                               mLocalDiskFreeBytes,
                               mLocalDiskTotalBytes};
    } else {
        const QVector<DriveListModel::Item> &drivesList = mDriveModel.items();
        if (mSelectedDriveIndex < 0 || mSelectedDriveIndex >= drivesList.size()) {
            emit startError(QStringLiteral("No USB drive is connected. Plug in a USB drive or choose a local folder."));
            return;
        }
        targetDrive = drivesList.at(mSelectedDriveIndex).drive;
    }

    QVector<qint64> selectedSizes;
    for (const PcListModel::Item &it : mPcModel.items()) {
        if (it.selectable() && it.selected) {
            selectedSizes.push_back(it.totalBytes());
        }
    }
    const SpaceEstimate est = SpaceEstimator::estimate(selectedSizes,
                                                       targetDrive.freeBytes,
                                                       SpaceEstimator::kDefaultMarginFraction);
    mFits = est.fits;
    mFittingPcCount = est.fittingPcCount;
    emit estimateChanged();
    emit canStartChanged();

    if (!est.fits) {
        emit startError(QStringLiteral("Not enough free space on %1 for the selected PCs.")
                            .arg(targetDrive.label.isEmpty() ? QStringLiteral("the destination") : targetDrive.label));
        return;
    }

    // 3. Create session folder and test writability
    QString mountPoint = targetDrive.mountPoint;
    while (mountPoint.endsWith(QLatin1Char('/')) && mountPoint.size() > 1) {
        mountPoint.chop(1);
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString sessionDir = mountPoint + QStringLiteral("/collect_") + stamp;

    if (!QDir().mkpath(sessionDir)) {
        emit startError(mUseLocalDisk
                            ? QStringLiteral("Could not create destination folder in %1. Check folder permissions.")
                                  .arg(targetDrive.mountPoint)
                            : QStringLiteral("Could not create destination folder on %1. Check that the drive is mounted and writable.")
                                  .arg(targetDrive.mountPoint));
        return;
    }

    const QString probeFilePath = sessionDir + QStringLiteral("/.dc_write_test");
    QFile probeFile(probeFilePath);
    if (!probeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir().rmdir(sessionDir);
        emit startError(mUseLocalDisk
                            ? QStringLiteral("The local folder %1 is not writable. Check folder permissions.")
                                  .arg(targetDrive.mountPoint)
                            : QStringLiteral("The USB drive at %1 is not writable. Check drive permissions or mount options.")
                                  .arg(targetDrive.mountPoint));
        return;
    }
    probeFile.close();
    probeFile.remove();

    // 4. Build TransferJobSpec list (selected online PCs + skipped rows)
    QVector<TransferJobSpec> jobs;
    const QVector<PcListModel::Item> &pcItems = mPcModel.items();
    jobs.reserve(pcItems.size());

    for (int i = 0; i < pcItems.size(); ++i) {
        const PcListModel::Item &pcItem = pcItems[i];
        TransferJobSpec spec;
        if (i < mConfig.pcs.size() && mConfig.pcs[i].id == pcItem.id) {
            spec.pc = mConfig.pcs[i];
        } else {
            spec.pc = PcConfig{pcItem.id, pcItem.host, pcItem.port, {},
                               pcItem.supportsPictures, pcItem.supportsRecordings, QString()};
        }

        if (pcItem.selectable() && pcItem.selected) {
            spec.skipped = false;
            if (mFiltered.contains(pcItem.id)) {
                spec.files = mFiltered.value(pcItem.id);
            } else {
                spec.files = FilteredPc{};
                spec.files.pcId = pcItem.id;
            }
        } else {
            spec.skipped = true;
            spec.skipReason = !pcItem.selectable()
                                  ? pcStateText(pcItem.state)
                                  : QStringLiteral("Not selected");
        }
        jobs.push_back(spec);
    }

    mPreparedSessionDir = sessionDir;
    mPreparedJobs = jobs;
    mPreparedDrive = targetDrive;
    mPreparedIsLocalDisk = mUseLocalDisk;

    // 5. Transition to progress view
    emit copyRequested();
}

SshParams SourcesController::buildSshParams(int port) const {
    SshParams params;
    params.port = port;
    params.sshKeyPath = mConfig.sshKeyPath;
    params.knownHostsPath = mConfig.knownHostsPath;
    params.agentSocket = (mKeyAgent != nullptr) ? mKeyAgent->socketPath() : QString();
    return params;
}

void SourcesController::dispatchNextProbes() {
    while (!mProbeQueue.isEmpty() && mActiveProbes < kMaxParallelProbes) {
        const int row = mProbeQueue.takeFirst();
        if (row < 0 || row >= mConfig.pcs.size()) {
            continue;
        }
        const PcConfig &pc = mConfig.pcs.at(row);
        ++mActiveProbes;
        mHostProbe.probe(pc, resolveSshUser(pc, mConfig.sshUser), buildSshParams(pc.port));
    }
    updateBusyState();
}

void SourcesController::onProbeFinished(const QString &pcId, dc::PcState state, const QString &detail) {
    Q_UNUSED(detail);
    if (mActiveProbes > 0) {
        --mActiveProbes;
    }

    const int row = mPcModel.indexOfPc(pcId);
    if (row >= 0) {
        PcListModel::Item &item = mPcModel.items()[row];
        item.state = state;

        if (state == PcState::Online) {
            item.selected = !mUserDeselectedPcs.contains(pcId);
            item.statusDetail.clear();
            mPcModel.notifyRowChanged(row);

            if (row < mConfig.pcs.size()) {
                const PcConfig &pc = mConfig.pcs.at(row);
                ++mActiveListings;
                mRsyncLister.listPc(pc,
                                    resolveSshUser(pc, mConfig.sshUser),
                                    resolveLogSources(pc, mConfig.paths),
                                    mConfig.paths.screenshots,
                                    mConfig.paths.recordings,
                                    buildSshParams(pc.port));
            }
        } else {
            item.selected = false;
            item.logsBytes = 0;
            item.logsCount = 0;
            item.shotsBytes = 0;
            item.shotsCount = 0;
            item.recordingsBytes = 0;
            item.recordingsCount = 0;
            item.statusDetail.clear();
            mPcModel.notifyRowChanged(row);
        }
    }

    dispatchNextProbes();
    updateBusyState();
    emit selectionChanged();
    recalculateEstimate();
}

void SourcesController::onListingFinished(const QString &pcId, const dc::PcListing &listing) {
    if (mActiveListings > 0) {
        --mActiveListings;
    }

    mListings.insert(pcId, listing);

    const int row = mPcModel.indexOfPc(pcId);
    if (row >= 0) {
        applyFilterToRow(row);
    }

    updateBusyState();
    emit selectionChanged();
    recalculateEstimate();
}

void SourcesController::onListingFailed(const QString &pcId, const QString &error) {
    Q_UNUSED(error);
    if (mActiveListings > 0) {
        --mActiveListings;
    }

    const int row = mPcModel.indexOfPc(pcId);
    if (row >= 0) {
        PcListModel::Item &item = mPcModel.items()[row];
        item.state = PcState::Offline;
        item.selected = false;
        item.logsBytes = 0;
        item.logsCount = 0;
        item.shotsBytes = 0;
        item.shotsCount = 0;
        item.recordingsBytes = 0;
        item.recordingsCount = 0;
        item.statusDetail = QStringLiteral("Could not list files on this PC. Check the PC and click Rescan PCs.");
        mPcModel.notifyRowChanged(row);
    }

    updateBusyState();
    emit selectionChanged();
    recalculateEstimate();
}

void SourcesController::applyFilterToRow(int row) {
    QVector<PcListModel::Item> &items = mPcModel.items();
    if (row < 0 || row >= items.size()) {
        return;
    }

    PcListModel::Item &item = items[row];
    const auto it = mListings.constFind(item.id);
    if (it == mListings.constEnd()) {
        return;
    }

    const QVector<LogSourceConfig> logSources =
        (row < mConfig.pcs.size() && mConfig.pcs.at(row).id == item.id)
            ? resolveLogSources(mConfig.pcs.at(row), mConfig.paths)
            : resolveLogSources(PcConfig{}, mConfig.paths);
    const FilteredPc filtered = TimeFilter::filterPc(it.value(),
                                                     logSources,
                                                     timeWindow(),
                                                     mConfig.logs.includeModifiedAfterEnd,
                                                     mIncludePictures && item.supportsPictures,
                                                     mIncludeRecordings && item.supportsRecordings);
    mFiltered.insert(item.id, filtered);

    item.logsBytes = filtered.logsBytes;
    item.logsCount = filtered.logs.size();
    item.shotsBytes = filtered.shotsBytes;
    item.shotsCount = filtered.screenshots.size();
    item.recordingsBytes = filtered.recordingsBytes;
    item.recordingsCount = filtered.recordings.size();
    mPcModel.notifyRowChanged(row);
}

void SourcesController::reapplyFilterAll() {
    const int count = mPcModel.items().size();
    for (int i = 0; i < count; ++i) {
        applyFilterToRow(i);
    }
}

void SourcesController::onUsbDrivesChanged() {
    if (mUsbMonitor == nullptr) {
        return;
    }
    const QVector<UsbDrive> drivesList = mUsbMonitor->drives();
    if (mUseLocalDisk) {
        mSelectedDriveIndex = -1;
        mDriveModel.syncDrives(drivesList, -1);
        queryLocalDiskSpace(mLocalDiskPath, &mLocalDiskFreeBytes, &mLocalDiskTotalBytes);
    } else {
        mSelectedDriveIndex = mUsbMonitor->selectedIndex();
        mDriveModel.syncDrives(drivesList, mSelectedDriveIndex);
    }
    emit drivesChanged();
    recalculateEstimate();
}

void SourcesController::onUsbFreeSpaceUpdated(qint64 freeBytes) {
    if (mUseLocalDisk) {
        return;
    }
    if (mSelectedDriveIndex >= 0 && mSelectedDriveIndex < mDriveModel.items().size()) {
        mDriveModel.updateFreeBytes(mSelectedDriveIndex, freeBytes);
    }
    emit drivesChanged();
    recalculateEstimate();
}

void SourcesController::recalculateEstimate() {
    const qint64 free = freeBytes();
    QVector<qint64> selectedSizes;
    for (const PcListModel::Item &it : mPcModel.items()) {
        if (it.selectable() && it.selected) {
            selectedSizes.push_back(it.totalBytes());
        }
    }

    const SpaceEstimate est = SpaceEstimator::estimate(selectedSizes,
                                                       free,
                                                       SpaceEstimator::kDefaultMarginFraction);
    const bool newFits = (driveState() != None) && est.fits;
    const int newFitCount = (driveState() != None) ? est.fittingPcCount : 0;

    mFits = newFits;
    mFittingPcCount = newFitCount;

    emit estimateChanged();
    emit canStartChanged();
}

void SourcesController::updateBusyState() {
    const bool nowBusy = !mProbeQueue.isEmpty()
                         || mActiveProbes > 0
                         || mActiveListings > 0
                         || mHostProbe.isRunning()
                         || mRsyncLister.isRunning();
    if (mBusy != nowBusy) {
        mBusy = nowBusy;
        emit busyChanged();
        emit canStartChanged();
    }
}

} // namespace dc
