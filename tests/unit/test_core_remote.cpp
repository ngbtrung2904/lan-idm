#include "ConfigStore.h"
#include "HostProbe.h"
#include "PathSanitizer.h"
#include "RsyncLister.h"
#include "SpaceEstimator.h"
#include "SshOptions.h"
#include "TimeFilter.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest/QtTest>

using namespace dc;

class TestCoreRemote : public QObject {
    Q_OBJECT

private slots:
    // ConfigStore
    void configStoreValidAndTildeExpansion();
    void configStoreMissingKeys();
    void configStoreInvalidMaxParallelAndPcs();
    void configStoreResolvePathPrecedence();

    // SshOptions
    void sshOptionsRequiredFlagsAndNeverStrictNo();
    void sshOptionsQuotingForSpaces();

    // PathSanitizer
    void pathSanitizerValidation();
    void pathSanitizerWriteFilesFromList();

    // RsyncLister
    void rsyncListerParseListOutput();
    void rsyncListerParseEmptyAndUnsafe();

    // TimeFilter
    void timeFilterExactEdgesAndOutside();
    void timeFilterLogsRuleOnVsOff();
    void timeFilterDstTransitionDay();
    void timeFilterInvalidWindow();

    // SpaceEstimator
    void spaceEstimatorExactAndMarginBoundary();
    void spaceEstimatorOverflowAndFittingCount();

    // HostProbe
    void hostProbeClassifyResult();
};

void TestCoreRemote::configStoreValidAndTildeExpansion() {
    const QByteArray json = R"({
        "sshUser": "collector",
        "sshKeyPath": "~/.ssh/collector_key",
        "knownHostsPath": "~/.local/share/DataCollector/known_hosts",
        "maxParallel": 3,
        "paths": {
            "logs": ".app",
            "screenshots": "screenshot"
        },
        "logs": {
            "includeModifiedAfterEnd": false
        },
        "pcs": [
            { "id": "pc01", "host": "192.168.10.11" },
            { "id": "pc02", "host": "192.168.10.12", "port": 2222 }
        ]
    })";

    AppConfig cfg;
    QString err;
    QVERIFY2(ConfigStore::parseJson(json, &cfg, &err), qPrintable(err));
    QCOMPARE(cfg.sshUser, QStringLiteral("collector"));
    QCOMPARE(cfg.sshKeyPath, QDir::homePath() + QStringLiteral("/.ssh/collector_key"));
    QCOMPARE(cfg.knownHostsPath, QDir::homePath() + QStringLiteral("/.local/share/DataCollector/known_hosts"));
    QCOMPARE(cfg.maxParallel, 3);
    QCOMPARE(cfg.paths.logs, QStringLiteral(".app"));
    QCOMPARE(cfg.paths.screenshots, QStringLiteral("screenshot"));
    QCOMPARE(cfg.logs.includeModifiedAfterEnd, false);
    QCOMPARE(cfg.pcs.size(), 2);
    QCOMPARE(cfg.pcs[0].id, QStringLiteral("pc01"));
    QCOMPARE(cfg.pcs[0].host, QStringLiteral("192.168.10.11"));
    QCOMPARE(cfg.pcs[0].port, 22);
    QCOMPARE(cfg.pcs[1].id, QStringLiteral("pc02"));
    QCOMPARE(cfg.pcs[1].port, 2222);

    QCOMPARE(ConfigStore::expandTilde(QStringLiteral("~")), QDir::homePath());
    QCOMPARE(ConfigStore::expandTilde(QStringLiteral("/abs/path")), QStringLiteral("/abs/path"));
}

void TestCoreRemote::configStoreMissingKeys() {
    AppConfig cfg;
    QString err;

    // Missing sshKeyPath
    QVERIFY(!ConfigStore::parseJson(R"({
        "knownHostsPath": "/tmp/kh",
        "pcs": [{"id": "pc01", "host": "10.0.0.1"}]
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("sshKeyPath")), qPrintable(err));

    // Missing knownHostsPath
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "pcs": [{"id": "pc01", "host": "10.0.0.1"}]
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("knownHostsPath")), qPrintable(err));

    // Missing pcs
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh"
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("pcs")), qPrintable(err));
}

