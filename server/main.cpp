#include <QApplication>
#include <QIcon>
#include <QTextStream>
#include "../common/audioengine.h"
#include <QMetaType>
#include "serverwindow.h"
#include "../common/i18n.h"

int main(int argc, char *argv[])
{
    // Diagnostic audio avant toute interface : utile quand un périphérique
    // n'apparaît pas dans les listes.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--list-audio") == 0) {
            QCoreApplication probe(argc, argv);
            QTextStream(stdout) << rr::AudioEngine::describeDevices();
            rr::AudioEngine::terminateLibrary();
            return 0;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("RemoteRigServer");
    app.setOrganizationName("F4JTV");

    app.setWindowIcon(QIcon(QStringLiteral(":/icons/remoterig-server.png")));

    rr::installTranslators(app, rr::readLanguage(QStringLiteral("RemoteRigServer")));

    qRegisterMetaType<rr::RigState>("rr::RigState");
    qRegisterMetaType<rr::RigConfig>("rr::RigConfig");
    qRegisterMetaType<rr::ServerConfig>("rr::ServerConfig");

    rr::ServerWindow w;
    w.show();
    return app.exec();
}
