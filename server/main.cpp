#include <QApplication>
#include <QMetaType>
#include "serverwindow.h"
#include "../common/i18n.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("RemoteRigServer");
    app.setOrganizationName("F4JTV");

    rr::installTranslators(app, rr::readLanguage(QStringLiteral("RemoteRigServer")));

    qRegisterMetaType<rr::RigState>("rr::RigState");
    qRegisterMetaType<rr::RigConfig>("rr::RigConfig");
    qRegisterMetaType<rr::ServerConfig>("rr::ServerConfig");

    rr::ServerWindow w;
    w.show();
    return app.exec();
}