void TestCoreRemote::configStoreInvalidMaxParallelAndPcs() {
    AppConfig cfg;
    QString err;

    // maxParallel = 0
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "maxParallel": 0,
        "pcs": [{"id": "pc01", "host": "10.0.0.1"}]
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("maxParallel")), qPrintable(err));

    // Empty pcs array
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "pcs": []
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("pcs")), qPrintable(err));

    // Invalid port
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "pcs": [{"id": "pc01", "host": "10.0.0.1", "port": 70000}]
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("port")), qPrintable(err));

    // Missing pc host
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "pcs": [{"id": "pc01", "host": ""}]
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("host")), qPrintable(err));
}

void TestCoreRemote::configStoreResolvePathPrecedence() {
    const QByteArray oldEnv = qgetenv("DATA_COLLECTOR_CONFIG");

    qputenv("DATA_COLLECTOR_CONFIG", "/env/config.json");
    QCOMPARE(ConfigStore::resolveConfigPath(QStringLiteral("/explicit/config.json")),
             QStringLiteral("/explicit/config.json"));
    QCOMPARE(ConfigStore::resolveConfigPath(QString()), QStringLiteral("/env/config.json"));

    qunsetenv("DATA_COLLECTOR_CONFIG");
    QCOMPARE(ConfigStore::resolveConfigPath(QString()),
             QDir::homePath() + QStringLiteral("/.config/DataCollector/config.json"));

    if (!oldEnv.isEmpty()) {
        qputenv("DATA_COLLECTOR_CONFIG", oldEnv);
    }
}

void TestCoreRemote::sshOptionsRequiredFlagsAndNeverStrictNo() {
    SshParams p;
    p.port = 2202;
    p.sshKeyPath = QStringLiteral("/home/op/.ssh/collector_key");
    p.knownHostsPath = QStringLiteral("/home/op/.local/share/DataCollector/known_hosts");
    p.agentSocket = QStringLiteral("/run/user/1000/datacollector-agent-1234.sock");

    const QStringList args = SshOptions::buildSshArgs(p);
    QVERIFY(args.contains(QStringLiteral("-p")));
    QVERIFY(args.contains(QStringLiteral("2202")));
    QVERIFY(args.contains(QStringLiteral("-i")));
    QVERIFY(args.contains(p.sshKeyPath));
    QVERIFY(args.contains(QStringLiteral("BatchMode=yes")));
    QVERIFY(args.contains(QStringLiteral("ConnectTimeout=5")));
    QVERIFY(args.contains(QStringLiteral("ServerAliveInterval=10")));
    QVERIFY(args.contains(QStringLiteral("ServerAliveCountMax=3")));
    QVERIFY(args.contains(QStringLiteral("StrictHostKeyChecking=yes")));
    QVERIFY(args.contains(QStringLiteral("UserKnownHostsFile=") + p.knownHostsPath));
    QVERIFY(args.contains(QStringLiteral("IdentitiesOnly=yes")));
    QVERIFY(args.contains(QStringLiteral("IdentityAgent=") + p.agentSocket));

    const QString cmd = SshOptions::buildRsyncSshCommand(p);
    QVERIFY(cmd.startsWith(QStringLiteral("ssh ")));
    QVERIFY(!cmd.contains(QStringLiteral("StrictHostKeyChecking=no"), Qt::CaseInsensitive));
    QVERIFY(!cmd.contains(QStringLiteral("StrictHostKeyChecking=accept-new"), Qt::CaseInsensitive));
}

void TestCoreRemote::sshOptionsQuotingForSpaces() {
    SshParams p;
    p.sshKeyPath = QStringLiteral("/home/my user/keys/collector key");
    p.knownHostsPath = QStringLiteral("/home/my user/known hosts");
    p.agentSocket = QStringLiteral("/tmp/my sock/agent.sock");

    const QString cmd = SshOptions::buildRsyncSshCommand(p);
    QVERIFY(cmd.contains(QStringLiteral("'/home/my user/keys/collector key'")));
    QVERIFY(cmd.contains(QStringLiteral("'UserKnownHostsFile=/home/my user/known hosts'")));
    QVERIFY(cmd.contains(QStringLiteral("'IdentityAgent=/tmp/my sock/agent.sock'")));
}

