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
    void configStoreLogSourcesValidAndInvalid();
    void configStoreMediaPathsAndSupportsFlags();
    void configStoreUiTheme();
    void configStoreLocalOutputPath();

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
    void timeFilterExtractFilenameDate();
    void timeFilterFilenameTimestampSources();
    void timeFilterExtractFilenameDateTime();
    void timeFilterMediaToggles();

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

void TestCoreRemote::configStoreLogSourcesValidAndInvalid() {
    AppConfig cfg;
    QString err;

    // Valid syslog + metadata custom logSources on pc07
    const QByteArray validJson = R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "pcs": [
            { "id": "pc01", "host": "10.0.0.1" },
            {
                "id": "pc07",
                "host": "10.0.0.7",
                "logSources": [
                    {
                        "name": "syslog",
                        "remotePath": ".app/aesardp/syslog",
                        "destSubdir": "syslog",
                        "timestampSource": "filename"
                    },
                    {
                        "name": "metadata",
                        "remotePath": "app/aesardp/metadata",
                        "destSubdir": "metadata",
                        "timestampSource": "filename"
                    }
                ]
            }
        ]
    })";
    QVERIFY2(ConfigStore::parseJson(validJson, &cfg, &err), qPrintable(err));
    QCOMPARE(cfg.pcs.size(), 2);
    QVERIFY(cfg.pcs[0].logSources.isEmpty());
    const QVector<LogSourceConfig> defaultResolved = resolveLogSources(cfg.pcs[0], cfg.paths);
    QCOMPARE(defaultResolved.size(), 1);
    QCOMPARE(defaultResolved[0].remotePath, cfg.paths.logs);
    QCOMPARE(defaultResolved[0].timestampSource, TimestampSource::Mtime);

    QCOMPARE(cfg.pcs[1].logSources.size(), 2);
    QCOMPARE(cfg.pcs[1].logSources[0].name, QStringLiteral("syslog"));
    QCOMPARE(cfg.pcs[1].logSources[0].remotePath, QStringLiteral(".app/aesardp/syslog"));
    QCOMPARE(cfg.pcs[1].logSources[0].destSubdir, QStringLiteral("syslog"));
    QCOMPARE(cfg.pcs[1].logSources[0].timestampSource, TimestampSource::Filename);
    QCOMPARE(cfg.pcs[1].logSources[1].name, QStringLiteral("metadata"));
    QCOMPARE(cfg.pcs[1].logSources[1].remotePath, QStringLiteral("app/aesardp/metadata"));
    QCOMPARE(cfg.pcs[1].logSources[1].destSubdir, QStringLiteral("metadata"));
    QCOMPARE(cfg.pcs[1].logSources[1].timestampSource, TimestampSource::Filename);

    // Invalid timestampSource
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "pcs": [{
            "id": "pc07",
            "host": "10.0.0.7",
            "logSources": [{
                "name": "syslog",
                "remotePath": ".app/aesardp/syslog",
                "destSubdir": "syslog",
                "timestampSource": "ctime"
            }]
        }]
    })", &cfg, &err));
    QCOMPARE(err, QStringLiteral("Missing or invalid key: pcs[0].logSources[0].timestampSource"));

    // Unsafe remotePath
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "pcs": [{
            "id": "pc07",
            "host": "10.0.0.7",
            "logSources": [{
                "name": "syslog",
                "remotePath": "../etc/passwd",
                "destSubdir": "syslog"
            }]
        }]
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("remotePath")), qPrintable(err));

    // Unsafe destSubdir
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "pcs": [{
            "id": "pc07",
            "host": "10.0.0.7",
            "logSources": [{
                "name": "syslog",
                "remotePath": ".app/aesardp/syslog",
                "destSubdir": "../escape"
            }]
        }]
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("destSubdir")), qPrintable(err));

    // Duplicate non-empty destSubdir
    err.clear();
    QVERIFY(!ConfigStore::parseJson(R"({
        "sshKeyPath": "/tmp/key",
        "knownHostsPath": "/tmp/kh",
        "pcs": [{
            "id": "pc07",
            "host": "10.0.0.7",
            "logSources": [
                {
                    "name": "syslog",
                    "remotePath": ".app/aesardp/syslog",
                    "destSubdir": "same"
                },
                {
                    "name": "metadata",
                    "remotePath": "app/aesardp/metadata",
                    "destSubdir": "same"
                }
            ]
        }]
    })", &cfg, &err));
    QVERIFY2(err.contains(QStringLiteral("destSubdir")), qPrintable(err));
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

    const QVector<LogSourceConfig> defaultSources = resolveLogSources(PcConfig{}, PathsConfig{});
    const FilteredPc filtered = TimeFilter::filterPc(listing, defaultSources, window, true, true, false);
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

    const QVector<LogSourceConfig> defaultSources = resolveLogSources(PcConfig{}, PathsConfig{});
    const FilteredPc ruleOn = TimeFilter::filterPc(listing, defaultSources, window, true, true, false);
    QCOMPARE(ruleOn.logs.size(), 2);
    QCOMPARE(ruleOn.logsBytes, 600);

    const FilteredPc ruleOff = TimeFilter::filterPc(listing, defaultSources, window, false, true, false);
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

    const QVector<LogSourceConfig> defaultSources = resolveLogSources(PcConfig{}, PathsConfig{});
    const FilteredPc filtered = TimeFilter::filterPc(listing, defaultSources, window, true, true, false);
    QCOMPARE(filtered.screenshots.size(), 2);
    QCOMPARE(filtered.shotsBytes, 30);
}

