#include "ConfigStore.h"
#include "PathSanitizer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <cmath>

namespace dc {

namespace {

void setError(QString *error, const QString &message) {
    if (error) {
        *error = message;
    }
}

bool isIntegralJsonNumber(const QJsonValue &val, int *outInt) {
    if (!val.isDouble()) {
        return false;
    }
    const double d = val.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d) {
        return false;
    }
    if (d < -2147483648.0 || d > 2147483647.0) {
        return false;
    }
    if (outInt) {
        *outInt = static_cast<int>(d);
    }
    return true;
}

bool isSafePcId(const QString &id) {
    if (id.isEmpty() || id == QLatin1String(".") || id == QLatin1String("..")) {
        return false;
    }
    if (id.contains(QLatin1String(".."))) {
        return false;
    }
    for (const QChar &ch : id) {
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\')) {
            return false;
        }
        const ushort code = ch.unicode();
        if (code < 0x20 || code == 0x7F) {
            return false;
        }
    }
    return true;
}

} // namespace

QString ConfigStore::expandTilde(const QString &path) {
    if (path == QLatin1String("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

QString ConfigStore::resolveConfigPath(const QString &explicitPath) {
    const QString trimmedExplicit = explicitPath.trimmed();
    if (!trimmedExplicit.isEmpty()) {
        return expandTilde(trimmedExplicit);
    }

    const QString envPath = qEnvironmentVariable("DATA_COLLECTOR_CONFIG").trimmed();
    if (!envPath.isEmpty()) {
        return expandTilde(envPath);
    }

    return QDir::homePath() + QStringLiteral("/.config/DataCollector/config.json");
}

bool ConfigStore::loadDefault(AppConfig *outConfig, QString *error, const QString &explicitPath) {
    return load(resolveConfigPath(explicitPath), outConfig, error);
}

bool ConfigStore::load(const QString &filePath, AppConfig *outConfig, QString *error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open config file '%1': %2")
                            .arg(filePath, file.errorString()));
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();
    return parseJson(data, outConfig, error);
}

bool ConfigStore::parseJson(const QByteArray &jsonData, AppConfig *outConfig, QString *error) {
    if (!outConfig) {
        setError(error, QStringLiteral("Internal error: null output config"));
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("Invalid JSON at offset %1: %2")
                            .arg(parseError.offset)
                            .arg(parseError.errorString()));
        return false;
    }
    if (!doc.isObject()) {
        setError(error, QStringLiteral("Config root must be a JSON object"));
        return false;
    }

    const QJsonObject root = doc.object();
    AppConfig cfg;

    // sshUser (optional, defaults to "user", but if present must be non-empty string)
    if (root.contains(QStringLiteral("sshUser"))) {
        const QJsonValue v = root.value(QStringLiteral("sshUser"));
        if (!v.isString() || v.toString().trimmed().isEmpty()) {
            setError(error, QStringLiteral("Missing or invalid key: sshUser"));
            return false;
        }
        cfg.sshUser = v.toString().trimmed();
    }

    // sshKeyPath (required)
    {
        const QJsonValue v = root.value(QStringLiteral("sshKeyPath"));
        if (!v.isString() || v.toString().trimmed().isEmpty()) {
            setError(error, QStringLiteral("Missing or invalid key: sshKeyPath"));
            return false;
        }
        cfg.sshKeyPath = expandTilde(v.toString().trimmed());
    }

    // knownHostsPath (required)
    {
        const QJsonValue v = root.value(QStringLiteral("knownHostsPath"));
        if (!v.isString() || v.toString().trimmed().isEmpty()) {
            setError(error, QStringLiteral("Missing or invalid key: knownHostsPath"));
            return false;
        }
        cfg.knownHostsPath = expandTilde(v.toString().trimmed());
    }

    // maxParallel (optional, default 2, must be integer >= 1)
    if (root.contains(QStringLiteral("maxParallel"))) {
        int val = 0;
        if (!isIntegralJsonNumber(root.value(QStringLiteral("maxParallel")), &val) || val < 1) {
            setError(error, QStringLiteral("Missing or invalid key: maxParallel"));
            return false;
        }
        cfg.maxParallel = val;
    }

    // paths (optional object)
    if (root.contains(QStringLiteral("paths"))) {
        const QJsonValue pv = root.value(QStringLiteral("paths"));
        if (!pv.isObject()) {
            setError(error, QStringLiteral("Missing or invalid key: paths"));
            return false;
        }
        const QJsonObject po = pv.toObject();
        if (po.contains(QStringLiteral("logs"))) {
            const QJsonValue lv = po.value(QStringLiteral("logs"));
            if (!lv.isString() || lv.toString().trimmed().isEmpty()) {
                setError(error, QStringLiteral("Missing or invalid key: paths.logs"));
                return false;
            }
            cfg.paths.logs = lv.toString().trimmed();
        }
        if (po.contains(QStringLiteral("screenshots"))) {
            const QJsonValue sv = po.value(QStringLiteral("screenshots"));
            if (!sv.isString() || sv.toString().trimmed().isEmpty()) {
                setError(error, QStringLiteral("Missing or invalid key: paths.screenshots"));
                return false;
            }
            cfg.paths.screenshots = sv.toString().trimmed();
        }
    }

    // logs (optional object)
    if (root.contains(QStringLiteral("logs"))) {
        const QJsonValue lv = root.value(QStringLiteral("logs"));
        if (!lv.isObject()) {
            setError(error, QStringLiteral("Missing or invalid key: logs"));
            return false;
        }
        const QJsonObject lo = lv.toObject();
        if (lo.contains(QStringLiteral("includeModifiedAfterEnd"))) {
            const QJsonValue iv = lo.value(QStringLiteral("includeModifiedAfterEnd"));
            if (!iv.isBool()) {
                setError(error, QStringLiteral("Missing or invalid key: logs.includeModifiedAfterEnd"));
                return false;
            }
            cfg.logs.includeModifiedAfterEnd = iv.toBool();
        }
    }

    // pcs (required non-empty array)
    if (!root.contains(QStringLiteral("pcs")) || !root.value(QStringLiteral("pcs")).isArray()) {
        setError(error, QStringLiteral("Missing or invalid key: pcs"));
        return false;
    }
    const QJsonArray pcsArray = root.value(QStringLiteral("pcs")).toArray();
    if (pcsArray.isEmpty()) {
        setError(error, QStringLiteral("Missing or invalid key: pcs"));
        return false;
    }

    QSet<QString> seenIds;
    cfg.pcs.reserve(pcsArray.size());
    for (int i = 0; i < pcsArray.size(); ++i) {
        if (!pcsArray.at(i).isObject()) {
            setError(error, QStringLiteral("Missing or invalid key: pcs[%1]").arg(i));
            return false;
        }
        const QJsonObject po = pcsArray.at(i).toObject();
        PcConfig pc;

        const QJsonValue idVal = po.value(QStringLiteral("id"));
        if (!idVal.isString() || idVal.toString().trimmed().isEmpty()) {
            setError(error, QStringLiteral("Missing or invalid key: pcs[%1].id").arg(i));
            return false;
        }
        pc.id = idVal.toString().trimmed();
        if (!isSafePcId(pc.id)) {
            setError(error, QStringLiteral("Unsafe PC id: %1").arg(pc.id));
            return false;
        }
        if (seenIds.contains(pc.id)) {
            setError(error, QStringLiteral("Duplicate PC id: %1").arg(pc.id));
            return false;
        }
        seenIds.insert(pc.id);

        const QJsonValue hostVal = po.value(QStringLiteral("host"));
        if (!hostVal.isString() || hostVal.toString().trimmed().isEmpty()) {
            setError(error, QStringLiteral("Missing or invalid key: pcs[%1].host").arg(i));
            return false;
        }
        pc.host = hostVal.toString().trimmed();

        if (po.contains(QStringLiteral("port"))) {
            int port = 0;
            if (!isIntegralJsonNumber(po.value(QStringLiteral("port")), &port)
                || port < 1 || port > 65535) {
                setError(error, QStringLiteral("Missing or invalid key: pcs[%1].port").arg(i));
                return false;
            }
            pc.port = port;
        }

        if (po.contains(QStringLiteral("logSources"))) {
            const QJsonValue lsVal = po.value(QStringLiteral("logSources"));
            if (!lsVal.isArray()) {
                setError(error, QStringLiteral("Missing or invalid key: pcs[%1].logSources").arg(i));
                return false;
            }
            const QJsonArray lsArray = lsVal.toArray();
            QSet<QString> seenSubdirs;
            pc.logSources.reserve(lsArray.size());
            for (int j = 0; j < lsArray.size(); ++j) {
                if (!lsArray.at(j).isObject()) {
                    setError(error, QStringLiteral("Missing or invalid key: pcs[%1].logSources[%2]")
                                        .arg(i)
                                        .arg(j));
                    return false;
                }
                const QJsonObject lso = lsArray.at(j).toObject();
                LogSourceConfig src;

                const QJsonValue nameVal = lso.value(QStringLiteral("name"));
                if (!nameVal.isString() || nameVal.toString().trimmed().isEmpty()) {
                    setError(error, QStringLiteral("Missing or invalid key: pcs[%1].logSources[%2].name")
                                        .arg(i)
                                        .arg(j));
                    return false;
                }
                src.name = nameVal.toString().trimmed();

                const QJsonValue rpVal = lso.value(QStringLiteral("remotePath"));
                const QString remotePath = rpVal.isString() ? rpVal.toString().trimmed() : QString();
                if (remotePath.isEmpty() || !PathSanitizer::isSafeRelativePath(remotePath)) {
                    setError(error, QStringLiteral("Missing or invalid key: pcs[%1].logSources[%2].remotePath")
                                        .arg(i)
                                        .arg(j));
                    return false;
                }
                src.remotePath = remotePath;

                if (lso.contains(QStringLiteral("destSubdir"))) {
                    const QJsonValue dsVal = lso.value(QStringLiteral("destSubdir"));
                    if (!dsVal.isString()) {
                        setError(error, QStringLiteral("Missing or invalid key: pcs[%1].logSources[%2].destSubdir")
                                            .arg(i)
                                            .arg(j));
                        return false;
                    }
                    const QString destSubdir = dsVal.toString().trimmed();
                    if (!destSubdir.isEmpty()) {
                        if (!PathSanitizer::isSafeRelativePath(destSubdir)) {
                            setError(error, QStringLiteral("Missing or invalid key: pcs[%1].logSources[%2].destSubdir")
                                                .arg(i)
                                                .arg(j));
                            return false;
                        }
                        if (seenSubdirs.contains(destSubdir)) {
                            setError(error, QStringLiteral("Duplicate destSubdir in pcs[%1].logSources: %2")
                                                .arg(i)
                                                .arg(destSubdir));
                            return false;
                        }
                        seenSubdirs.insert(destSubdir);
                        src.destSubdir = destSubdir;
                    }
                }

                if (lso.contains(QStringLiteral("timestampSource"))) {
                    const QJsonValue tsVal = lso.value(QStringLiteral("timestampSource"));
                    if (!tsVal.isString()) {
                        setError(error, QStringLiteral("Missing or invalid key: pcs[%1].logSources[%2].timestampSource")
                                            .arg(i)
                                            .arg(j));
                        return false;
                    }
                    const QString tsStr = tsVal.toString().trimmed();
                    if (tsStr == QLatin1String("mtime")) {
                        src.timestampSource = TimestampSource::Mtime;
                    } else if (tsStr == QLatin1String("filename")) {
                        src.timestampSource = TimestampSource::Filename;
                    } else {
                        setError(error, QStringLiteral("Missing or invalid key: pcs[%1].logSources[%2].timestampSource")
                                            .arg(i)
                                            .arg(j));
                        return false;
                    }
                }

                pc.logSources.append(src);
            }
        }

        cfg.pcs.append(pc);
    }

    *outConfig = cfg;
    return true;
}

} // namespace dc
