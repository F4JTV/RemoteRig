#include <QIcon>
#include <QMetaType>
#include <QTextStream>

#include "../common/audioengine.h"
#include "../common/i18n.h"
#include "cwkeyer.h"
#include "serverdaemon.h"
#include "serverwindow.h"

#include <QApplication>
#include <QCoreApplication>

#include <csignal>

namespace {

void printUsage()
{
    QTextStream(stdout)
        << "RemoteRig server\n\n"
        << "  remoterig-server                    graphical interface\n"
        << "  remoterig-server --headless         run without a desktop session\n"
        << "  remoterig-server --headless -v      ... and log frequency changes\n"
        << "  remoterig-server --config FILE      read an .ini instead of the saved settings\n"
        << "  remoterig-server --list-audio       list audio devices and exit\n"
        << "  remoterig-server --help\n\n"
        << "Headless mode reuses the settings made in the graphical interface.\n"
        << "Configure once with a display, then run without one.\n";
}

bool hasArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], name) == 0) return true;
    return false;
}

QString argValue(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc - 1; ++i)
        if (qstrcmp(argv[i], name) == 0) return QString::fromLocal8Bit(argv[i + 1]);
    return QString();
}

extern "C" void onTerminationSignal(int)
{
    // Un gestionnaire de signal ne peut presque rien faire sans risque : il se
    // contente de lever un drapeau, relu par un minuteur.
    rr::ServerDaemon::requestStop();
}

} // namespace

int main(int argc, char *argv[])
{
    if (hasArg(argc, argv, "--help") || hasArg(argc, argv, "-h")) {
        printUsage();
        return 0;
    }

    qRegisterMetaType<rr::RigState>("rr::RigState");
    qRegisterMetaType<rr::RigConfig>("rr::RigConfig");
    qRegisterMetaType<rr::ServerConfig>("rr::ServerConfig");
    qRegisterMetaType<rr::RigCaps>("rr::RigCaps");
    qRegisterMetaType<rr::CwKeyerConfig>("rr::CwKeyerConfig");

    // Diagnostic audio avant toute interface : utile quand un peripherique
    // n'apparait pas dans les listes.
    if (hasArg(argc, argv, "--list-audio")) {
        QCoreApplication probe(argc, argv);
        QTextStream(stdout) << rr::AudioEngine::describeDevices();
        rr::AudioEngine::terminateLibrary();
        return 0;
    }

    // ------------------------------------------------------ sans interface
    if (hasArg(argc, argv, "--headless")) {
        QCoreApplication app(argc, argv);
        app.setApplicationName(QStringLiteral("RemoteRigServer"));
        app.setOrganizationName(QStringLiteral("F4JTV"));

        rr::installTranslators(app, rr::readLanguage(QStringLiteral("RemoteRigServer")));

        std::signal(SIGINT, onTerminationSignal);
        std::signal(SIGTERM, onTerminationSignal);

        rr::ServerDaemon daemon;
        const bool verbose = hasArg(argc, argv, "-v") || hasArg(argc, argv, "--verbose");
        if (!daemon.start(argValue(argc, argv, "--config"), verbose))
            return 1;

        const int code = app.exec();
        daemon.stop();
        return code;
    }

    // ------------------------------------------------------ avec interface
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RemoteRigServer"));
    app.setOrganizationName(QStringLiteral("F4JTV"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/remoterig-server.png")));

    rr::installTranslators(app, rr::readLanguage(QStringLiteral("RemoteRigServer")));

    rr::ServerWindow w;
    w.show();
    return app.exec();
}