void TestCoreRemote::pathSanitizerValidation() {
    QVERIFY(PathSanitizer::isSafeRelativePath(QStringLiteral("app.log")));
    QVERIFY(PathSanitizer::isSafeRelativePath(QStringLiteral("sub dir/my log file.log")));
    QVERIFY(PathSanitizer::isSafeRelativePath(QStringLiteral("sub dir/.hidden/café_日本語.log")));
    QVERIFY(PathSanitizer::isSafeRelativePath(QStringLiteral("file..with..dots.log")));

    QVERIFY(!PathSanitizer::isSafeRelativePath(QString()));
    QVERIFY(!PathSanitizer::isSafeRelativePath(QStringLiteral(".")));
    QVERIFY(!PathSanitizer::isSafeRelativePath(QStringLiteral("..")));
    QVERIFY(!PathSanitizer::isSafeRelativePath(QStringLiteral("../etc/passwd")));
    QVERIFY(!PathSanitizer::isSafeRelativePath(QStringLiteral("a/../b.log")));
    QVERIFY(!PathSanitizer::isSafeRelativePath(QStringLiteral("/etc/passwd")));
    QVERIFY(!PathSanitizer::isSafeRelativePath(QStringLiteral("bad\nname.log")));
    QVERIFY(!PathSanitizer::isSafeRelativePath(QStringLiteral("bad\rname.log")));

    QString withNul = QStringLiteral("bad");
    withNul.append(QChar(0));
    withNul.append(QStringLiteral("name.log"));
    QVERIFY(!PathSanitizer::isSafeRelativePath(withNul));
}

void TestCoreRemote::pathSanitizerWriteFilesFromList() {
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QString listPath = tmpDir.filePath(QStringLiteral("files.from0"));
    QVector<FileEntry> entries;
    entries.append(FileEntry{QStringLiteral("app.log"), 10, {}});
    entries.append(FileEntry{QStringLiteral("sub dir/café_日本語.log"), 20, {}});

    QString err;
    QVERIFY2(PathSanitizer::writeFilesFromList(listPath, entries, &err), qPrintable(err));

    QFile f(listPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray raw = f.readAll();
    const QByteArray expected = QByteArray("app.log\0", 8)
                                + QStringLiteral("sub dir/café_日本語.log").toUtf8()
                                + QByteArray("\0", 1);
    QCOMPARE(raw, expected);

    // Unsafe path must be rejected and not written
    const QString badListPath = tmpDir.filePath(QStringLiteral("bad.from0"));
    QVector<FileEntry> badEntries = entries;
    badEntries.append(FileEntry{QStringLiteral("../escape.log"), 5, {}});
    QVERIFY(!PathSanitizer::writeFilesFromList(badListPath, badEntries, &err));
    QVERIFY(!QFile::exists(badListPath));
}

void TestCoreRemote::rsyncListerParseListOutput() {
    const QByteArray sample = QStringLiteral(
        "drwxrwxr-x        4096 2026/11/19 20:43:39 .\n"
        "-rw-rw-r--          10 2026/11/19 20:43:39 app.log\n"
        "lrwxrwxrwx           7 2026/11/19 20:43:39 link.log\n"
        "drwxrwxr-x        4096 2026/11/19 20:43:39 sub dir\n"
        "-rw-rw-r--       4,096 2026/11/19 20:43:39 sub dir/my log file.log\r\n"
        "drwxrwxr-x        4096 2026/11/19 20:43:39 sub dir/.hidden\n"
        "-rw-rw-r--           8 2026/11/19 20:43:39 sub dir/.hidden/café_日本語.log\n"
    ).toUtf8();

    const QVector<FileEntry> entries = RsyncLister::parseListOutput(sample);
    QCOMPARE(entries.size(), 3);

    QCOMPARE(entries[0].relPath, QStringLiteral("app.log"));
    QCOMPARE(entries[0].size, 10);
    QCOMPARE(entries[0].mtime, QDateTime(QDate(2026, 11, 19), QTime(20, 43, 39)));

    QCOMPARE(entries[1].relPath, QStringLiteral("sub dir/my log file.log"));
    QCOMPARE(entries[1].size, 4096);

    QCOMPARE(entries[2].relPath, QStringLiteral("sub dir/.hidden/café_日本語.log"));
    QCOMPARE(entries[2].size, 8);
}

void TestCoreRemote::rsyncListerParseEmptyAndUnsafe() {
    // Empty dir listing (only "." directory line)
    const QByteArray emptyDir = "drwxrwxr-x        4096 2026/11/19 20:43:39 .\n";
    QVERIFY(RsyncLister::parseListOutput(emptyDir).isEmpty());
    QVERIFY(RsyncLister::parseListOutput(QByteArray()).isEmpty());

    // Unsafe paths should be ignored
    const QByteArray unsafeLines =
        "-rw-rw-r--          10 2026/11/19 20:43:39 ../secret.txt\n"
        "-rw-rw-r--          10 2026/11/19 20:43:39 /etc/passwd\n"
        "-rw-rw-r--          10 2026/11/19 20:43:39 safe.log\n";
    const QVector<FileEntry> entries = RsyncLister::parseListOutput(unsafeLines);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries[0].relPath, QStringLiteral("safe.log"));
}

