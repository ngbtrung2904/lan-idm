#include "UsbMonitor.h"

#include <QDir>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

class MockVolumeProvider : public dc::IVolumeProvider {
public:
    QVector<dc::RawVolume> volumes;
    QHash<QString, qint64> freeSpaceOverrides;

    QVector<dc::RawVolume> mountedVolumes() const override
    {
        return volumes;
    }

    qint64 freeBytesForPath(const QString &mountPoint) const override
    {
        if (freeSpaceOverrides.contains(mountPoint)) {
            return freeSpaceOverrides.value(mountPoint);
        }
        for (const dc::RawVolume &v : volumes) {
            if (QDir::cleanPath(v.mountPoint) == QDir::cleanPath(mountPoint)) {
                return v.freeBytes;
            }
        }
        return -1;
    }
};

dc::RawVolume makeValidUsb(const QString &device,
                           const QString &label,
                           const QString &mountPoint,
                           qint64 freeBytes = 10LL * 1024 * 1024 * 1024,
                           qint64 totalBytes = 64LL * 1024 * 1024 * 1024)
{
    dc::RawVolume vol;
    vol.device = device;
    vol.label = label;
    vol.mountPoint = mountPoint;
    vol.freeBytes = freeBytes;
    vol.totalBytes = totalBytes;
    vol.valid = true;
    vol.ready = true;
    vol.readOnly = false;
    vol.removable = true;
    return vol;
}

} // namespace

