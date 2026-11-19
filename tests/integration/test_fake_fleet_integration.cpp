#include "ConfigStore.h"
#include "HostProbe.h"
#include "KeyAgent.h"
#include "Manifest.h"
#include "RsyncLister.h"
#include "SpaceEstimator.h"
#include "SshOptions.h"
#include "TimeFilter.h"
#include "TransferEngine.h"
#include "Types.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <unistd.h>

using namespace dc;

namespace {

struct ScopeGuard {
    std::function<void()> fn;
    ~ScopeGuard() {
        if (fn) {
            fn();
        }
    }
};

bool waitForKeyAgentUnlock(KeyAgent &agent,
                           const QString &keyPath,
                           const QString &passphrase,
                           bool *outUnlocked,
                           QString *outError,
                           int timeoutMs = 15000) {
    QEventLoop loop;
    bool finished = false;
    bool ok = false;
    QString err;

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    const auto c1 = QObject::connect(&agent, &KeyAgent::unlocked, &loop, [&]() {
        finished = true;
        ok = true;
        loop.quit();
    });
    const auto c2 = QObject::connect(&agent, &KeyAgent::unlockFailed, &loop, [&](const QString &reason) {
        finished = true;
        ok = false;
        err = reason;
        loop.quit();
    });

    timer.start(timeoutMs);
    agent.unlock(keyPath, passphrase);
    if (!finished) {
        loop.exec();
    }

    QObject::disconnect(c1);
    QObject::disconnect(c2);

    if (outUnlocked) {
        *outUnlocked = ok;
    }
    if (outError) {
        *outError = err;
    }
    return finished;
}

SshParams makeSshParams(const AppConfig &config, const QString &agentSocket, int port) {
    SshParams p;
    p.port = port;
    p.sshKeyPath = config.sshKeyPath;
    p.knownHostsPath = config.knownHostsPath;
    p.agentSocket = agentSocket;
    return p;
}

QHash<QString, PcState> probeAllPcs(const AppConfig &config,
                                    const QString &agentSocket,
                                    int timeoutMs = 30000) {
    HostProbe probe(nullptr);
    QHash<QString, PcState> results;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    QObject::connect(&probe, &HostProbe::probeFinished, &loop,
                     [&](const QString &pcId, PcState state, const QString &) {
                         results.insert(pcId, state);
                         if (results.size() == config.pcs.size()) {
                             loop.quit();
                         }
                     });

    timer.start(timeoutMs);
    for (const PcConfig &pc : config.pcs) {
        probe.probe(pc, config.sshUser, makeSshParams(config, agentSocket, pc.port));
    }
    if (results.size() < config.pcs.size()) {
        loop.exec();
    }
    return results;
}

QHash<QString, PcListing> listSelectedPcs(const AppConfig &config,
                                          const QString &agentSocket,
                                          const QStringList &pcIds,
                                          QStringList *outErrors = nullptr,
                                          int timeoutMs = 45000) {
    RsyncLister lister(nullptr);
    QHash<QString, PcListing> listings;
    int completed = 0;
    const int expected = pcIds.size();

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    QObject::connect(&lister, &RsyncLister::listingFinished, &loop,
                     [&](const QString &pcId, const PcListing &listing) {
                         listings.insert(pcId, listing);
                         ++completed;
                         if (completed >= expected) {
                             loop.quit();
                         }
                     });
    QObject::connect(&lister, &RsyncLister::listingFailed, &loop,
                     [&](const QString &pcId, const QString &error) {
                         if (outErrors) {
                             outErrors->append(QStringLiteral("%1: %2").arg(pcId, error));
                         }
                         ++completed;
                         if (completed >= expected) {
                             loop.quit();
                         }
                     });

    timer.start(timeoutMs);
    for (const PcConfig &pc : config.pcs) {
        if (!pcIds.contains(pc.id)) {
            continue;
        }
        lister.listPc(pc, config.sshUser, resolveLogSources(pc, config.paths), config.paths.screenshots,
                      makeSshParams(config, agentSocket, pc.port));
    }
    if (completed < expected) {
        loop.exec();
    }
    return listings;
}

bool waitForEnginePhases(TransferEngine &engine,
                         const QSet<EnginePhase> &targets,
                         int timeoutMs = 60000) {
    if (targets.contains(engine.phase())) {
        return true;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    const auto conn = QObject::connect(&engine, &TransferEngine::phaseChanged, &loop,
                                       [&](EnginePhase phase) {
                                           if (targets.contains(phase)) {
                                               loop.quit();
                                           }
                                       });

    timer.start(timeoutMs);
    loop.exec();
    QObject::disconnect(conn);
    return targets.contains(engine.phase());
}

bool hasPartialArtifacts(const QString &rootDir) {
    if (!QDir(rootDir).exists()) {
        return false;
    }
    QDirIterator it(rootDir,
                    QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileName() == QLatin1String(".partial")) {
            return true;
        }
    }
    return false;
}

int countChildRsyncProcesses() {
    const qint64 myPid = static_cast<qint64>(getpid());
    int count = 0;

    QDir procDir(QStringLiteral("/proc"));
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0) {
            continue;
        }

