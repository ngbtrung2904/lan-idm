#include "AuthController.h"
#include "SourcesController.h"
#include "TransferController.h"
#include "core/ConfigStore.h"
#include "core/KeyAgent.h"
#include "core/TransferEngine.h"
#include "core/UsbMonitor.h"
#include "stubs/StubControllers.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("DataCollector"));
    app.setOrganizationName(QStringLiteral("DataCollector"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Data Collector — Collect logs and screenshots onto a USB drive"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption demoOption(
        QStringLiteral("demo"),
        QStringLiteral("Run in demo mode using mock data (no SSH or config.json required)."));
    QCommandLineOption configOption(
        QStringLiteral("config"),
        QStringLiteral("Path to config.json override."),
        QStringLiteral("path"));
    parser.addOption(demoOption);
    parser.addOption(configOption);
    parser.process(app);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    static const char *const kFontPaths[] = {
        ":/fonts/Inter-Regular.ttf",
        ":/fonts/Inter-Medium.ttf",
        ":/fonts/Inter-SemiBold.ttf",
        ":/fonts/JetBrainsMono-Regular.ttf"
    };

    for (const char *fontPath : kFontPaths) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(fontPath)) < 0) {
            qCritical().nospace() << "op=\"load_font\" path=\"" << fontPath
                                  << "\" err=\"addApplicationFont failed\"";
            return 1;
        }
    }

    const QString kAppVersion = QStringLiteral(APP_VERSION);
    const QString kAppAuthor = QStringLiteral("noname");

    if (parser.isSet(demoOption)) {
        dc::StubAuthController authController(nullptr);
        dc::StubSourcesController sourcesController(nullptr);
        dc::StubTransferController transferController(nullptr);

        QObject::connect(&sourcesController, &dc::StubSourcesController::copyRequested,
                         &transferController, [&]() {
                             transferController.startFromSources(&sourcesController);
                         });

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("authController"), &authController);
        engine.rootContext()->setContextProperty(QStringLiteral("sourcesController"), &sourcesController);
        engine.rootContext()->setContextProperty(QStringLiteral("transferController"), &transferController);
        engine.rootContext()->setContextProperty(QStringLiteral("initialDarkTheme"), false);
        engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), kAppVersion);
        engine.rootContext()->setContextProperty(QStringLiteral("appAuthor"), kAppAuthor);

        const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreated, &app,
            [url](QObject *obj, const QUrl &objUrl) {
                if (obj == nullptr && url == objUrl) {
                    QCoreApplication::exit(-1);
                }
            },
            Qt::QueuedConnection);
        engine.load(url);

        return app.exec();
    }

    const QString explicitConfigPath = parser.value(configOption);
    const QString resolvedConfigPath = dc::ConfigStore::resolveConfigPath(explicitConfigPath);

    dc::AppConfig config;
    QString configError;
    if (!dc::ConfigStore::loadDefault(&config, &configError, explicitConfigPath)) {
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("configErrorText"), configError);
        engine.rootContext()->setContextProperty(QStringLiteral("configPathText"), resolvedConfigPath);
        engine.rootContext()->setContextProperty(QStringLiteral("initialDarkTheme"), false);
        engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), kAppVersion);
        engine.rootContext()->setContextProperty(QStringLiteral("appAuthor"), kAppAuthor);

        const QUrl url(QStringLiteral("qrc:/qml/ConfigErrorWindow.qml"));
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreated, &app,
            [url](QObject *obj, const QUrl &objUrl) {
                if (obj == nullptr && url == objUrl) {
                    QCoreApplication::exit(-1);
                }
            },
            Qt::QueuedConnection);
        engine.load(url);

        return app.exec();
    }

    dc::KeyAgent keyAgent(nullptr);
    dc::UsbMonitor usbMonitor(nullptr);
    dc::TransferEngine transferEngine(nullptr);

    dc::AuthController authController(config, &keyAgent, nullptr);
    dc::SourcesController sourcesController(config, &keyAgent, &usbMonitor, nullptr);
    dc::TransferController transferController(&transferEngine, &usbMonitor, nullptr);

    QObject::connect(&authController, &dc::AuthController::stateChanged,
                     &app, [&]() {
                         if (authController.state() == dc::AuthController::LoggedIn) {
                             usbMonitor.start();
                             sourcesController.rescanPcs();
                         } else if (authController.state() == dc::AuthController::LoggedOut) {
                             transferController.shutdownNow();
                             sourcesController.cancelPending();
                             usbMonitor.stop();
                         }
                     });

    QObject::connect(&sourcesController, &dc::SourcesController::copyRequested,
                     &transferController, [&]() {
                         transferController.startSession(&sourcesController,
                                                         authController.operatorId(),
                                                         keyAgent.socketPath());
                     });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&]() {
        transferController.shutdownNow();
        sourcesController.cancelPending();
        usbMonitor.stop();
        authController.lockSync();
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("authController"), &authController);
    engine.rootContext()->setContextProperty(QStringLiteral("sourcesController"), &sourcesController);
    engine.rootContext()->setContextProperty(QStringLiteral("transferController"), &transferController);
    engine.rootContext()->setContextProperty(QStringLiteral("initialDarkTheme"),
                                             config.ui.theme == QStringLiteral("dark"));
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), kAppVersion);
    engine.rootContext()->setContextProperty(QStringLiteral("appAuthor"), kAppAuthor);

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (obj == nullptr && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
