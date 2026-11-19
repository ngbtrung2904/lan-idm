#include "ConfigStore.h"
#include "Manifest.h"
#include "TransferEngine.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace dc;

class TestTransferEngine : public QObject {
    Q_OBJECT

private slots:
    void manifestWriteAtomicAndStructure();
    void manifestOverwriteAtomicity();
    void parseProgress2Lines();
    void parseName1FileAndDirLines();
    void configStoreRejectsUnsafePcIds();
};

void TestTransferEngine::manifestWriteAtomicAndStructure() {
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString manifestPath = QDir(tmpDir.path()).filePath(QStringLiteral("session/manifest.json"));

    ManifestData data;
    data.operatorId = QStringLiteral("op-42");
    data.appVersion = QStringLiteral("0.1.0");
    data.windowStart = QDateTime(QDate(2026, 11, 10), QTime(8, 0, 0), Qt::UTC);
    data.windowEnd = QDateTime(QDate(2026, 11, 17), QTime(17, 0, 0), Qt::UTC);
    data.startedAt = QDateTime(QDate(2026, 11, 19), QTime(14, 0, 0), Qt::UTC);
    data.finishedAt = QDateTime(); // still running -> null
    data.status = QStringLiteral("running");

    PcTransferResult pc1;
    pc1.pcId = QStringLiteral("pc01");
    pc1.host = QStringLiteral("192.168.10.11");
    pc1.state = JobState::DoneWithWarnings;
    pc1.logsFilesPlanned = 10;
    pc1.logsFilesCopied = 9;
    pc1.logsBytesCopied = 102400;
    pc1.shotsFilesPlanned = 5;
    pc1.shotsFilesCopied = 5;
    pc1.shotsBytesCopied = 2048000;
    pc1.recordingsFilesPlanned = 2;
    pc1.recordingsFilesCopied = 2;
    pc1.recordingsBytesCopied = 8192000;
    pc1.logsRsyncExitCode = 24;
    pc1.shotsRsyncExitCode = 0;
    pc1.recordingsRsyncExitCode = 0;
    pc1.errors = {QStringLiteral("pc01: partial transfer of logs (rsync exit 24)")};
    data.pcs.append(pc1);

    QString err;
    QVERIFY2(Manifest::writeAtomic(manifestPath, data, &err), qPrintable(err));

    QFile f(manifestPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    QVERIFY(doc.isObject());

    const QJsonObject root = doc.object();
    QCOMPARE(root.value(QStringLiteral("operatorId")).toString(), QStringLiteral("op-42"));
    QCOMPARE(root.value(QStringLiteral("appVersion")).toString(), QStringLiteral("0.1.0"));
    QCOMPARE(root.value(QStringLiteral("windowStart")).toString(), QStringLiteral("2026-11-10T08:00:00Z"));
    QCOMPARE(root.value(QStringLiteral("windowEnd")).toString(), QStringLiteral("2026-11-17T17:00:00Z"));
    QCOMPARE(root.value(QStringLiteral("startedAt")).toString(), QStringLiteral("2026-11-19T14:00:00Z"));
    QVERIFY(root.value(QStringLiteral("finishedAt")).isNull());
    QCOMPARE(root.value(QStringLiteral("status")).toString(), QStringLiteral("running"));

    const QJsonArray pcs = root.value(QStringLiteral("pcs")).toArray();
    QCOMPARE(pcs.size(), 1);
    const QJsonObject pcObj = pcs.at(0).toObject();
    QCOMPARE(pcObj.value(QStringLiteral("id")).toString(), QStringLiteral("pc01"));
    QCOMPARE(pcObj.value(QStringLiteral("host")).toString(), QStringLiteral("192.168.10.11"));
    QCOMPARE(pcObj.value(QStringLiteral("status")).toString(), QStringLiteral("done_with_warnings"));
    QCOMPARE(pcObj.value(QStringLiteral("logsFilesPlanned")).toInt(), 10);
    QCOMPARE(pcObj.value(QStringLiteral("logsFilesCopied")).toInt(), 9);
    QCOMPARE(static_cast<qint64>(pcObj.value(QStringLiteral("logsBytesCopied")).toDouble()), qint64(102400));
    QCOMPARE(pcObj.value(QStringLiteral("shotsFilesPlanned")).toInt(), 5);
    QCOMPARE(pcObj.value(QStringLiteral("shotsFilesCopied")).toInt(), 5);
    QCOMPARE(static_cast<qint64>(pcObj.value(QStringLiteral("shotsBytesCopied")).toDouble()), qint64(2048000));
    QCOMPARE(pcObj.value(QStringLiteral("recordingsFilesPlanned")).toInt(), 2);
    QCOMPARE(pcObj.value(QStringLiteral("recordingsFilesCopied")).toInt(), 2);
    QCOMPARE(static_cast<qint64>(pcObj.value(QStringLiteral("recordingsBytesCopied")).toDouble()), qint64(8192000));
    QCOMPARE(pcObj.value(QStringLiteral("logsRsyncExitCode")).toInt(), 24);
    QCOMPARE(pcObj.value(QStringLiteral("shotsRsyncExitCode")).toInt(), 0);
    QCOMPARE(pcObj.value(QStringLiteral("recordingsRsyncExitCode")).toInt(), 0);
    QCOMPARE(pcObj.value(QStringLiteral("errors")).toArray().size(), 1);
}

void TestTransferEngine::manifestOverwriteAtomicity() {
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const QString manifestPath = QDir(tmpDir.path()).filePath(QStringLiteral("manifest.json"));

    ManifestData data;
    data.operatorId = QStringLiteral("op-1");
    data.appVersion = QStringLiteral("0.1.0");
    data.startedAt = QDateTime(QDate(2026, 11, 19), QTime(14, 0, 0), Qt::UTC);
    data.status = QStringLiteral("running");
    QVERIFY(Manifest::writeAtomic(manifestPath, data, nullptr));

    data.finishedAt = QDateTime(QDate(2026, 11, 19), QTime(14, 5, 0), Qt::UTC);
    data.status = QStringLiteral("finished");
    QVERIFY(Manifest::writeAtomic(manifestPath, data, nullptr));

    // Only manifest.json should remain in the directory (no leftover temp files)
    const QStringList entries = QDir(tmpDir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QCOMPARE(entries, QStringList{QStringLiteral("manifest.json")});

    QFile f(manifestPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("status")).toString(), QStringLiteral("finished"));
    QCOMPARE(root.value(QStringLiteral("finishedAt")).toString(), QStringLiteral("2026-11-19T14:05:00Z"));
}

void TestTransferEngine::parseProgress2Lines() {
    // 0% line
    {
        const ProgressLine p = TransferEngine::parseOutputLine(
            QStringLiteral("              0   0%    0.00kB/s    0:00:00  "));
        QVERIFY(p.isProgress);
        QCOMPARE(p.bytesDone, qint64(0));
        QCOMPARE(p.percent, 0);
        QCOMPARE(p.rateBytesPerSec, 0.0);
        QVERIFY(p.currentFile.isEmpty());
    }

    // Commas + MB/s + 100% + xfr suffix
    {
        const ProgressLine p = TransferEngine::parseOutputLine(
            QStringLiteral("      4,194,304 100%  173.91MB/s    0:00:00 (xfr#1, to-chk=0/2)"));
        QVERIFY(p.isProgress);
        QCOMPARE(p.bytesDone, qint64(4194304));
        QCOMPARE(p.percent, 100);
        const double expected = 173.91 * 1024.0 * 1024.0;
        QVERIFY(std::abs(p.rateBytesPerSec - expected) < 1.0);
    }

    // kB/s
    {
        const ProgressLine p = TransferEngine::parseOutputLine(
            QStringLiteral("        512,000  42%  250.50kB/s    0:00:02"));
        QVERIFY(p.isProgress);
        QCOMPARE(p.bytesDone, qint64(512000));
        QCOMPARE(p.percent, 42);
        const double expected = 250.50 * 1024.0;
        QVERIFY(std::abs(p.rateBytesPerSec - expected) < 0.1);
    }

    // GB/s
    {
        const ProgressLine p = TransferEngine::parseOutputLine(
            QStringLiteral("  2,147,483,648  75%    1.25GB/s    0:00:01"));
        QVERIFY(p.isProgress);
        QCOMPARE(p.bytesDone, qint64(2147483648LL));
        QCOMPARE(p.percent, 75);
        const double expected = 1.25 * 1024.0 * 1024.0 * 1024.0;
        QVERIFY(std::abs(p.rateBytesPerSec - expected) < 1.0);
    }

    // B/s
    {
        const ProgressLine p = TransferEngine::parseOutputLine(
            QStringLiteral("            128  10%   64.00B/s    0:00:05"));
        QVERIFY(p.isProgress);
        QCOMPARE(p.bytesDone, qint64(128));
        QCOMPARE(p.percent, 10);
        QCOMPARE(p.rateBytesPerSec, 64.0);
    }
}

void TestTransferEngine::parseName1FileAndDirLines() {
    // Directory lines must be ignored
    {
        const ProgressLine p = TransferEngine::parseOutputLine(QStringLiteral("2026-11/"));
        QVERIFY(!p.isProgress);
        QVERIFY(p.currentFile.isEmpty());
    }
    {
        const ProgressLine p = TransferEngine::parseOutputLine(QStringLiteral("./"));
        QVERIFY(!p.isProgress);
        QVERIFY(p.currentFile.isEmpty());
    }
    // Blank line
    {
        const ProgressLine p = TransferEngine::parseOutputLine(QStringLiteral("   \r\n"));
        QVERIFY(!p.isProgress);
        QVERIFY(p.currentFile.isEmpty());
    }
    // Regular file with spaces
    {
        const ProgressLine p = TransferEngine::parseOutputLine(
            QStringLiteral("2026-11/big shot 1.png\r\n"));
        QVERIFY(!p.isProgress);
        QCOMPARE(p.currentFile, QStringLiteral("2026-11/big shot 1.png"));
    }
}

void TestTransferEngine::configStoreRejectsUnsafePcIds() {
    auto makeConfigJson = [](const QString &pcId) -> QByteArray {
        QJsonObject root;
        root.insert(QStringLiteral("sshKeyPath"), QStringLiteral("/tmp/key"));
        root.insert(QStringLiteral("knownHostsPath"), QStringLiteral("/tmp/known_hosts"));
        QJsonObject pc;
        pc.insert(QStringLiteral("id"), pcId);
        pc.insert(QStringLiteral("host"), QStringLiteral("192.168.10.11"));
        root.insert(QStringLiteral("pcs"), QJsonArray{pc});
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    };

    const QStringList unsafeIds = {
        QStringLiteral("../evil"),
        QStringLiteral("a/b"),
        QStringLiteral("a\\b"),
        QStringLiteral(".."),
        QStringLiteral("."),
        QStringLiteral("pc01") + QChar(0x07) + QStringLiteral("bad")
    };

    for (const QString &badId : unsafeIds) {
        AppConfig cfg;
        QString err;
        QVERIFY2(!ConfigStore::parseJson(makeConfigJson(badId), &cfg, &err),
                 qPrintable(QStringLiteral("Expected rejection for id: %1").arg(badId)));
        QVERIFY(!err.isEmpty());
    }

    // Valid id should still pass
    AppConfig validCfg;
    QString validErr;
    QVERIFY2(ConfigStore::parseJson(makeConfigJson(QStringLiteral("pc01-lab_A")), &validCfg, &validErr),
             qPrintable(validErr));
    QCOMPARE(validCfg.pcs.size(), 1);
    QCOMPARE(validCfg.pcs.at(0).id, QStringLiteral("pc01-lab_A"));
}

QTEST_MAIN(TestTransferEngine)
#include "test_transfer_engine.moc"
