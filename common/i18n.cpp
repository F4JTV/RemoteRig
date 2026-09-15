#include "i18n.h"

// Ce fichier ne depend que de Qt Core : il sert aux deux interfaces.
// Le menu de langue, qui a besoin de QtWidgets, vit dans i18n_menu.cpp.
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace rr {

static QTranslator *g_appTranslator = nullptr;
static QTranslator *g_qtTranslator  = nullptr;

QString languageCode(Language lang)
{
    switch (lang) {
    case Language::French:  return QStringLiteral("fr");
    case Language::English: return QStringLiteral("en");
    default:                return QStringLiteral("auto");
    }
}

Language languageFromCode(const QString &code)
{
    if (code == QLatin1String("fr")) return Language::French;
    if (code == QLatin1String("en")) return Language::English;
    return Language::Auto;
}

Language readLanguage(const QString &appKey)
{
    QSettings s(QStringLiteral("F4JTV"), appKey);
    return languageFromCode(s.value(QStringLiteral("language"),
                                    QStringLiteral("auto")).toString());
}

void writeLanguage(const QString &appKey, Language lang)
{
    QSettings s(QStringLiteral("F4JTV"), appKey);
    s.setValue(QStringLiteral("language"), languageCode(lang));
}

QString resolveLanguage(Language lang)
{
    if (lang == Language::French)  return QStringLiteral("fr");
    if (lang == Language::English) return QStringLiteral("en");
    // Auto : francais si la locale systeme l'est, anglais sinon.
    return QLocale::system().name().startsWith(QLatin1String("fr"))
               ? QStringLiteral("fr") : QStringLiteral("en");
}

QString installTranslators(QCoreApplication &app, Language lang)
{
    const QString code = resolveLanguage(lang);

    // L'anglais est la langue des chaines sources : rien a charger.
    if (code == QLatin1String("en")) return code;

    if (!g_appTranslator) g_appTranslator = new QTranslator(&app);
    if (g_appTranslator->load(QStringLiteral(":/i18n/remoterig_") + code))
        app.installTranslator(g_appTranslator);

    // Boutons standard des boites de dialogue Qt.
    if (!g_qtTranslator) g_qtTranslator = new QTranslator(&app);
    const QString qtPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (g_qtTranslator->load(QStringLiteral("qtbase_") + code, qtPath))
        app.installTranslator(g_qtTranslator);

    return code;
}

} // namespace rr