void TestCoreRemote::timeFilterExactEdgesAndOutside() {
    const QDateTime start(QDate(2026, 11, 10), QTime(8, 0, 0));
    const QDateTime end(QDate(2026, 11, 17), QTime(17, 0, 0));
    const TimeWindow window{start, end};

    PcListing listing;
    listing.pcId = QStringLiteral("pc01");
    listing.screenshots = {
        {QStringLiteral("before.png"), 100, start.addSecs(-1)},
        {QStringLiteral("at_start.png"), 200, start},
        {QStringLiteral("inside.png"), 300, start.addSecs(3600)},
        {QStringLiteral("at_end.png"), 400, end},
        {QStringLiteral("after.png"), 500, end.addSecs(1)},
    };

    const FilteredPc filtered = TimeFilter::filterPc(listing, window, true);
    QCOMPARE(filtered.pcId, QStringLiteral("pc01"));
    QCOMPARE(filtered.screenshots.size(), 3);
    QCOMPARE(filtered.screenshots[0].relPath, QStringLiteral("at_start.png"));
    QCOMPARE(filtered.screenshots[1].relPath, QStringLiteral("inside.png"));
    QCOMPARE(filtered.screenshots[2].relPath, QStringLiteral("at_end.png"));
    QCOMPARE(filtered.shotsBytes, 900);
}

void TestCoreRemote::timeFilterLogsRuleOnVsOff() {
    const QDateTime start(QDate(2026, 11, 10), QTime(8, 0, 0));
    const QDateTime end(QDate(2026, 11, 17), QTime(17, 0, 0));
    const TimeWindow window{start, end};

    PcListing listing;
    listing.pcId = QStringLiteral("pc01");
    listing.logs = {
        {QStringLiteral("old.log"), 100, start.addSecs(-1)},
        {QStringLiteral("mid.log"), 200, start.addSecs(60)},
        {QStringLiteral("active_after_end.log"), 400, end.addSecs(3600)},
    };

    const FilteredPc ruleOn = TimeFilter::filterPc(listing, window, true);
    QCOMPARE(ruleOn.logs.size(), 2);
    QCOMPARE(ruleOn.logsBytes, 600);

    const FilteredPc ruleOff = TimeFilter::filterPc(listing, window, false);
    QCOMPARE(ruleOff.logs.size(), 1);
    QCOMPARE(ruleOff.logs[0].relPath, QStringLiteral("mid.log"));
    QCOMPARE(ruleOff.logsBytes, 200);
}

void TestCoreRemote::timeFilterDstTransitionDay() {
    // Use America/New_York spring-forward day (2026-03-08: 01:59:59 EST -> 03:00:00 EDT)
    const QTimeZone tz("America/New_York");
    QVERIFY(tz.isValid());

    const QDateTime start(QDate(2026, 3, 8), QTime(1, 0, 0), tz);
    const QDateTime end(QDate(2026, 3, 8), QTime(4, 0, 0), tz);
    const TimeWindow window{start, end};
    QVERIFY(window.isValid());

    PcListing listing;
    listing.pcId = QStringLiteral("pc_dst");
    listing.screenshots = {
        {QStringLiteral("before_jump.png"), 10, QDateTime(QDate(2026, 3, 8), QTime(1, 30, 0), tz)},
        {QStringLiteral("after_jump.png"), 20, QDateTime(QDate(2026, 3, 8), QTime(3, 30, 0), tz)},
        {QStringLiteral("outside.png"), 30, QDateTime(QDate(2026, 3, 8), QTime(4, 0, 1), tz)},
    };

    const FilteredPc filtered = TimeFilter::filterPc(listing, window, true);
    QCOMPARE(filtered.screenshots.size(), 2);
    QCOMPARE(filtered.shotsBytes, 30);
}