        QFile statusFile(QStringLiteral("/proc/%1/status").arg(pid));
        if (!statusFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QString name;
        qint64 ppid = -1;
        while (!statusFile.atEnd()) {
            const QByteArray line = statusFile.readLine();
            if (line.startsWith("Name:\t")) {
                name = QString::fromUtf8(line.mid(6)).trimmed();
            } else if (line.startsWith("PPid:\t")) {
                ppid = QString::fromUtf8(line.mid(6)).trimmed().toLongLong();
            }
            if (!name.isEmpty() && ppid >= 0) {
                break;
            }
        }

        if (ppid == myPid && name == QLatin1String("rsync")) {
            ++count;
        }
    }
    return count;
}

bool runUpShCommand(const QString &fleetDir, const QStringList &args, QString *outErr = nullptr) {
    const QString upScript = QStringLiteral(DC_SOURCE_DIR) + QStringLiteral("/scripts/fake-fleet/up.sh");
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DC_FAKE_FLEET_DIR"), fleetDir);
    proc.setProcessEnvironment(env);
    proc.start(upScript, args);
    if (!proc.waitForStarted(5000)) {
        if (outErr) {
            *outErr = QStringLiteral("Failed to start %1: %2").arg(upScript, proc.errorString());
        }
        return false;
    }
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        proc.waitForFinished(3000);
        if (outErr) {
            *outErr = QStringLiteral("Timed out running %1 %2").arg(upScript, args.join(QLatin1Char(' ')));
        }
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (outErr) {
            *outErr = QStringLiteral("up.sh exited %1: %2 %3")
                          .arg(proc.exitCode())
                          .arg(QString::fromUtf8(proc.readAllStandardOutput()),
                               QString::fromUtf8(proc.readAllStandardError()));
        }
        return false;
    }
    return true;
}

const PcConfig *findPc(const AppConfig &config, const QString &pcId) {
    for (const PcConfig &pc : config.pcs) {
        if (pc.id == pcId) {
            return &pc;
        }
    }
    return nullptr;
}

} // namespace

class TestFakeFleetIntegration : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testFullRunAndManifestVerification();
    void testCancelMidRun();
    void testUsbRemovedAndRetry();
    void testHostDropAndRetryPc();
    void testSoak300kEntries();
    void testKeyAgentCleanup();

private:
    QString mFleetDir;
    AppConfig mConfig;
};

void TestFakeFleetIntegration::initTestCase() {
    mFleetDir = qEnvironmentVariable("DC_FAKE_FLEET_DIR").trimmed();
    if (mFleetDir.isEmpty()) {
        QSKIP("DC_FAKE_FLEET_DIR is not set; skipping fake-fleet integration tests.");
    }

    const QString configPath = QDir(mFleetDir).filePath(QStringLiteral("config.json"));
    QVERIFY2(QFileInfo::exists(configPath),
             qPrintable(QStringLiteral("Expected fake-fleet config at %1").arg(configPath)));

    QString err;
    QVERIFY2(ConfigStore::load(configPath, &mConfig, &err), qPrintable(err));
    QCOMPARE(mConfig.pcs.size(), 6);
}

