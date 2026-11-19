#pragma once

#include "Types.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <memory>
#include <optional>
#include <vector>

class QProcess;

namespace dc {

struct RawVolume {
    QString device;
    QString label;
    QString mountPoint;
    qint64 freeBytes = 0;
    qint64 totalBytes = 0;
    bool valid = false;
    bool ready = false;
    bool readOnly = false;
    bool removable = false;
};

class IVolumeProvider {
    Q_DISABLE_COPY(IVolumeProvider)

public:
    virtual ~IVolumeProvider() = default;
    virtual QVector<RawVolume> mountedVolumes() const = 0;
    virtual qint64 freeBytesForPath(const QString &mountPoint) const = 0;

protected:
    IVolumeProvider() = default;
};

class LinuxVolumeProvider final : public IVolumeProvider {
    Q_DISABLE_COPY(LinuxVolumeProvider)

public:
    LinuxVolumeProvider() = default;

    QVector<RawVolume> mountedVolumes() const override;
    qint64 freeBytesForPath(const QString &mountPoint) const override;

    static bool isDeviceRemovable(const QString &device);
    static bool isMediaMountPoint(const QString &mountPoint);
    static QString overridePath();
};

class UsbMonitor : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(UsbMonitor)

public:
    explicit UsbMonitor(QObject *parent);
    UsbMonitor(std::unique_ptr<IVolumeProvider> provider, QObject *parent);
    ~UsbMonitor() override;

    void start();
    void stop();
    void setPollIntervalMs(int intervalMs);
    int pollIntervalMs() const;

    QVector<UsbDrive> drives() const;
    int selectedIndex() const;
    std::optional<UsbDrive> currentDrive() const;

    void selectDrive(int index);
    void refreshFreeSpace();
    void rescan();

    void ejectDrive(const QString &device, const QString &mountPoint);
    bool isEjectInProgress() const;

signals:
    void drivesChanged();
    void driveRemoved(const QString &mountPoint);
    void freeSpaceUpdated(qint64 freeBytes);
    void ejectFinished(bool ok, const QString &message);

private:
    enum class EjectStep {
        Idle = 0,
        Sync,
        Unmount,
        PowerOff
    };

    static bool isEligibleVolume(const RawVolume &vol);
    static bool matchesOverridePath(const QString &mountPoint);
    static bool sameDriveMetadata(const UsbDrive &a, const UsbDrive &b);

    void startEjectStep(EjectStep step, const QString &program, const QStringList &args);
    void handleEjectProcessFinished(bool ok);
    void handleEjectProcessFailedToStart();
    void finishEject(bool ok, const QString &message, bool triggerRescan);
    void retireEjectProcessAsync(std::unique_ptr<QProcess> proc);
    void eraseStoppingProcess(QProcess *proc);

    std::unique_ptr<IVolumeProvider> mProvider;
    QTimer mPollTimer;

    QVector<UsbDrive> mDrives;
    int mSelectedIndex = -1;
    bool mUserPinnedSelection = false;
    quint64 mMountSeq = 0;
    QHash<QString, quint64> mMountOrder;

    EjectStep mEjectStep = EjectStep::Idle;
    std::unique_ptr<QProcess> mEjectProcess;
    std::vector<std::unique_ptr<QProcess>> mStoppingProcesses;
    QString mEjectDevice;
    QString mEjectMountPoint;
};

} // namespace dc
