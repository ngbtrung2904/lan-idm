#pragma once

#include "SshOptions.h"
#include "Types.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <map>
#include <memory>
#include <vector>

class QProcess;

namespace dc {

class RsyncLister : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(RsyncLister)

public:
    explicit RsyncLister(QObject *parent);
    ~RsyncLister() override;

    static QVector<FileEntry> parseListOutput(const QByteArray &stdoutData);

    void listPc(const PcConfig &pc,
                const QString &sshUser,
                const QVector<LogSourceConfig> &logSources,
                const QString &shotsRelDir,
                const QString &recordingsRelDir,
                const SshParams &sshParams);
    void cancel();
    bool isRunning() const;

signals:
    void listingFinished(const QString &pcId, const dc::PcListing &listing);
    void listingFailed(const QString &pcId, const QString &error);

private:
    struct PendingJob {
        PcListing listing;
        std::vector<std::unique_ptr<QProcess>> logProcs;
        std::unique_ptr<QProcess> shotsProc;
        std::unique_ptr<QProcess> recordingsProc;
        int pendingCount = 0;
    };

    void startDirProcess(const QString &pcId,
                         int sourceIndex,
                         const QString &sourceName,
                         const QString &sshCmd,
                         const QString &remoteSpec);
    void cleanupJob(const QString &pcId, bool syncWait);
    void retireProcessAsync(std::unique_ptr<QProcess> proc);
    void eraseStoppingProcess(QProcess *proc);
    static QString normalizeRemoteDir(const QString &relDir);

    std::map<QString, PendingJob> mJobs;
    std::vector<std::unique_ptr<QProcess>> mStoppingProcesses;
};

} // namespace dc
