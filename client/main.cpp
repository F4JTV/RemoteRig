#include <QApplication>
#include <QIcon>
#include <QTextStream>
#include "../common/audioengine.h"
#include <QMetaType>
#include "clientwindow.h"
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
    app.setApplicationName("RemoteRigClient");
    app.setOrganizationName("F4JTV");

    app.setWindowIcon(QIcon(QStringLiteral(":/icons/remoterig-client.png")));

    rr::installTranslators(app, rr::readLanguage(QStringLiteral("RemoteRigClient")));

    qRegisterMetaType<rr::RigState>("rr::RigState");
    qRegisterMetaType<rr::ClientConfig>("rr::ClientConfig");
    qRegisterMetaType<rr::SpeechSettings>("rr::SpeechSettings");

    rr::ClientWindow w;
    w.show();
    return app.exec();
}
