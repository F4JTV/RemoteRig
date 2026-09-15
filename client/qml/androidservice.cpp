#include "androidservice.h"

// QtGlobal en premier, sans condition : c'est lui qui definit Q_OS_ANDROID.
// L'oublier fait sauter les inclusions ci-dessous tout en laissant compiler le
// code qui en depend, et le compilateur propose alors QObject a la place de
// QJniObject.
#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QString>
#include <jni.h>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace rr {

static VolumePttSink *g_pttSink = nullptr;
void setVolumePttSink(VolumePttSink *sink) { g_pttSink = sink; }

#ifdef Q_OS_ANDROID

// Le type de retour de context() a change en cours de route : jobject avant
// Qt 6.7, QJniObject ensuite.
static QJniObject androidContext()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    return QNativeInterface::QAndroidApplication::context();
#else
    return QJniObject(QNativeInterface::QAndroidApplication::context());
#endif
}

// setClassName evite de passer par ComponentName : une methode de moins a
// traverser en JNI, et un point de rupture de moins.
static QJniObject serviceIntent()
{
    QJniObject intent("android/content/Intent", "()V");
    QJniObject className = QJniObject::fromString(
        QStringLiteral("org.remoterig.client.RemoteRigService"));
    const QJniObject context = androidContext();
    intent.callObjectMethod(
        "setClassName",
        "(Landroid/content/Context;Ljava/lang/String;)Landroid/content/Intent;",
        context.object(), className.object());
    return intent;
}

void startAndroidService()
{
    const QJniObject context = androidContext();
    const QJniObject intent = serviceIntent();
    // startForegroundService est exige a partir d'Android 8.
    context.callObjectMethod("startForegroundService",
                             "(Landroid/content/Intent;)Landroid/content/ComponentName;",
                             intent.object());
}

int androidStatusBarHeight()
{
    QJniObject context = androidContext();
    QJniObject resources = context.callObjectMethod(
        "getResources", "()Landroid/content/res/Resources;");
    if (!resources.isValid()) return 0;

    QJniObject name = QJniObject::fromString(QStringLiteral("status_bar_height"));
    QJniObject defType = QJniObject::fromString(QStringLiteral("dimen"));
    QJniObject defPackage = QJniObject::fromString(QStringLiteral("android"));
    const jint id = resources.callMethod<jint>(
        "getIdentifier",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
        name.object(), defType.object(), defPackage.object());
    if (id <= 0) return 0;

    return int(resources.callMethod<jint>("getDimensionPixelSize", "(I)I", id));
}

void stopAndroidService()
{
    const QJniObject context = androidContext();
    const QJniObject intent = serviceIntent();
    context.callMethod<jboolean>("stopService", "(Landroid/content/Intent;)Z",
                                 intent.object());
}

#else

void startAndroidService() {}
void stopAndroidService() {}
int androidStatusBarHeight() { return 0; }

#endif

} // namespace rr

#ifdef Q_OS_ANDROID
// Qt laisse passer les touches de volume au systeme : elles n'atteignent
// jamais le C++. C'est l'activite Java qui les intercepte et nous appelle.
extern "C" JNIEXPORT jboolean JNICALL
Java_org_remoterig_client_RemoteRigActivity_volumePttEnabled(JNIEnv *, jclass)
{
    return rr::g_pttSink && rr::g_pttSink->volumePttWanted() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_remoterig_client_RemoteRigActivity_volumePtt(JNIEnv *, jclass, jboolean pressed)
{
    if (rr::g_pttSink) rr::g_pttSink->volumePttChanged(pressed == JNI_TRUE);
}
#endif