void TestCoreRemote::timeFilterInvalidWindow() {
    const QDateTime t(QDate(2026, 11, 10), QTime(8, 0, 0));

    PcListing listing;
    listing.pcId = QStringLiteral("pc01");
    listing.logs = {{QStringLiteral("a.log"), 100, t}};
    listing.screenshots = {{QStringLiteral("a.png"), 100, t}};

    // start == end
    const FilteredPc eqRes = TimeFilter::filterPc(listing, TimeWindow{t, t}, true);
    QVERIFY(eqRes.logs.isEmpty());
    QVERIFY(eqRes.screenshots.isEmpty());
    QCOMPARE(eqRes.totalBytes(), 0);

    // start > end
    const FilteredPc revRes = TimeFilter::filterPc(listing, TimeWindow{t.addSecs(60), t}, true);
    QVERIFY(revRes.logs.isEmpty());
    QVERIFY(revRes.screenshots.isEmpty());
}

void TestCoreRemote::spaceEstimatorExactAndMarginBoundary() {
    // 1000 bytes with 2% margin -> 1020 required
    const SpaceEstimate exactFit = SpaceEstimator::estimate({1000}, 1020, 0.02);
    QCOMPARE(exactFit.neededBytes, 1000);
    QCOMPARE(exactFit.requiredWithMarginBytes, 1020);
    QCOMPARE(exactFit.fits, true);
    QCOMPARE(exactFit.fittingPcCount, 1);

    // 1 byte short of margin boundary -> does not fit
    const SpaceEstimate oneByteShort = SpaceEstimator::estimate({1000}, 1019, 0.02);
    QCOMPARE(oneByteShort.fits, false);
    QCOMPARE(oneByteShort.fittingPcCount, 0);

    // Empty selection with positive free bytes -> fits
    const SpaceEstimate emptySel = SpaceEstimator::estimate({}, 1024, 0.02);
    QCOMPARE(emptySel.neededBytes, 0);
    QCOMPARE(emptySel.requiredWithMarginBytes, 0);
    QCOMPARE(emptySel.fits, true);
    QCOMPARE(emptySel.fittingPcCount, 0);
}

void TestCoreRemote::spaceEstimatorOverflowAndFittingCount() {
    // Unsorted PCs: 500, 100, 200, 1000
    // Sorted: 100 (req 102), +200=300 (req 306), +500=800 (req 816), +1000=1800 (req 1836)
    // With 820 bytes free -> 3 PCs fit (100, 200, 500), total 1800 does not fit.
    const SpaceEstimate est = SpaceEstimator::estimate({500, 100, 200, 1000}, 820, 0.02);
    QCOMPARE(est.neededBytes, 1800);
    QCOMPARE(est.requiredWithMarginBytes, 1836);
    QCOMPARE(est.fits, false);
    QCOMPARE(est.fittingPcCount, 3);

    // Zero free bytes -> nothing fits
    const SpaceEstimate noSpace = SpaceEstimator::estimate({100, 200}, 0, 0.02);
    QCOMPARE(noSpace.fits, false);
    QCOMPARE(noSpace.fittingPcCount, 0);
}

void TestCoreRemote::hostProbeClassifyResult() {
    QCOMPARE(HostProbe::classifyProbeResult(0, QString()), PcState::Online);

    QCOMPARE(HostProbe::classifyProbeResult(
                 255,
                 QStringLiteral("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
                                "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\n")),
             PcState::HostKeyMismatch);

    QCOMPARE(HostProbe::classifyProbeResult(
                 255,
                 QStringLiteral("Host key verification failed.\nrsync: connection unexpectedly closed")),
             PcState::HostKeyMismatch);

    QCOMPARE(HostProbe::classifyProbeResult(
                 255,
                 QStringLiteral("user@192.168.10.11: Permission denied (publickey).")),
             PcState::AuthFailed);

    QCOMPARE(HostProbe::classifyProbeResult(
                 255,
                 QStringLiteral("ssh: connect to host 192.168.10.11 port 22: Connection timed out")),
             PcState::Offline);

    QCOMPARE(HostProbe::classifyProbeResult(12, QStringLiteral("connection unexpectedly closed")),
             PcState::Offline);
}

QTEST_MAIN(TestCoreRemote)
#include "test_core_remote.moc"