void TestCoreRemote::timeFilterInvalidWindow() {
    const QDateTime t(QDate(2026, 11, 10), QTime(8, 0, 0));

    PcListing listing;
    listing.pcId = QStringLiteral("pc01");
    listing.logs = {{QStringLiteral("a.log"), 100, t}};
    listing.screenshots = {{QStringLiteral("a.png"), 100, t}};

    const QVector<LogSourceConfig> defaultSources = resolveLogSources(PcConfig{}, PathsConfig{});

    // start == end
    const FilteredPc eqRes = TimeFilter::filterPc(listing, defaultSources, TimeWindow{t, t}, true, true, true);
    QVERIFY(eqRes.logs.isEmpty());
    QVERIFY(eqRes.screenshots.isEmpty());
    QCOMPARE(eqRes.totalBytes(), 0);

    // start > end
    const FilteredPc revRes = TimeFilter::filterPc(listing, defaultSources, TimeWindow{t.addSecs(60), t}, true, true, true);
    QVERIFY(revRes.logs.isEmpty());
    QVERIFY(revRes.screenshots.isEmpty());
}

void TestCoreRemote::timeFilterExtractFilenameDate() {
    QCOMPARE(TimeFilter::extractFilenameDate(QStringLiteral("appardp.LOG.20261101-002344.883159")),
             QDate(2026, 11, 1));
    QCOMPARE(TimeFilter::extractFilenameDate(QStringLiteral("minfo/minfo-rdp-20260929_084049_1")),
             QDate(2026, 9, 29));
    QCOMPARE(TimeFilter::extractFilenameDate(QStringLiteral("cinfo/cinfo-20261119.log")),
             QDate(2026, 11, 19));

    // Non-date or invalid date filenames
    QVERIFY(!TimeFilter::extractFilenameDate(QStringLiteral("app.log")).isValid());
    QVERIFY(!TimeFilter::extractFilenameDate(QStringLiteral("kinfo/report_123456789.log")).isValid());
    QVERIFY(!TimeFilter::extractFilenameDate(QStringLiteral("bad_20260230.log")).isValid());
    QVERIFY(!TimeFilter::extractFilenameDate(QStringLiteral("20261101/no_date_in_basename.log")).isValid());
}