void TestFakeFleetIntegration::testFullRunAndManifestVerification() {
    KeyAgent agent(nullptr);
    ScopeGuard agentGuard{[&]() { agent.lock(); }};

    // 1. Wrong passphrase must fail cleanly
    bool unlocked = false;
    QString unlockErr;
    QVERIFY(waitForKeyAgentUnlock(agent, mConfig.sshKeyPath,
                                  QStringLiteral("definitely-wrong-pass"),
                                  &unlocked, &unlockErr));
    QVERIFY(!unlocked);
    QVERIFY(!agent.isUnlocked());
    QVERIFY(!unlockErr.isEmpty());

    // 2. Correct passphrase unlocks cleanly
    QVERIFY(waitForKeyAgentUnlock(agent, mConfig.sshKeyPath,
                                  QStringLiteral("testpass123"),
                                  &unlocked, &unlockErr));
    QVERIFY2(unlocked, qPrintable(unlockErr));
    QVERIFY(agent.isUnlocked());
    QVERIFY(!agent.socketPath().isEmpty());

    // 3. Probe all 6 PCs: pc01..pc04, pc06 Online; pc05 Offline
    const QHash<QString, PcState> states = probeAllPcs(mConfig, agent.socketPath());
    QCOMPARE(states.size(), 6);
    const QStringList onlineIds = {
        QStringLiteral("pc01"),
        QStringLiteral("pc02"),
        QStringLiteral("pc03"),
        QStringLiteral("pc04"),
        QStringLiteral("pc06")
    };
    for (const QString &id : onlineIds) {
        QCOMPARE(states.value(id, PcState::Unknown), PcState::Online);
    }
    QCOMPARE(states.value(QStringLiteral("pc05"), PcState::Unknown), PcState::Offline);

    // 4. List all online PCs via RsyncLister
    QStringList listErrors;
    const QHash<QString, PcListing> listings =
        listSelectedPcs(mConfig, agent.socketPath(), onlineIds, &listErrors);
    QVERIFY2(listErrors.isEmpty(), qPrintable(listErrors.join(QStringLiteral("; "))));
    QCOMPARE(listings.size(), 5);

    // 5. Filter last 7 days and build job specs
    const QDateTime now = QDateTime::currentDateTime();
    const TimeWindow window{now.addDays(-7), now};

    QHash<QString, FilteredPc> filteredByPc;
    QVector<TransferJobSpec> specs;
    for (const PcConfig &pc : mConfig.pcs) {
        TransferJobSpec spec;
        spec.pc = pc;
        if (pc.id == QLatin1String("pc05")) {
            spec.skipped = true;
            spec.skipReason = QStringLiteral("Offline");
        } else {
            const FilteredPc filtered = TimeFilter::filterPc(
                listings.value(pc.id), resolveLogSources(pc, mConfig.paths), window,
                mConfig.logs.includeModifiedAfterEnd);
            QVERIFY2(!filtered.logs.isEmpty(), qPrintable(pc.id));
            QVERIFY2(!filtered.screenshots.isEmpty(), qPrintable(pc.id));
            filteredByPc.insert(pc.id, filtered);
            spec.files = filtered;
        }
        specs.append(spec);
    }

    // 6. Run TransferEngine into a temp USB dir
    QTemporaryDir usbDir;
    QVERIFY(usbDir.isValid());
    const QString sessionDir = QDir(usbDir.path()).filePath(QStringLiteral("collect_full_run"));
    const QString operatorId = QStringLiteral("op-integration-01");

    TransferEngine engine(nullptr);
    engine.startSession(mConfig, agent.socketPath(), operatorId, window, sessionDir, specs, QStringLiteral("0.1.0"));

    QVERIFY(waitForEnginePhases(engine, {EnginePhase::Finished, EnginePhase::FinishedWithWarnings}, 90000));
    QCOMPARE(engine.phase(), EnginePhase::FinishedWithWarnings);
    QCOMPARE(engine.jobCount(), 6);

    // 7. Verify per-PC states, exact file counts/bytes, and absence of .partial
    QHash<QString, JobStatus> jobByPc;
    for (const JobStatus &st : engine.jobs()) {
        jobByPc.insert(st.pcId, st);
    }

    const QStringList cleanPcs = {
        QStringLiteral("pc01"),
        QStringLiteral("pc02"),
        QStringLiteral("pc03"),
        QStringLiteral("pc06")
    };
    for (const QString &id : cleanPcs) {
        const JobStatus st = jobByPc.value(id);
        const FilteredPc f = filteredByPc.value(id);
        QCOMPARE(st.state, JobState::Done);
        QVERIFY2(st.result.errors.isEmpty(), qPrintable(st.result.errors.join(QStringLiteral("; "))));
        QCOMPARE(st.result.logsRsyncExitCode, 0);
        QCOMPARE(st.result.shotsRsyncExitCode, 0);
        QCOMPARE(st.result.logsFilesPlanned, f.logs.size());
        QCOMPARE(st.result.logsFilesCopied, f.logs.size());
        QCOMPARE(st.result.logsBytesCopied, f.logsBytes);
        QCOMPARE(st.result.shotsFilesPlanned, f.screenshots.size());
        QCOMPARE(st.result.shotsFilesCopied, f.screenshots.size());
        QCOMPARE(st.result.shotsBytesCopied, f.shotsBytes);
        QVERIFY2(!hasPartialArtifacts(QDir(sessionDir).filePath(id)),
                 qPrintable(QStringLiteral("Lingering .partial under %1").arg(id)));
    }

    // pc04: DoneWithWarnings (rsync exit 23 due to chmod 000 unreadable_secret.log)
    {
        const JobStatus st = jobByPc.value(QStringLiteral("pc04"));
        const FilteredPc f = filteredByPc.value(QStringLiteral("pc04"));
        QCOMPARE(st.state, JobState::DoneWithWarnings);
        QCOMPARE(st.result.logsRsyncExitCode, 23);
        QCOMPARE(st.result.shotsRsyncExitCode, 0);
        QVERIFY(!st.result.errors.isEmpty());
        QCOMPARE(st.result.logsFilesPlanned, f.logs.size());
        QCOMPARE(st.result.logsFilesCopied, f.logs.size() - 1);
        QCOMPARE(st.result.shotsFilesPlanned, f.screenshots.size());
        QCOMPARE(st.result.shotsFilesCopied, f.screenshots.size());
        QCOMPARE(st.result.shotsBytesCopied, f.shotsBytes);
        QVERIFY(!hasPartialArtifacts(QDir(sessionDir).filePath(QStringLiteral("pc04"))));
    }

    // pc05: Skipped
    {
        const JobStatus st = jobByPc.value(QStringLiteral("pc05"));
        QCOMPARE(st.state, JobState::Skipped);
    }

    // 8. Verify manifest.json parses cleanly and matches job results
    const QString manifestPath = QDir(sessionDir).filePath(QStringLiteral("manifest.json"));
    QVERIFY(QFileInfo::exists(manifestPath));
    QFile mf(manifestPath);
    QVERIFY(mf.open(QIODevice::ReadOnly));
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &parseErr);
    QVERIFY2(parseErr.error == QJsonParseError::NoError, qPrintable(parseErr.errorString()));
    QVERIFY(doc.isObject());

    const QJsonObject root = doc.object();
    QCOMPARE(root.value(QStringLiteral("operatorId")).toString(), operatorId);
    QCOMPARE(root.value(QStringLiteral("status")).toString(), QStringLiteral("finished_with_warnings"));
    QVERIFY(!root.value(QStringLiteral("startedAt")).toString().isEmpty());
    QVERIFY(!root.value(QStringLiteral("finishedAt")).toString().isEmpty());

    const QJsonArray pcsArr = root.value(QStringLiteral("pcs")).toArray();
    QCOMPARE(pcsArr.size(), 6);

    for (const QJsonValue &val : pcsArr) {
        const QJsonObject obj = val.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString();
        QVERIFY(jobByPc.contains(id));
        const JobStatus st = jobByPc.value(id);

        QCOMPARE(obj.value(QStringLiteral("status")).toString(), Manifest::jobStateToString(st.state));
        QCOMPARE(obj.value(QStringLiteral("logsFilesPlanned")).toInt(), st.result.logsFilesPlanned);
        QCOMPARE(obj.value(QStringLiteral("logsFilesCopied")).toInt(), st.result.logsFilesCopied);
        QCOMPARE(static_cast<qint64>(obj.value(QStringLiteral("logsBytesCopied")).toDouble()),
                 st.result.logsBytesCopied);
        QCOMPARE(obj.value(QStringLiteral("shotsFilesPlanned")).toInt(), st.result.shotsFilesPlanned);
        QCOMPARE(obj.value(QStringLiteral("shotsFilesCopied")).toInt(), st.result.shotsFilesCopied);
        QCOMPARE(static_cast<qint64>(obj.value(QStringLiteral("shotsBytesCopied")).toDouble()),
                 st.result.shotsBytesCopied);
        QCOMPARE(obj.value(QStringLiteral("logsRsyncExitCode")).toInt(), st.result.logsRsyncExitCode);
        QCOMPARE(obj.value(QStringLiteral("shotsRsyncExitCode")).toInt(), st.result.shotsRsyncExitCode);
    }
}