class TestUsbMonitor : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        qunsetenv("DC_USB_OVERRIDE_PATH");
    }

    void cleanup()
    {
        qunsetenv("DC_USB_OVERRIDE_PATH");
    }

    void testNoneToOneDrive()
    {
        auto mock = std::make_unique<MockVolumeProvider>();
        MockVolumeProvider *rawMock = mock.get();

        dc::UsbMonitor monitor(std::move(mock), nullptr);
        QSignalSpy drivesSpy(&monitor, &dc::UsbMonitor::drivesChanged);
        QSignalSpy freeSpy(&monitor, &dc::UsbMonitor::freeSpaceUpdated);

        monitor.rescan();
        QVERIFY(monitor.drives().isEmpty());
        QCOMPARE(monitor.selectedIndex(), -1);
        QVERIFY(!monitor.currentDrive().has_value());
        QCOMPARE(drivesSpy.count(), 0);

        rawMock->volumes = {
            makeValidUsb(QStringLiteral("/dev/sdb1"),
                         QString(), // verify fallback to mountPoint basename
                         QStringLiteral("/media/user/KINGSTON_64G"),
                         20'000,
                         64'000)
        };
        monitor.rescan();

        QCOMPARE(drivesSpy.count(), 1);
        QCOMPARE(freeSpy.count(), 1);
        QCOMPARE(freeSpy.at(0).at(0).toLongLong(), 20'000LL);
        QCOMPARE(monitor.drives().size(), 1);
        QCOMPARE(monitor.selectedIndex(), 0);
        QVERIFY(monitor.currentDrive().has_value());
        QCOMPARE(monitor.currentDrive()->device, QStringLiteral("/dev/sdb1"));
        QCOMPARE(monitor.currentDrive()->label, QStringLiteral("KINGSTON_64G"));
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/KINGSTON_64G"));
        QCOMPARE(monitor.currentDrive()->freeBytes, 20'000LL);
        QCOMPARE(monitor.currentDrive()->totalBytes, 64'000LL);

        // Rescanning with identical state should not emit spurious signals
        monitor.rescan();
        QCOMPARE(drivesSpy.count(), 1);
        QCOMPARE(freeSpy.count(), 1);
    }

    void testFilteringInvalidAndNonMediaVolumes()
    {
        auto mock = std::make_unique<MockVolumeProvider>();
        MockVolumeProvider *rawMock = mock.get();

        dc::RawVolume nonRemovable = makeValidUsb(
            QStringLiteral("/dev/sda1"), QStringLiteral("Internal"), QStringLiteral("/media/user/INTERNAL"));
        nonRemovable.removable = false;

        dc::RawVolume readOnly = makeValidUsb(
            QStringLiteral("/dev/sr0"), QStringLiteral("CDROM"), QStringLiteral("/media/user/CDROM"));
        readOnly.readOnly = true;

        dc::RawVolume invalid = makeValidUsb(
            QStringLiteral("/dev/sdc1"), QStringLiteral("Invalid"), QStringLiteral("/media/user/INVALID"));
        invalid.valid = false;

        dc::RawVolume notReady = makeValidUsb(
            QStringLiteral("/dev/sdd1"), QStringLiteral("NotReady"), QStringLiteral("/media/user/NOTREADY"));
        notReady.ready = false;

        dc::RawVolume rootMount = makeValidUsb(
            QStringLiteral("/dev/sde1"), QStringLiteral("Root"), QStringLiteral("/"));

        dc::RawVolume mntMount = makeValidUsb(
            QStringLiteral("/dev/sdf1"), QStringLiteral("Mnt"), QStringLiteral("/mnt/usb"));

        dc::RawVolume fakeMediaPrefix = makeValidUsb(
            QStringLiteral("/dev/sdg1"), QStringLiteral("Fake"), QStringLiteral("/media2/usb"));

        dc::RawVolume validMedia = makeValidUsb(
            QStringLiteral("/dev/sdb1"), QStringLiteral("Good1"), QStringLiteral("/media/user/GOOD1"));

        dc::RawVolume validRunMedia = makeValidUsb(
            QStringLiteral("/dev/sdh1"), QStringLiteral("Good2"), QStringLiteral("/run/media/user/GOOD2"));

        rawMock->volumes = {
            nonRemovable,
            readOnly,
            invalid,
            notReady,
            rootMount,
            mntMount,
            fakeMediaPrefix,
            validMedia,
            validRunMedia
        };

        dc::UsbMonitor monitor(std::move(mock), nullptr);
        monitor.rescan();

        QCOMPARE(monitor.drives().size(), 2);
        QCOMPARE(monitor.drives().at(0).mountPoint, QStringLiteral("/media/user/GOOD1"));
        QCOMPARE(monitor.drives().at(1).mountPoint, QStringLiteral("/run/media/user/GOOD2"));
    }

    void testMultipleDrivesMostRecentlyMountedDefault()
    {
        auto mock = std::make_unique<MockVolumeProvider>();
        MockVolumeProvider *rawMock = mock.get();

        dc::UsbMonitor monitor(std::move(mock), nullptr);

        // Drive A inserted first
        const auto driveA = makeValidUsb(
            QStringLiteral("/dev/sdb1"), QStringLiteral("Drive A"), QStringLiteral("/media/user/A"));
        rawMock->volumes = {driveA};
        monitor.rescan();

        QCOMPARE(monitor.drives().size(), 1);
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/A"));

        // Drive B inserted second -> should become default selected drive
        const auto driveB = makeValidUsb(
            QStringLiteral("/dev/sdc1"), QStringLiteral("Drive B"), QStringLiteral("/media/user/B"));
        rawMock->volumes = {driveA, driveB};
        monitor.rescan();

        QCOMPARE(monitor.drives().size(), 2);
        QCOMPARE(monitor.selectedIndex(), 1);
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/B"));
    }

    void testExplicitSelectionPreservedWhenNewDriveAppears()
    {
        auto mock = std::make_unique<MockVolumeProvider>();
        MockVolumeProvider *rawMock = mock.get();

        dc::UsbMonitor monitor(std::move(mock), nullptr);

        const auto driveA = makeValidUsb(
            QStringLiteral("/dev/sdb1"), QStringLiteral("Drive A"), QStringLiteral("/media/user/A"));
        const auto driveB = makeValidUsb(
            QStringLiteral("/dev/sdc1"), QStringLiteral("Drive B"), QStringLiteral("/media/user/B"));
        const auto driveC = makeValidUsb(
            QStringLiteral("/dev/sdd1"), QStringLiteral("Drive C"), QStringLiteral("/media/user/C"));

        rawMock->volumes = {driveA};
        monitor.rescan();
        rawMock->volumes = {driveA, driveB};
        monitor.rescan();
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/B"));

        // User explicitly selects Drive A (index 0)
        QSignalSpy drivesSpy(&monitor, &dc::UsbMonitor::drivesChanged);
        monitor.selectDrive(0);
        QCOMPARE(drivesSpy.count(), 1);
        QCOMPARE(monitor.selectedIndex(), 0);
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/A"));

        // Drive C appears -> user's explicit selection (Drive A) must be preserved
        rawMock->volumes = {driveA, driveB, driveC};
        monitor.rescan();
        QCOMPARE(monitor.drives().size(), 3);
        QCOMPARE(monitor.selectedIndex(), 0);
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/A"));

        // Out-of-bounds selectDrive is a safe no-op
        monitor.selectDrive(-1);
        monitor.selectDrive(99);
        QCOMPARE(monitor.selectedIndex(), 0);
    }

    void testSelectedDriveRemovedAndFallback()
    {
        auto mock = std::make_unique<MockVolumeProvider>();
        MockVolumeProvider *rawMock = mock.get();

        dc::UsbMonitor monitor(std::move(mock), nullptr);
        QSignalSpy removedSpy(&monitor, &dc::UsbMonitor::driveRemoved);
        QSignalSpy drivesSpy(&monitor, &dc::UsbMonitor::drivesChanged);

        const auto driveA = makeValidUsb(
            QStringLiteral("/dev/sdb1"), QStringLiteral("Drive A"), QStringLiteral("/media/user/A"));
        const auto driveB = makeValidUsb(
            QStringLiteral("/dev/sdc1"), QStringLiteral("Drive B"), QStringLiteral("/media/user/B"));

        rawMock->volumes = {driveA};
        monitor.rescan();
        rawMock->volumes = {driveA, driveB};
        monitor.rescan();
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/B"));

        // Remove non-selected Drive A -> driveRemoved must NOT fire
        drivesSpy.clear();
        removedSpy.clear();
        rawMock->volumes = {driveB};
        monitor.rescan();
        QCOMPARE(drivesSpy.count(), 1);
        QCOMPARE(removedSpy.count(), 0);
        QCOMPARE(monitor.selectedIndex(), 0);
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/B"));

        // Re-add Drive A (now most recent), keep Drive B pinned
        monitor.selectDrive(0); // pin Drive B
        rawMock->volumes = {driveB, driveA};
        monitor.rescan();
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/B"));

        // Now remove the selected Drive B -> driveRemoved MUST fire and fall back to Drive A
        removedSpy.clear();
        drivesSpy.clear();
        rawMock->volumes = {driveA};
        monitor.rescan();
        QCOMPARE(drivesSpy.count(), 1);
        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.at(0).at(0).toString(), QStringLiteral("/media/user/B"));
        QCOMPARE(monitor.drives().size(), 1);
        QCOMPARE(monitor.selectedIndex(), 0);
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/A"));

        // Remove last remaining Drive A -> driveRemoved fires, no current drive
        removedSpy.clear();
        drivesSpy.clear();
        rawMock->volumes.clear();
        monitor.rescan();
        QCOMPARE(drivesSpy.count(), 1);
        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.at(0).at(0).toString(), QStringLiteral("/media/user/A"));
        QVERIFY(monitor.drives().isEmpty());
        QCOMPARE(monitor.selectedIndex(), -1);
        QVERIFY(!monitor.currentDrive().has_value());
    }

    void testDriveReinserted()
    {
        auto mock = std::make_unique<MockVolumeProvider>();
        MockVolumeProvider *rawMock = mock.get();

        dc::UsbMonitor monitor(std::move(mock), nullptr);
        const auto driveA = makeValidUsb(
            QStringLiteral("/dev/sdb1"), QStringLiteral("Drive A"), QStringLiteral("/media/user/A"));
        const auto driveB = makeValidUsb(
            QStringLiteral("/dev/sdc1"), QStringLiteral("Drive B"), QStringLiteral("/media/user/B"));

        rawMock->volumes = {driveA};
        monitor.rescan();
        rawMock->volumes = {driveA, driveB};
        monitor.rescan();
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/B"));

        // Remove Drive A
        rawMock->volumes = {driveB};
        monitor.rescan();
        QCOMPARE(monitor.drives().size(), 1);

        // Re-insert Drive A -> now Drive A is the most recently mounted drive
        QSignalSpy drivesSpy(&monitor, &dc::UsbMonitor::drivesChanged);
        rawMock->volumes = {driveB, driveA};
        monitor.rescan();
        QCOMPARE(drivesSpy.count(), 1);
        QCOMPARE(monitor.drives().size(), 2);
        QCOMPARE(monitor.currentDrive()->mountPoint, QStringLiteral("/media/user/A"));
    }

    void testFreeSpaceUpdateOnPollAndRefresh()
    {
        auto mock = std::make_unique<MockVolumeProvider>();
        MockVolumeProvider *rawMock = mock.get();

        dc::UsbMonitor monitor(std::move(mock), nullptr);
        QSignalSpy drivesSpy(&monitor, &dc::UsbMonitor::drivesChanged);
        QSignalSpy freeSpy(&monitor, &dc::UsbMonitor::freeSpaceUpdated);

        rawMock->volumes = {
            makeValidUsb(QStringLiteral("/dev/sdb1"),
                         QStringLiteral("Drive A"),
                         QStringLiteral("/media/user/A"),
                         50'000,
                         100'000)
        };
        monitor.rescan();
        QCOMPARE(drivesSpy.count(), 1);
        QCOMPARE(freeSpy.count(), 1);
        QCOMPARE(freeSpy.at(0).at(0).toLongLong(), 50'000LL);

        // Poll detects free space change without changing drive list
        drivesSpy.clear();
        freeSpy.clear();
        rawMock->volumes[0].freeBytes = 42'000;
        monitor.rescan();
        QCOMPARE(drivesSpy.count(), 0);
        QCOMPARE(freeSpy.count(), 1);
        QCOMPARE(freeSpy.at(0).at(0).toLongLong(), 42'000LL);
        QCOMPARE(monitor.currentDrive()->freeBytes, 42'000LL);

        // refreshFreeSpace detects updated free space
        freeSpy.clear();
        rawMock->freeSpaceOverrides.insert(QStringLiteral("/media/user/A"), 35'000);
        monitor.refreshFreeSpace();
        QCOMPARE(freeSpy.count(), 1);
        QCOMPARE(freeSpy.at(0).at(0).toLongLong(), 35'000LL);
        QCOMPARE(monitor.currentDrive()->freeBytes, 35'000LL);

        // Calling refreshFreeSpace again with same value does not emit
        monitor.refreshFreeSpace();
        QCOMPARE(freeSpy.count(), 1);
    }

    void testOverridePathAndEmptyDeviceEject()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        qputenv("DC_USB_OVERRIDE_PATH", tempDir.path().toUtf8());

        dc::UsbMonitor monitor(nullptr); // uses real LinuxVolumeProvider
        monitor.rescan();

        QCOMPARE(monitor.drives().size(), 1);
        QVERIFY(monitor.currentDrive().has_value());
        QCOMPARE(monitor.currentDrive()->device, QString());
        QCOMPARE(monitor.currentDrive()->mountPoint, QDir::cleanPath(tempDir.path()));
        QVERIFY(monitor.currentDrive()->totalBytes > 0);

        // Eject with empty device should run `sync` and emit ejectFinished(true, ...)
        QSignalSpy ejectSpy(&monitor, &dc::UsbMonitor::ejectFinished);
        monitor.ejectDrive(monitor.currentDrive()->device, monitor.currentDrive()->mountPoint);
        QVERIFY(ejectSpy.wait(10000));
        QCOMPARE(ejectSpy.count(), 1);
        QCOMPARE(ejectSpy.at(0).at(0).toBool(), true);
        QVERIFY(!monitor.isEjectInProgress());
    }
};

QTEST_MAIN(TestUsbMonitor)
#include "test_usb_monitor.moc"
