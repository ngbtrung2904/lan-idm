#include "UsbMonitor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QSet>
#include <QStorageInfo>
#include <QtGlobal>

#include <algorithm>
#include <utility>

namespace dc {

namespace {

constexpr int kDefaultPollIntervalMs = 2000;
constexpr int kEjectSyncWaitMs = 1000;
constexpr int kKillTimeoutMs = 500;

bool readRemovableFlag(const QString &sysBlockName) {
    if (sysBlockName.isEmpty()) {
        return false;
    }

    QFile file(QStringLiteral("/sys/block/%1/removable").arg(sysBlockName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QByteArray content = file.readAll().trimmed();
    return content == "1";
}

void terminateProcessSync(QProcess *proc, QObject *receiver) {
    if (!proc) {
        return;
    }
    proc->disconnect(receiver);
    if (proc->state() != QProcess::NotRunning) {
        proc->terminate();
        if (!proc->waitForFinished(kEjectSyncWaitMs)) {
            proc->kill();
            proc->waitForFinished(kKillTimeoutMs);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// LinuxVolumeProvider
// ---------------------------------------------------------------------------

bool LinuxVolumeProvider::isDeviceRemovable(const QString &device) {
    if (device.isEmpty() || !device.startsWith(QLatin1String("/dev/"))) {
        return false;
    }

    QString resolvedDevice = QFileInfo(device).canonicalFilePath();
    if (resolvedDevice.isEmpty()) {
        resolvedDevice = device;
    }

    const QString devName = QFileInfo(resolvedDevice).fileName();
    if (devName.isEmpty()) {
        return false;
    }

    const QString sysClassPath = QStringLiteral("/sys/class/block/%1").arg(devName);
    const QString canonicalSysPath = QFileInfo(sysClassPath).canonicalFilePath();
    if (canonicalSysPath.isEmpty()) {
        return readRemovableFlag(devName);
    }

    // Typical sysfs canonical paths:
    //   Partition:  /sys/devices/.../block/sdb/sdb1   -> parent = sdb
    //   Whole disk: /sys/devices/.../block/sdb        -> parent = sdb
    const QStringList parts = canonicalSysPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const int blockIdx = parts.indexOf(QStringLiteral("block"));
    if (blockIdx >= 0 && blockIdx + 1 < parts.size()) {
        return readRemovableFlag(parts.at(blockIdx + 1));
    }

    if (readRemovableFlag(devName)) {
        return true;
    }

    const QString parentDir = QFileInfo(canonicalSysPath).dir().dirName();
    if (!parentDir.isEmpty() && parentDir != devName) {
        return readRemovableFlag(parentDir);
    }

    return false;
}

bool LinuxVolumeProvider::isMediaMountPoint(const QString &mountPoint) {
    if (mountPoint.isEmpty()) {
        return false;
    }

    const QString clean = QDir::cleanPath(mountPoint);
    return clean == QLatin1String("/media")
           || clean.startsWith(QLatin1String("/media/"))
           || clean.startsWith(QLatin1String("/run/media/"));
}

QString LinuxVolumeProvider::overridePath() {
    const QString raw = qEnvironmentVariable("DC_USB_OVERRIDE_PATH").trimmed();
    if (raw.isEmpty()) {
        return {};
    }

    const QFileInfo fi(raw);
    if (!fi.exists() || !fi.isDir() || !fi.isWritable()) {
        return {};
    }

    return QDir::cleanPath(fi.absoluteFilePath());
}

QVector<RawVolume> LinuxVolumeProvider::mountedVolumes() const {
    QVector<RawVolume> result;

    const QString overrideDir = overridePath();
    if (!overrideDir.isEmpty()) {
        QStorageInfo storage(overrideDir);
        storage.refresh();

        const QFileInfo fi(overrideDir);
        RawVolume vol;
        vol.device = QString();
        vol.label = fi.fileName().isEmpty() ? QStringLiteral("USB") : fi.fileName();
        vol.mountPoint = overrideDir;
        vol.freeBytes = storage.isValid() ? qMax<qint64>(0, storage.bytesAvailable()) : 0;
        vol.totalBytes = storage.isValid() ? qMax<qint64>(0, storage.bytesTotal()) : 0;
        vol.valid = true;
        vol.ready = true;
        vol.readOnly = false;
        vol.removable = true;
        result.append(vol);
        return result;
    }

    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    result.reserve(volumes.size());

    for (const QStorageInfo &storage : volumes) {
        const QString mountPoint = QDir::cleanPath(storage.rootPath());
        if (!isMediaMountPoint(mountPoint)) {
            continue;
        }
        if (!storage.isValid() || !storage.isReady() || storage.isReadOnly()) {
            continue;
        }

        const QString device = QString::fromUtf8(storage.device());
        if (!isDeviceRemovable(device)) {
            continue;
        }

        RawVolume vol;
        vol.device = device;
        vol.label = storage.name().trimmed();
        vol.mountPoint = mountPoint;
        vol.freeBytes = qMax<qint64>(0, storage.bytesAvailable());
        vol.totalBytes = qMax<qint64>(0, storage.bytesTotal());
        vol.valid = storage.isValid();
        vol.ready = storage.isReady();
        vol.readOnly = storage.isReadOnly();
        vol.removable = true;
        result.append(vol);
    }

    return result;
}

qint64 LinuxVolumeProvider::freeBytesForPath(const QString &mountPoint) const {
    if (mountPoint.isEmpty()) {
        return -1;
    }

    QStorageInfo storage(mountPoint);
    storage.refresh();
    if (!storage.isValid() || !storage.isReady()) {
        return -1;
    }
    return qMax<qint64>(0, storage.bytesAvailable());
}

// ---------------------------------------------------------------------------
// UsbMonitor
// ---------------------------------------------------------------------------

UsbMonitor::UsbMonitor(QObject *parent)
    : UsbMonitor(std::make_unique<LinuxVolumeProvider>(), parent) {}

UsbMonitor::UsbMonitor(std::unique_ptr<IVolumeProvider> provider, QObject *parent)
    : QObject(parent)
    , mProvider(provider ? std::move(provider) : std::make_unique<LinuxVolumeProvider>()) {
    mPollTimer.setInterval(kDefaultPollIntervalMs);
    connect(&mPollTimer, &QTimer::timeout, this, &UsbMonitor::rescan);
}

UsbMonitor::~UsbMonitor() {
    mPollTimer.stop();
    if (mEjectProcess) {
        terminateProcessSync(mEjectProcess.get(), this);
        mEjectProcess.reset();
    }
    for (std::unique_ptr<QProcess> &proc : mStoppingProcesses) {
        terminateProcessSync(proc.get(), this);
    }
    mStoppingProcesses.clear();
}

void UsbMonitor::start() {
    rescan();
    if (!mPollTimer.isActive()) {
        mPollTimer.start();
    }
}

void UsbMonitor::stop() {
    mPollTimer.stop();
}

void UsbMonitor::setPollIntervalMs(int intervalMs) {
    if (intervalMs > 0) {
        mPollTimer.setInterval(intervalMs);
    }
}

int UsbMonitor::pollIntervalMs() const {
    return mPollTimer.interval();
}

QVector<UsbDrive> UsbMonitor::drives() const {
    return mDrives;
}

int UsbMonitor::selectedIndex() const {
    return mSelectedIndex;
}

std::optional<UsbDrive> UsbMonitor::currentDrive() const {
    if (mSelectedIndex >= 0 && mSelectedIndex < mDrives.size()) {
        return mDrives.at(mSelectedIndex);
    }
    return std::nullopt;
}

void UsbMonitor::selectDrive(int index) {
    if (index < 0 || index >= mDrives.size()) {
        return;
    }

    mUserPinnedSelection = true;

    if (mSelectedIndex == index) {
        return;
    }

    const qint64 prevFree = (mSelectedIndex >= 0 && mSelectedIndex < mDrives.size())
                                ? mDrives.at(mSelectedIndex).freeBytes
                                : -1;
    mSelectedIndex = index;
    emit drivesChanged();

    const qint64 newFree = mDrives.at(mSelectedIndex).freeBytes;
    if (newFree != prevFree) {
        emit freeSpaceUpdated(newFree);
    }
}

void UsbMonitor::refreshFreeSpace() {
    if (mSelectedIndex < 0 || mSelectedIndex >= mDrives.size()) {
        return;
    }

    const qint64 newFree = mProvider->freeBytesForPath(mDrives.at(mSelectedIndex).mountPoint);
    if (newFree < 0) {
        return;
    }

    if (newFree != mDrives.at(mSelectedIndex).freeBytes) {
        mDrives[mSelectedIndex].freeBytes = newFree;
        emit freeSpaceUpdated(newFree);
    }
}

void UsbMonitor::rescan() {
    const QVector<UsbDrive> prevDrives = mDrives;
    const int prevSelectedIndex = mSelectedIndex;
    const QString prevSelectedMount = (prevSelectedIndex >= 0 && prevSelectedIndex < prevDrives.size())
                                          ? prevDrives.at(prevSelectedIndex).mountPoint
                                          : QString();
    const qint64 prevSelectedFree = (prevSelectedIndex >= 0 && prevSelectedIndex < prevDrives.size())
                                        ? prevDrives.at(prevSelectedIndex).freeBytes
                                        : -1;

    const QVector<RawVolume> rawVolumes = mProvider->mountedVolumes();

    QVector<UsbDrive> newDrives;
    newDrives.reserve(rawVolumes.size());
    QSet<QString> currentMounts;

    for (const RawVolume &raw : rawVolumes) {
        if (!isEligibleVolume(raw)) {
            continue;
        }

        const QString cleanMount = QDir::cleanPath(raw.mountPoint);
        if (currentMounts.contains(cleanMount)) {
            continue;
        }
        currentMounts.insert(cleanMount);

        UsbDrive drive;
        drive.device = raw.device;
        drive.mountPoint = cleanMount;
        drive.label = raw.label.trimmed();
        if (drive.label.isEmpty()) {
            const QString baseName = QFileInfo(cleanMount).fileName();
            drive.label = baseName.isEmpty() ? cleanMount : baseName;
        }
        drive.freeBytes = qMax<qint64>(0, raw.freeBytes);
        drive.totalBytes = qMax<qint64>(0, raw.totalBytes);
        newDrives.append(drive);
    }

    // Prune unmounted entries from mount-order history
    for (auto it = mMountOrder.begin(); it != mMountOrder.end();) {
        if (!currentMounts.contains(it.key())) {
            it = mMountOrder.erase(it);
        } else {
            ++it;
        }
    }

    // Assign monotonically increasing mount sequence to newly seen drives
    for (const UsbDrive &drive : newDrives) {
        if (!mMountOrder.contains(drive.mountPoint)) {
            mMountOrder.insert(drive.mountPoint, ++mMountSeq);
        }
    }

    const bool selectedWasRemoved = !prevSelectedMount.isEmpty() && !currentMounts.contains(prevSelectedMount);
    if (selectedWasRemoved) {
        mUserPinnedSelection = false;
    }

    int newSelectedIndex = -1;
    if (!newDrives.isEmpty()) {
        if (mUserPinnedSelection && !prevSelectedMount.isEmpty()) {
            for (int i = 0; i < newDrives.size(); ++i) {
                if (newDrives.at(i).mountPoint == prevSelectedMount) {
                    newSelectedIndex = i;
                    break;
                }
            }
        }

        if (newSelectedIndex < 0) {
            // Default to the most recently mounted drive
            quint64 maxSeq = 0;
            for (int i = 0; i < newDrives.size(); ++i) {
                const quint64 seq = mMountOrder.value(newDrives.at(i).mountPoint, 0);
                if (newSelectedIndex < 0 || seq > maxSeq) {
                    maxSeq = seq;
                    newSelectedIndex = i;
                }
            }
        }
    } else {
        mUserPinnedSelection = false;
    }

    bool listOrSelectionChanged = (newDrives.size() != prevDrives.size())
                                  || (newSelectedIndex != prevSelectedIndex);
    if (!listOrSelectionChanged) {
        for (int i = 0; i < newDrives.size(); ++i) {
            if (!sameDriveMetadata(newDrives.at(i), prevDrives.at(i))) {
                listOrSelectionChanged = true;
                break;
            }
        }
    }

    mDrives = std::move(newDrives);
    mSelectedIndex = newSelectedIndex;

    const qint64 newSelectedFree = (mSelectedIndex >= 0 && mSelectedIndex < mDrives.size())
                                       ? mDrives.at(mSelectedIndex).freeBytes
                                       : -1;

    if (listOrSelectionChanged) {
        emit drivesChanged();
    }

    if (selectedWasRemoved) {
        emit driveRemoved(prevSelectedMount);
    }

    if (mSelectedIndex >= 0 && newSelectedFree != prevSelectedFree) {
        emit freeSpaceUpdated(newSelectedFree);
    }
}

void UsbMonitor::ejectDrive(const QString &device, const QString &mountPoint) {
    if (mEjectStep != EjectStep::Idle) {
        emit ejectFinished(false, QStringLiteral("Eject already in progress."));
        return;
    }

    mEjectDevice = device.trimmed();
    mEjectMountPoint = mountPoint;

    startEjectStep(EjectStep::Sync, QStringLiteral("sync"), {});
}

bool UsbMonitor::isEjectInProgress() const {
    return mEjectStep != EjectStep::Idle;
}

bool UsbMonitor::isEligibleVolume(const RawVolume &vol) {
    if (!vol.valid || !vol.ready || vol.readOnly || !vol.removable) {
        return false;
    }
    if (vol.mountPoint.isEmpty()) {
        return false;
    }
    return LinuxVolumeProvider::isMediaMountPoint(vol.mountPoint)
           || matchesOverridePath(vol.mountPoint);
}

bool UsbMonitor::matchesOverridePath(const QString &mountPoint) {
    const QString overrideDir = LinuxVolumeProvider::overridePath();
    if (overrideDir.isEmpty()) {
        return false;
    }
    return QDir::cleanPath(mountPoint) == overrideDir;
}

bool UsbMonitor::sameDriveMetadata(const UsbDrive &a, const UsbDrive &b) {
    return a.device == b.device
           && a.label == b.label
           && a.mountPoint == b.mountPoint
           && a.totalBytes == b.totalBytes;
}

void UsbMonitor::retireEjectProcessAsync(std::unique_ptr<QProcess> proc) {
    if (!proc) {
        return;
    }
    QProcess *raw = proc.get();
    raw->disconnect(this);
    mStoppingProcesses.push_back(std::move(proc));
    QMetaObject::invokeMethod(
        this, [this, raw]() { eraseStoppingProcess(raw); }, Qt::QueuedConnection);
}

void UsbMonitor::eraseStoppingProcess(QProcess *proc) {
    const auto it = std::find_if(mStoppingProcesses.begin(), mStoppingProcesses.end(),
                                 [proc](const std::unique_ptr<QProcess> &item) {
                                     return item.get() == proc;
                                 });
    if (it != mStoppingProcesses.end()) {
        mStoppingProcesses.erase(it);
    }
}

void UsbMonitor::startEjectStep(EjectStep step, const QString &program, const QStringList &args) {
    if (mEjectProcess) {
        retireEjectProcessAsync(std::move(mEjectProcess));
    }

    mEjectStep = step;
    mEjectProcess = std::make_unique<QProcess>();
    QProcess *proc = mEjectProcess.get();
    connect(proc,
            &QProcess::finished,
            this,
            [this, proc](int exitCode, QProcess::ExitStatus exitStatus) {
                if (proc != mEjectProcess.get()) {
                    return;
                }
                const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);
                handleEjectProcessFinished(ok);
            });
    connect(proc,
            &QProcess::errorOccurred,
            this,
            [this, proc](QProcess::ProcessError error) {
                if (proc != mEjectProcess.get() || error != QProcess::FailedToStart) {
                    return;
                }
                handleEjectProcessFailedToStart();
            });

    mEjectProcess->start(program, args);
}

void UsbMonitor::handleEjectProcessFinished(bool ok) {
    switch (mEjectStep) {
    case EjectStep::Sync:
        if (!ok) {
            finishEject(false,
                        QStringLiteral("Could not flush files to the USB drive. Check that the drive is connected and try again."),
                        false);
            return;
        }
        if (mEjectDevice.isEmpty()) {
            finishEject(true, QStringLiteral("Drive synced and ready for safe removal."), false);
            return;
        }
        startEjectStep(EjectStep::Unmount,
                       QStringLiteral("udisksctl"),
                       {QStringLiteral("unmount"), QStringLiteral("-b"), mEjectDevice});
        return;

    case EjectStep::Unmount:
        if (!ok) {
            finishEject(false,
                        QStringLiteral("Could not unmount the USB drive. Close any open files on the drive and try again."),
                        false);
            return;
        }
        startEjectStep(EjectStep::PowerOff,
                       QStringLiteral("udisksctl"),
                       {QStringLiteral("power-off"), QStringLiteral("-b"), mEjectDevice});
        return;

    case EjectStep::PowerOff:
        if (!ok) {
            finishEject(false,
                        QStringLiteral("The USB drive was unmounted, but could not be powered off. Wait a moment and unplug it safely."),
                        true);
            return;
        }
        finishEject(true, QStringLiteral("Safe to remove USB drive."), true);
        return;

    case EjectStep::Idle:
        return;
    }
}

void UsbMonitor::handleEjectProcessFailedToStart() {
    if (mEjectStep == EjectStep::Idle) {
        return;
    }

    finishEject(false,
                QStringLiteral("Could not eject the USB drive. Close any open files and unmount it from your desktop before unplugging."),
                false);
}

void UsbMonitor::finishEject(bool ok, const QString &message, bool triggerRescan) {
    mEjectStep = EjectStep::Idle;
    mEjectDevice.clear();
    mEjectMountPoint.clear();

    if (mEjectProcess) {
        retireEjectProcessAsync(std::move(mEjectProcess));
    }

    if (triggerRescan) {
        rescan();
    }

    emit ejectFinished(ok, message);
}

} // namespace dc
