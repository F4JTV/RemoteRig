// Point d'entree de la version tactile : QML au lieu de Widgets.
// Le coeur reseau, le protocole et le traitement de la voix sont les memes.
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QKeyEvent>
#include <QQmlContext>
#include <qqml.h>

#include "clientbridge.h"
#include "../../common/i18n.h"

#ifdef Q_OS_ANDROID
#include <QPermissions>
#include "androidservice.h"
#endif

// PTT sur la touche volume bas.
//
// Le filtre est pose sur l'application entiere, pas sur un element de
// l'interface : les touches de volume ne suivent pas le focus, et un champ de
// saisie actif les capterait sinon. L'evenement est consomme pour que le
// volume ne bouge pas pendant l'emission.
class VolumeKeyPtt : public QObject {
public:
    explicit VolumeKeyPtt(rr::ClientBridge *bridge, QObject *parent = nullptr)
        : QObject(parent), m_bridge(bridge) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!m_bridge->pttOnVolumeKey())
            return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_VolumeDown && !key->isAutoRepeat()) {
                m_bridge->setPtt(event->type() == QEvent::KeyPress);
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    rr::ClientBridge *m_bridge = nullptr;
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RemoteRigClient"));
    app.setOrganizationName(QStringLiteral("F4JTV"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/remoterig-client.png")));

    rr::installTranslators(app, rr::readLanguage(QStringLiteral("RemoteRigClient")));

#ifdef Q_OS_ANDROID
    // Sans la permission, Oboe ouvre un flux d'entree qui ne rend que du
    // silence, sans signaler la moindre erreur.
    QMicrophonePermission micPermission;
    if (app.checkPermission(micPermission) == Qt::PermissionStatus::Undetermined)
        app.requestPermission(micPermission, [](const QPermission &) {});
#endif

    rr::ClientBridge bridge;
    app.installEventFilter(new VolumeKeyPtt(&bridge, &app));

    // Singleton plutot que propriete de contexte : Qt 6 deconseille la
    // seconde, qui empeche la compilation anticipee du QML et prive l'outillage
    // de toute verification de type.
    qmlRegisterSingletonInstance("RemoteRig", 1, 0, "Station", &bridge);

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return -1;

    return app.exec();
}
