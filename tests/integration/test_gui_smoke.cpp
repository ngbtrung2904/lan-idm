#include "AuthController.h"
#include "SourcesController.h"
#include "TransferController.h"
#include "core/ConfigStore.h"
#include "core/KeyAgent.h"
#include "core/TransferEngine.h"
#include "core/UsbMonitor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dc;

class TestGuiSmoke : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(TestGuiSmoke)

public:
    TestGuiSmoke() = default;

private slots:
    void testBundledFontsLoaded();
    void testEndToEndControllerFlow();

private:
    QString mFleetDir;
    AppConfig mConfig;
};

void TestGuiSmoke::testBundledFontsLoaded() {
    static const char *const kFontPaths[] = {
        ":/fonts/Inter-Regular.ttf",
        ":/fonts/Inter-Medium.ttf",
        ":/fonts/Inter-SemiBold.ttf",
        ":/fonts/JetBrainsMono-Regular.ttf"
    };

    QStringList loadedFamilies;
    for (const char *fontPath : kFontPaths) {
        const int fontId = QFontDatabase::addApplicationFont(QString::fromLatin1(fontPath));
        QVERIFY2(fontId >= 0, qPrintable(QStringLiteral("Failed to load bundled font: %1")
                                             .arg(QString::fromLatin1(fontPath))));
        loadedFamilies.append(QFontDatabase::applicationFontFamilies(fontId));
    }

    QVERIFY2(loadedFamilies.contains(QStringLiteral("Inter")),
             qPrintable(QStringLiteral("Expected 'Inter' in applicationFontFamilies, got: %1")
                            .arg(loadedFamilies.join(QStringLiteral(", ")))));
    QVERIFY2(loadedFamilies.contains(QStringLiteral("JetBrains Mono")),
             qPrintable(QStringLiteral("Expected 'JetBrains Mono' in applicationFontFamilies, got: %1")
                            .arg(loadedFamilies.join(QStringLiteral(", ")))));

    // Qt appends " [<foundry>]" in QFontDatabase::families() when multiple foundries
    // provide the same family (e.g. bundled TTF + system-installed font).
    const QStringList families = QFontDatabase::families();
    auto hasFamily = [&families](const QString &target) {
        const QString disambiguatedPrefix = target + QStringLiteral(" [");
        for (const QString &f : families) {
            if (f == target || f.startsWith(disambiguatedPrefix)) {
                return true;
            }
        }
        return false;
    };

    QVERIFY2(hasFamily(QStringLiteral("Inter")),
             qPrintable(QStringLiteral("Expected 'Inter' in QFontDatabase::families()")));
    QVERIFY2(hasFamily(QStringLiteral("JetBrains Mono")),
             qPrintable(QStringLiteral("Expected 'JetBrains Mono' in QFontDatabase::families()")));
}

