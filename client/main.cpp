#include <QApplication>
#include <QIcon>
#include <QMetaType>
#include "clientwindow.h"
#include "../common/i18n.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("RemoteRigClient");
    app.setOrganizationName("F4JTV");

    app.setWindowIcon(QIcon(QStringLiteral(":/icons/remoterig-client.png")));

    rr::installTranslators(app, rr::readLanguage(QStringLiteral("RemoteRigClient")));

    qRegisterMetaType<rr::RigState>("rr::RigState");
    qRegisterMetaType<rr::ClientConfig>("rr::ClientConfig");

    rr::ClientWindow w;
    w.show();
    return app.exec();
}