void TestCoreRemote::timeFilterFilenameTimestampSources() {
    const QDateTime start(QDate(2026, 11, 1), QTime(8, 0, 0));
    const QDateTime end(QDate(2026, 11, 19), QTime(17, 0, 0));
    const TimeWindow window{start, end};

    // Deliberately set filesystem mtime far outside the window to prove filename date is used
    const QDateTime staleMtime(QDate(2020, 1, 1), QTime(0, 0, 0));
    const QDateTime futureMtime(QDate(2030, 1, 1), QTime(0, 0, 0));

    const QVector<LogSourceConfig> sources = {
        LogSourceConfig{QStringLiteral("syslog"), QStringLiteral(".app/aesardp/syslog"),
                        QStringLiteral("syslog"), TimestampSource::Filename},
        LogSourceConfig{QStringLiteral("metadata"), QStringLiteral("app/aesardp/metadata"),
                        QStringLiteral("metadata"), TimestampSource::Filename}
    };

    PcListing listing;
    listing.pcId = QStringLiteral("pc07");
    listing.logs = {
        {QStringLiteral("appardp.LOG.20261031-235959.000001"), 100, start.addSecs(60), 0},    // before window (mtime inside!)
        {QStringLiteral("appardp.LOG.20261101-002344.883159"), 200, staleMtime, 0},           // on start date (mtime stale!)
        {QStringLiteral("appardp.LOG.20261120-000001.000001"), 300, start.addSecs(120), 0},   // after window (mtime inside!)
        {QStringLiteral("minfo/minfo-rdp-20260929_084049_1"), 400, start.addSecs(180), 1},    // before window
        {QStringLiteral("cinfo/cinfo-20261119.log"), 500, futureMtime, 1},                    // on end date (mtime future!)
        {QStringLiteral("kinfo/kinfo-20261110_120000.dat"), 600, staleMtime, 1},              // inside window (mtime stale!)
        {QStringLiteral("kinfo/readme.txt"), 700, start.addSecs(60), 1}                       // no date in filename
    };

    const FilteredPc filtered = TimeFilter::filterPc(listing, sources, window, true, true, false);
    QCOMPARE(filtered.logs.size(), 3);
    QCOMPARE(filtered.logs[0].relPath, QStringLiteral("appardp.LOG.20261101-002344.883159"));
    QCOMPARE(filtered.logs[0].sourceIndex, 0);
    QCOMPARE(filtered.logs[1].relPath, QStringLiteral("cinfo/cinfo-20261119.log"));
    QCOMPARE(filtered.logs[1].sourceIndex, 1);
    QCOMPARE(filtered.logs[2].relPath, QStringLiteral("kinfo/kinfo-20261110_120000.dat"));
    QCOMPARE(filtered.logs[2].sourceIndex, 1);
    QCOMPARE(filtered.logsBytes, 1300);
}

void TestCoreRemote::configStoreMediaPathsAndSupportsFlags() {
    // Defaults when paths and supports* are omitted
    {
        AppConfig cfg;
        QString err;
        const QByteArray json = R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "pcs": [
                { "id": "pc01", "host": "192.168.10.11" },
                { "id": "pc07", "host": "192.168.10.17", "supportsPictures": false, "supportsRecordings": false }
            ]
        })";
        QVERIFY2(ConfigStore::parseJson(json, &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.paths.logs, QStringLiteral(".app"));
        QCOMPARE(cfg.paths.screenshots, QStringLiteral("Pictures/lightshots"));
        QCOMPARE(cfg.paths.recordings, QStringLiteral("recordings"));
        QCOMPARE(cfg.pcs.size(), 2);
        QCOMPARE(cfg.pcs[0].supportsPictures, true);
        QCOMPARE(cfg.pcs[0].supportsRecordings, true);
        QCOMPARE(cfg.pcs[1].supportsPictures, false);
        QCOMPARE(cfg.pcs[1].supportsRecordings, false);
    }

    // Explicit paths.recordings
    {
        AppConfig cfg;
        QString err;
        const QByteArray json = R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "paths": {
                "recordings": "custom_recs"
            },
            "pcs": [
                { "id": "pc01", "host": "192.168.10.11" }
            ]
        })";
        QVERIFY2(ConfigStore::parseJson(json, &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.paths.recordings, QStringLiteral("custom_recs"));
    }

    // Empty paths.recordings rejected
    {
        AppConfig cfg;
        QString err;
        QVERIFY(!ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "paths": { "recordings": "   " },
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err));
        QCOMPARE(err, QStringLiteral("Missing or invalid key: paths.recordings"));
    }

    // Non-boolean supportsPictures rejected
    {
        AppConfig cfg;
        QString err;
        QVERIFY(!ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "pcs": [{ "id": "pc01", "host": "10.0.0.1", "supportsPictures": "false" }]
        })", &cfg, &err));
        QCOMPARE(err, QStringLiteral("Missing or invalid key: pcs[0].supportsPictures"));
    }

    // Non-boolean supportsRecordings rejected
    {
        AppConfig cfg;
        QString err;
        QVERIFY(!ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "pcs": [{ "id": "pc01", "host": "10.0.0.1", "supportsRecordings": 0 }]
        })", &cfg, &err));
        QCOMPARE(err, QStringLiteral("Missing or invalid key: pcs[0].supportsRecordings"));
    }
}