void TestGuiSmoke::testEndToEndControllerFlow() {
    mFleetDir = qEnvironmentVariable("DC_FAKE_FLEET_DIR").trimmed();
    if (mFleetDir.isEmpty()) {
        QSKIP("DC_FAKE_FLEET_DIR is not set; skipping end-to-end controller flow.");
    }

    const QString configPath = QDir(mFleetDir).filePath(QStringLiteral("config.json"));
    QVERIFY2(QFileInfo::exists(configPath),
             qPrintable(QStringLiteral("Expected fake-fleet config at %1").arg(configPath)));

    QString err;
    QVERIFY2(ConfigStore::load(configPath, &mConfig, &err), qPrintable(err));
    QCOMPARE(mConfig.pcs.size(), 6);

    QTemporaryDir usbDir;
    QVERIFY(usbDir.isValid());

    const QByteArray prevOverride = qgetenv("DC_USB_OVERRIDE_PATH");
    qputenv("DC_USB_OVERRIDE_PATH", usbDir.path().toUtf8());

    auto restoreOverride = [&]() {
        if (prevOverride.isEmpty()) {
            qunsetenv("DC_USB_OVERRIDE_PATH");
        } else {
            qputenv("DC_USB_OVERRIDE_PATH", prevOverride);
        }
    };

    KeyAgent keyAgent(nullptr);
    UsbMonitor usbMonitor(nullptr);
    TransferEngine engine(nullptr);

    AuthController authController(mConfig, &keyAgent, nullptr);
    SourcesController sourcesController(mConfig, &keyAgent, &usbMonitor, nullptr);
    TransferController transferController(&engine, &usbMonitor, nullptr);

    // Ensure override drive is detected before scanning PCs
    usbMonitor.rescan();
    QVERIFY(sourcesController.driveState() != SourcesController::None);

    // 1. Login with wrong passphrase -> error set, state LoggedOut
    authController.unlock(QStringLiteral("op-gui-smoke"), QStringLiteral("wrong-passphrase"));
    QTRY_COMPARE_WITH_TIMEOUT(authController.state(), static_cast<int>(AuthController::LoggedOut), 15000);
    QVERIFY(!authController.error().isEmpty());
    QVERIFY(!keyAgent.isUnlocked());

    // 2. Login with testpass123 -> state LoggedIn
    authController.unlock(QStringLiteral("op-gui-smoke"), QStringLiteral("testpass123"));
    QTRY_COMPARE_WITH_TIMEOUT(authController.state(), static_cast<int>(AuthController::LoggedIn), 15000);
    QVERIFY(authController.error().isEmpty());
    QCOMPARE(authController.operatorId(), QStringLiteral("op-gui-smoke"));
    QVERIFY(keyAgent.isUnlocked());

    // 3. rescanPcs() -> wait until busy == false; assert pcModel has 6 rows (pc05 Offline, 5 Online)
    sourcesController.rescanPcs();
    QTRY_COMPARE_WITH_TIMEOUT(sourcesController.busy(), false, 45000);

    QAbstractListModel *pcModel = sourcesController.pcModel();
    QVERIFY(pcModel != nullptr);
    QCOMPARE(pcModel->rowCount(), 6);

    QHash<QString, PcState> pcStates;
    for (int row = 0; row < pcModel->rowCount(); ++row) {
        const QModelIndex idx = pcModel->index(row, 0);
        const QString id = pcModel->data(idx, PcListModel::IdRole).toString();
        const auto st = static_cast<PcState>(pcModel->data(idx, PcListModel::StateRole).toInt());
        pcStates.insert(id, st);
    }

    QCOMPARE(pcStates.value(QStringLiteral("pc05"), PcState::Unknown), PcState::Offline);
    for (const QString &onlineId : {QStringLiteral("pc01"), QStringLiteral("pc02"),
                                    QStringLiteral("pc03"), QStringLiteral("pc04"),
                                    QStringLiteral("pc06")}) {
        QCOMPARE(pcStates.value(onlineId, PcState::Unknown), PcState::Online);
    }

    QCOMPARE(sourcesController.onlinePcCount(), 5);
    QCOMPARE(sourcesController.selectedCount(), 5);
    QCOMPARE(sourcesController.canStart(), true);

    // 4. applyPreset("7d") -> instant re-filter without rescan (busy remains false)
    sourcesController.applyPreset(QStringLiteral("7d"));
    QCOMPARE(sourcesController.busy(), false);
    QCOMPARE(sourcesController.activePreset(), QStringLiteral("7d"));
    QVERIFY(sourcesController.selectedBytes() > 0);
    QCOMPARE(sourcesController.canStart(), true);

    // 5. startCopy() -> TransferController::startSession(...) -> wait for finish
    QSignalSpy copyReqSpy(&sourcesController, &SourcesController::copyRequested);
    sourcesController.startCopy();
    QCOMPARE(copyReqSpy.count(), 1);

    transferController.startSession(&sourcesController,
                                    authController.operatorId(),
                                    keyAgent.socketPath());
    QCOMPARE(transferController.phase(), static_cast<int>(TransferController::Running));

    QTRY_VERIFY_WITH_TIMEOUT(
        transferController.phase() == static_cast<int>(TransferController::Finished)
            || transferController.phase() == static_cast<int>(TransferController::FinishedWithWarnings),
        90000);

    QCOMPARE(transferController.phase(), static_cast<int>(TransferController::FinishedWithWarnings));
    QCOMPARE(transferController.overallPercent(), 100);
    QCOMPARE(transferController.canEject(), true);

    // Verify jobModel states
    QAbstractListModel *jobModel = transferController.jobModel();
    QVERIFY(jobModel != nullptr);
    QCOMPARE(jobModel->rowCount(), 6);

    QHash<QString, JobState> jobStates;
    for (int row = 0; row < jobModel->rowCount(); ++row) {
        const QModelIndex idx = jobModel->index(row, 0);
        const QString id = jobModel->data(idx, JobListModel::IdRole).toString();
        const auto st = static_cast<JobState>(jobModel->data(idx, JobListModel::StateRole).toInt());
        jobStates.insert(id, st);
    }

    for (const QString &cleanId : {QStringLiteral("pc01"), QStringLiteral("pc02"),
                                   QStringLiteral("pc03"), QStringLiteral("pc06")}) {
        QCOMPARE(jobStates.value(cleanId, JobState::Queued), JobState::Done);
    }
    QCOMPARE(jobStates.value(QStringLiteral("pc04"), JobState::Queued), JobState::DoneWithWarnings);
    QCOMPARE(jobStates.value(QStringLiteral("pc05"), JobState::Queued), JobState::Skipped);

    // Verify manifest.json exists and parses cleanly
    const QString manifestPath = QDir(transferController.sessionPath()).filePath(QStringLiteral("manifest.json"));
    QVERIFY2(QFileInfo::exists(manifestPath), qPrintable(manifestPath));
    QFile mf(manifestPath);
    QVERIFY(mf.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("operatorId")).toString(), QStringLiteral("op-gui-smoke"));
    QCOMPARE(doc.object().value(QStringLiteral("status")).toString(), QStringLiteral("finished_with_warnings"));
    QCOMPARE(doc.object().value(QStringLiteral("pcs")).toArray().size(), 6);

    // 6. authController.lock() -> state LoggedOut, agent stopped
    const QString socketBeforeLock = keyAgent.socketPath();
    QVERIFY(!socketBeforeLock.isEmpty());

    authController.lock();
    QCOMPARE(authController.state(), static_cast<int>(AuthController::LoggedOut));
    QVERIFY(!keyAgent.isUnlocked());
    QVERIFY(!QFileInfo::exists(socketBeforeLock));

    restoreOverride();
}

QTEST_MAIN(TestGuiSmoke)
#include "test_gui_smoke.moc"