void TestFakeFleetIntegration::testCancelMidRun() {
    KeyAgent agent(nullptr);
    ScopeGuard agentGuard{[&]() { agent.lock(); }};

    bool unlocked = false;
    QString err;
    QVERIFY(waitForKeyAgentUnlock(agent, mConfig.sshKeyPath, QStringLiteral("testpass123"), &unlocked, &err));
    QVERIFY2(unlocked, qPrintable(err));

    const QStringList pcIds = {
        QStringLiteral("pc01"),
        QStringLiteral("pc02"),
        QStringLiteral("pc03"),
        QStringLiteral("pc06")
    };
    const QHash<QString, PcListing> listings = listSelectedPcs(mConfig, agent.socketPath(), pcIds);
    QCOMPARE(listings.size(), pcIds.size());

    const QDateTime now = QDateTime::currentDateTime();
    const TimeWindow window{now.addDays(-7), now};

    QVector<TransferJobSpec> specs;
    for (const QString &id : pcIds) {
        const PcConfig *pc = findPc(mConfig, id);
        QVERIFY(pc != nullptr);
        TransferJobSpec spec;
        spec.pc = *pc;
        spec.files = TimeFilter::filterPc(listings.value(id), resolveLogSources(*pc, mConfig.paths),
                                          window, mConfig.logs.includeModifiedAfterEnd);
        specs.append(spec);
    }

    QTemporaryDir usbDir;
    QVERIFY(usbDir.isValid());
    const QString sessionDir = QDir(usbDir.path()).filePath(QStringLiteral("collect_cancel"));

    TransferEngine engine(nullptr);
    engine.startSession(mConfig, agent.socketPath(), QStringLiteral("op-cancel"), window, sessionDir, specs, QStringLiteral("0.1.0"));
    QCOMPARE(engine.phase(), EnginePhase::Running);

    // Cancel shortly after starting while rsync workers are active
    QTimer::singleShot(10, &engine, &TransferEngine::cancel);

    QVERIFY(waitForEnginePhases(engine, {EnginePhase::Cancelled}, 15000));
    QCOMPARE(engine.phase(), EnginePhase::Cancelled);

    // Ensure no lingering rsync child processes remain
    QTRY_COMPARE_WITH_TIMEOUT(countChildRsyncProcesses(), 0, 5000);

    // Verify manifest records "cancelled"
    const QString manifestPath = QDir(sessionDir).filePath(QStringLiteral("manifest.json"));
    QVERIFY(QFileInfo::exists(manifestPath));
    QFile mf(manifestPath);
    QVERIFY(mf.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
}

void TestFakeFleetIntegration::testUsbRemovedAndRetry() {
    KeyAgent agent(nullptr);
    ScopeGuard agentGuard{[&]() { agent.lock(); }};

    bool unlocked = false;
    QString err;
    QVERIFY(waitForKeyAgentUnlock(agent, mConfig.sshKeyPath, QStringLiteral("testpass123"), &unlocked, &err));
    QVERIFY2(unlocked, qPrintable(err));

    const QStringList pcIds = {QStringLiteral("pc01"), QStringLiteral("pc02")};
    const QHash<QString, PcListing> listings = listSelectedPcs(mConfig, agent.socketPath(), pcIds);
    QCOMPARE(listings.size(), 2);

    const QDateTime now = QDateTime::currentDateTime();
    const TimeWindow window{now.addDays(-7), now};

    QHash<QString, FilteredPc> filteredByPc;
    QVector<TransferJobSpec> specs;
    for (const QString &id : pcIds) {
        const PcConfig *pc = findPc(mConfig, id);
        QVERIFY(pc != nullptr);
        TransferJobSpec spec;
        spec.pc = *pc;
        spec.files = TimeFilter::filterPc(listings.value(id), resolveLogSources(*pc, mConfig.paths),
                                          window, mConfig.logs.includeModifiedAfterEnd);
        filteredByPc.insert(id, spec.files);
        specs.append(spec);
    }

    QTemporaryDir usbDir;
    QVERIFY(usbDir.isValid());
    const QString sessionDir = QDir(usbDir.path()).filePath(QStringLiteral("collect_usb_retry"));

    TransferEngine engine(nullptr);
    engine.startSession(mConfig, agent.socketPath(), QStringLiteral("op-usb"), window, sessionDir, specs, QStringLiteral("0.1.0"));
    QCOMPARE(engine.phase(), EnginePhase::Running);

    engine.pauseForUsbRemoved();
    QCOMPARE(engine.phase(), EnginePhase::UsbRemoved);

    // Wait briefly for detached rsync processes to exit cleanly, then resume
    QTRY_COMPARE_WITH_TIMEOUT(countChildRsyncProcesses(), 0, 5000);

    engine.retryAfterUsbReturned();
    QVERIFY(waitForEnginePhases(engine, {EnginePhase::Finished, EnginePhase::FinishedWithWarnings}, 60000));
    QCOMPARE(engine.phase(), EnginePhase::Finished);

    for (const JobStatus &st : engine.jobs()) {
        const FilteredPc f = filteredByPc.value(st.pcId);
        QCOMPARE(st.state, JobState::Done);
        QCOMPARE(st.result.logsFilesCopied, f.logs.size());
        QCOMPARE(st.result.shotsFilesCopied, f.screenshots.size());
        QCOMPARE(st.result.logsBytesCopied, f.logsBytes);
        QCOMPARE(st.result.shotsBytesCopied, f.shotsBytes);
    }
}

void TestFakeFleetIntegration::testHostDropAndRetryPc() {
    KeyAgent agent(nullptr);
    ScopeGuard agentGuard{[&]() { agent.lock(); }};

    bool unlocked = false;
    QString err;
    QVERIFY(waitForKeyAgentUnlock(agent, mConfig.sshKeyPath, QStringLiteral("testpass123"), &unlocked, &err));
    QVERIFY2(unlocked, qPrintable(err));

    const QStringList pcIds = {QStringLiteral("pc03"), QStringLiteral("pc01")};
    const QHash<QString, PcListing> listings = listSelectedPcs(mConfig, agent.socketPath(), pcIds);
    QCOMPARE(listings.size(), 2);

    const QDateTime now = QDateTime::currentDateTime();
    const TimeWindow window{now.addDays(-7), now};

    QHash<QString, FilteredPc> filteredByPc;
    QVector<TransferJobSpec> specs;
    for (const QString &id : pcIds) {
        const PcConfig *pc = findPc(mConfig, id);
        QVERIFY(pc != nullptr);
        TransferJobSpec spec;
        spec.pc = *pc;
        spec.files = TimeFilter::filterPc(listings.value(id), resolveLogSources(*pc, mConfig.paths),
                                          window, mConfig.logs.includeModifiedAfterEnd);
        filteredByPc.insert(id, spec.files);
        specs.append(spec);
    }

    // Ensure pc03 is always restarted even if an assertion fails
    bool pc03Stopped = false;
    ScopeGuard pc03Guard{[&]() {
        if (pc03Stopped) {
            runUpShCommand(mFleetDir, {QStringLiteral("start-pc"), QStringLiteral("pc03")});
        }
    }};

    QString upErr;
    QVERIFY2(runUpShCommand(mFleetDir, {QStringLiteral("stop-pc"), QStringLiteral("pc03")}, &upErr),
             qPrintable(upErr));
    pc03Stopped = true;

    QTemporaryDir usbDir;
    QVERIFY(usbDir.isValid());
    const QString sessionDir = QDir(usbDir.path()).filePath(QStringLiteral("collect_host_drop"));

    TransferEngine engine(nullptr);
    engine.startSession(mConfig, agent.socketPath(), QStringLiteral("op-drop"), window, sessionDir, specs, QStringLiteral("0.1.0"));
    QVERIFY(waitForEnginePhases(engine, {EnginePhase::Finished, EnginePhase::FinishedWithWarnings}, 60000));
    QCOMPARE(engine.phase(), EnginePhase::FinishedWithWarnings);

    // pc03 must be Failed while pc01 must be Done
    QCOMPARE(engine.jobAt(0).pcId, QStringLiteral("pc03"));
    QCOMPARE(engine.jobAt(0).state, JobState::Failed);
    QCOMPARE(engine.jobAt(1).pcId, QStringLiteral("pc01"));
    QCOMPARE(engine.jobAt(1).state, JobState::Done);

    // Bring pc03 back online and retry just pc03
    QVERIFY2(runUpShCommand(mFleetDir, {QStringLiteral("start-pc"), QStringLiteral("pc03")}, &upErr),
             qPrintable(upErr));
    pc03Stopped = false;

    engine.retryPc(QStringLiteral("pc03"));
    QVERIFY(waitForEnginePhases(engine, {EnginePhase::Finished, EnginePhase::FinishedWithWarnings}, 60000));

    const JobStatus pc03After = engine.jobAt(0);
    const FilteredPc f03 = filteredByPc.value(QStringLiteral("pc03"));
    QCOMPARE(pc03After.state, JobState::Done);
    QCOMPARE(pc03After.result.logsFilesCopied, f03.logs.size());
    QCOMPARE(pc03After.result.shotsFilesCopied, f03.screenshots.size());
    QCOMPARE(pc03After.result.logsBytesCopied, f03.logsBytes);
    QCOMPARE(pc03After.result.shotsBytesCopied, f03.shotsBytes);
    QCOMPARE(engine.jobAt(1).state, JobState::Done);
}

void TestFakeFleetIntegration::testSoak300kEntries() {
    constexpr int kPcCount = 6;
    constexpr int kLogsPerPc = 25000;
    constexpr int kShotsPerPc = 25000;
    constexpr int kTotalExpected = kPcCount * (kLogsPerPc + kShotsPerPc); // 300,000

    // Reference window: 2026-11-12 00:00:00 to 2026-11-19 00:00:00 (local time)
    const TimeWindow window{
        QDateTime(QDate(2026, 11, 12), QTime(0, 0, 0)),
        QDateTime(QDate(2026, 11, 19), QTime(0, 0, 0))
    };

    // 4 mtime buckets (equal split per 4 entries):
    //   0: 2026/11/05 10:00:00 -> before start (excluded for both logs & shots)
    //   1: 2026/11/15 10:00:00 -> inside window (included for both)
    //   2: 2026/11/18 10:00:00 -> inside window (included for both)
    //   3: 2026/11/20 10:00:00 -> after end (included for logs when includeModifiedAfterEnd=true, excluded for shots)
    const char *kDates[4] = {
        "2026/11/05 10:00:00",
        "2026/11/15 10:00:00",
        "2026/11/18 10:00:00",
        "2026/11/20 10:00:00"
    };

    QVector<QByteArray> logsRaw(kPcCount);
    QVector<QByteArray> shotsRaw(kPcCount);

    for (int p = 0; p < kPcCount; ++p) {
        logsRaw[p].reserve(kLogsPerPc * 64);
        shotsRaw[p].reserve(kShotsPerPc * 64);
        for (int i = 0; i < kLogsPerPc; ++i) {
            const int bucket = i % 4;
            const int size = 1000 + (i % 10);
            logsRaw[p].append("-rw-r--r-- ");
            logsRaw[p].append(QByteArray::number(size));
            logsRaw[p].append(' ');
            logsRaw[p].append(kDates[bucket]);
            logsRaw[p].append(" batch/log_");
            logsRaw[p].append(QByteArray::number(i));
            logsRaw[p].append(".log\n");
        }
        for (int i = 0; i < kShotsPerPc; ++i) {
            const int bucket = i % 4;
            const int size = 2000 + (i % 10);
            shotsRaw[p].append("-rw-r--r-- ");
            shotsRaw[p].append(QByteArray::number(size));
            shotsRaw[p].append(' ');
            shotsRaw[p].append(kDates[bucket]);
            shotsRaw[p].append(" batch/shot_");
            shotsRaw[p].append(QByteArray::number(i));
            shotsRaw[p].append(".png\n");
        }
    }

    QElapsedTimer timer;
    timer.start();

    int totalParsed = 0;
    int totalFilteredLogs = 0;
    int totalFilteredShots = 0;
    QVector<qint64> selectedPcBytes;
    selectedPcBytes.reserve(kPcCount);

    for (int p = 0; p < kPcCount; ++p) {
        PcListing listing;
        listing.pcId = QStringLiteral("pc0%1").arg(p + 1);
        listing.logs = RsyncLister::parseListOutput(logsRaw.at(p));
        listing.screenshots = RsyncLister::parseListOutput(shotsRaw.at(p));
        totalParsed += listing.logs.size() + listing.screenshots.size();

        const QVector<LogSourceConfig> defaultSources = resolveLogSources(PcConfig{}, PathsConfig{});
        const FilteredPc filtered = TimeFilter::filterPc(listing, defaultSources, window, true);
        totalFilteredLogs += filtered.logs.size();
        totalFilteredShots += filtered.screenshots.size();
        selectedPcBytes.append(filtered.totalBytes());
    }

    // 25,000 logs/PC: buckets 1, 2, 3 match -> 3/4 * 25,000 = 18,750 per PC -> 112,500 total
    // 25,000 shots/PC: buckets 1, 2 match -> 2/4 * 25,000 = 12,500 per PC -> 75,000 total
    const qint64 freeBytes = 10LL * 1024 * 1024 * 1024; // 10 GiB
    const SpaceEstimate est = SpaceEstimator::estimate(
        selectedPcBytes, freeBytes, SpaceEstimator::kDefaultMarginFraction);
    const qint64 elapsedMs = timer.elapsed();

    qInfo() << "Soak 300k parse+filter+estimate completed in" << elapsedMs << "ms";

    QCOMPARE(totalParsed, kTotalExpected);
    QCOMPARE(totalFilteredLogs, kPcCount * (kLogsPerPc * 3 / 4));
    QCOMPARE(totalFilteredShots, kPcCount * (kShotsPerPc / 2));
    QVERIFY(est.fits);
    QCOMPARE(est.fittingPcCount, kPcCount);
    QVERIFY(est.neededBytes > 0);
    QVERIFY2(elapsedMs < 5000,
             qPrintable(QStringLiteral("Soak test took %1 ms (limit: 5000 ms)").arg(elapsedMs)));
}

void TestFakeFleetIntegration::testKeyAgentCleanup() {
    KeyAgent agent(nullptr);
    bool unlocked = false;
    QString err;
    QVERIFY(waitForKeyAgentUnlock(agent, mConfig.sshKeyPath, QStringLiteral("testpass123"), &unlocked, &err));
    QVERIFY2(unlocked, qPrintable(err));
    QVERIFY(agent.isUnlocked());

    const QString sock = agent.socketPath();
    QVERIFY(!sock.isEmpty());
    QVERIFY(QFileInfo::exists(sock));

    agent.lock();
    QVERIFY(!agent.isUnlocked());
    QVERIFY(!QFileInfo::exists(sock));
}

QTEST_MAIN(TestFakeFleetIntegration)
#include "test_fake_fleet_integration.moc"