void TestCoreRemote::configStoreUiTheme() {
    // Default when "ui" is omitted -> "light"
    {
        AppConfig cfg;
        QString err;
        QVERIFY2(ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.ui.theme, QStringLiteral("light"));
    }

    // Explicit "light"
    {
        AppConfig cfg;
        QString err;
        QVERIFY2(ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "ui": { "theme": "light" },
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.ui.theme, QStringLiteral("light"));
    }

    // Explicit "dark"
    {
        AppConfig cfg;
        QString err;
        QVERIFY2(ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "ui": { "theme": "dark" },
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.ui.theme, QStringLiteral("dark"));
    }

    // Empty "ui" object -> default "light"
    {
        AppConfig cfg;
        QString err;
        QVERIFY2(ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "ui": {},
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.ui.theme, QStringLiteral("light"));
    }

    // Invalid "ui.theme": "blue" rejected
    {
        AppConfig cfg;
        QString err;
        QVERIFY(!ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "ui": { "theme": "blue" },
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err));
        QCOMPARE(err, QStringLiteral("Missing or invalid key: ui.theme"));
    }

    // Non-string "ui.theme" rejected
    {
        AppConfig cfg;
        QString err;
        QVERIFY(!ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "ui": { "theme": 123 },
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err));
        QCOMPARE(err, QStringLiteral("Missing or invalid key: ui.theme"));
    }

    // Non-object "ui" rejected
    {
        AppConfig cfg;
        QString err;
        QVERIFY(!ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "ui": "dark",
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err));
        QCOMPARE(err, QStringLiteral("Missing or invalid key: ui"));
    }
}

void TestCoreRemote::configStoreLocalOutputPath() {
    // Default when "localOutputPath" is omitted -> expanded "~/DataCollector"
    {
        AppConfig cfg;
        QString err;
        QVERIFY2(ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.localOutputPath, QDir::homePath() + QStringLiteral("/DataCollector"));
    }

    // Explicit tilde path expanded
    {
        AppConfig cfg;
        QString err;
        QVERIFY2(ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "localOutputPath": "~/custom/out",
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.localOutputPath, QDir::homePath() + QStringLiteral("/custom/out"));
    }

    // Explicit absolute path preserved
    {
        AppConfig cfg;
        QString err;
        QVERIFY2(ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "localOutputPath": "/var/tmp/dc-output",
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err), qPrintable(err));
        QCOMPARE(cfg.localOutputPath, QStringLiteral("/var/tmp/dc-output"));
    }

    // Empty / whitespace string rejected
    {
        AppConfig cfg;
        QString err;
        QVERIFY(!ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "localOutputPath": "   ",
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err));
        QCOMPARE(err, QStringLiteral("Missing or invalid key: localOutputPath"));
    }

    // Non-string value rejected
    {
        AppConfig cfg;
        QString err;
        QVERIFY(!ConfigStore::parseJson(R"({
            "sshKeyPath": "/tmp/key",
            "knownHostsPath": "/tmp/kh",
            "localOutputPath": 42,
            "pcs": [{ "id": "pc01", "host": "10.0.0.1" }]
        })", &cfg, &err));
        QCOMPARE(err, QStringLiteral("Missing or invalid key: localOutputPath"));
    }
}

void TestCoreRemote::timeFilterExtractFilenameDateTime() {
    QCOMPARE(TimeFilter::extractFilenameDateTime(
                 QStringLiteral("screenshot_2026-11-19_22-31-40-115_screen0")),
             QDateTime(QDate(2026, 11, 19), QTime(22, 31, 40), Qt::LocalTime));

    QCOMPARE(TimeFilter::extractFilenameDateTime(
                 QStringLiteral("sub/dir/screenshot_2026-11-19_22-31-40-115_screen0.png")),
             QDateTime(QDate(2026, 11, 19), QTime(22, 31, 40), Qt::LocalTime));

    QCOMPARE(TimeFilter::extractFilenameDateTime(
                 QStringLiteral("rddm2-rec_Primary_20260929_105847_part000")),
             QDateTime(QDate(2026, 9, 29), QTime(10, 58, 47), Qt::LocalTime));

    QCOMPARE(TimeFilter::extractFilenameDateTime(
                 QStringLiteral("20260929/rddm2-rec_Primary_20260929_105847_part000.mp4")),
             QDateTime(QDate(2026, 9, 29), QTime(10, 58, 47), Qt::LocalTime));

    // Invalid filenames / dates / times
    QVERIFY(!TimeFilter::extractFilenameDateTime(QStringLiteral("plain.png")).isValid());
    QVERIFY(!TimeFilter::extractFilenameDateTime(QStringLiteral("screenshot_2026-02-30_10-00-00.png")).isValid());
    QVERIFY(!TimeFilter::extractFilenameDateTime(QStringLiteral("rddm2-rec_20260929_255847.mp4")).isValid());
    QVERIFY(!TimeFilter::extractFilenameDateTime(QStringLiteral("2026-11-19_22-31-40/no_ts.png")).isValid());
}

void TestCoreRemote::timeFilterMediaToggles() {
    const QDateTime start(QDate(2026, 11, 19), QTime(20, 0, 0), Qt::LocalTime);
    const QDateTime end(QDate(2026, 11, 19), QTime(23, 0, 0), Qt::LocalTime);
    const TimeWindow window{start, end};
    const QDateTime staleMtime(QDate(2020, 1, 1), QTime(0, 0, 0), Qt::LocalTime);

    PcListing listing;
    listing.pcId = QStringLiteral("pc01");
    listing.logs = {
        {QStringLiteral("app.log"), 100, start.addSecs(60), 0}
    };
    listing.screenshots = {
        {QStringLiteral("screenshot_2026-11-19_22-31-40-115_screen0.png"), 500, staleMtime, 0},
        {QStringLiteral("screenshot_2026-11-19_19-59-59-000_screen0.png"), 600, start.addSecs(60), 0},
        {QStringLiteral("fallback_mtime.png"), 700, start.addSecs(120), 0}
    };
    listing.recordings = {
        {QStringLiteral("rddm2-rec_Primary_20261119_210500_part000.mp4"), 2000, staleMtime, 0},
        {QStringLiteral("rddm2-rec_Primary_20260929_105847_part000.mp4"), 3000, start.addSecs(60), 0},
        {QStringLiteral("fallback_rec.mp4"), 4000, start.addSecs(180), 0}
    };

    const QVector<LogSourceConfig> defaultSources = resolveLogSources(PcConfig{}, PathsConfig{});

    // Both off -> only logs
    const FilteredPc none = TimeFilter::filterPc(listing, defaultSources, window, true, false, false);
    QCOMPARE(none.logs.size(), 1);
    QCOMPARE(none.screenshots.size(), 0);
    QCOMPARE(none.recordings.size(), 0);
    QCOMPARE(none.shotsBytes, 0);
    QCOMPARE(none.recordingsBytes, 0);
    QCOMPARE(none.totalBytes(), 100);

    // Pictures on, recordings off
    const FilteredPc picsOnly = TimeFilter::filterPc(listing, defaultSources, window, true, true, false);
    QCOMPARE(picsOnly.logs.size(), 1);
    QCOMPARE(picsOnly.screenshots.size(), 2);
    QCOMPARE(picsOnly.shotsBytes, 1200);
    QCOMPARE(picsOnly.recordings.size(), 0);
    QCOMPARE(picsOnly.recordingsBytes, 0);
    QCOMPARE(picsOnly.totalBytes(), 1300);

    // Pictures off, recordings on
    const FilteredPc recsOnly = TimeFilter::filterPc(listing, defaultSources, window, true, false, true);
    QCOMPARE(recsOnly.logs.size(), 1);
    QCOMPARE(recsOnly.screenshots.size(), 0);
    QCOMPARE(recsOnly.shotsBytes, 0);
    QCOMPARE(recsOnly.recordings.size(), 2);
    QCOMPARE(recsOnly.recordingsBytes, 6000);
    QCOMPARE(recsOnly.totalBytes(), 6100);

    // Both on
    const FilteredPc both = TimeFilter::filterPc(listing, defaultSources, window, true, true, true);
    QCOMPARE(both.logs.size(), 1);
    QCOMPARE(both.screenshots.size(), 2);
    QCOMPARE(both.recordings.size(), 2);
    QCOMPARE(both.totalBytes(), 7300);
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
