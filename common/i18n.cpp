#include "i18n.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
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

void addLanguageMenu(QMainWindow *window, const QString &appKey)
{
    if (!window) return;

    QMenu *menu = window->menuBar()->addMenu(
        QCoreApplication::translate("Language", "&Language"));
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);

    struct Entry { Language lang; const char *label; };
    const Entry entries[] = {
        {Language::Auto,    QT_TRANSLATE_NOOP("Language", "System language")},
        {Language::French,  QT_TRANSLATE_NOOP("Language", "Français")},
        {Language::English, QT_TRANSLATE_NOOP("Language", "English")},
    };

    const Language current = readLanguage(appKey);

    for (const Entry &e : entries) {
        QAction *a = menu->addAction(QCoreApplication::translate("Language", e.label));
        a->setCheckable(true);
        a->setChecked(e.lang == current);
        group->addAction(a);
        const Language target = e.lang;
        QObject::connect(a, &QAction::triggered, window, [window, appKey, target] {
            if (readLanguage(appKey) == target) return;
            writeLanguage(appKey, target);

            const QMessageBox::StandardButton answer = QMessageBox::question(
                window,
                QCoreApplication::translate("Language", "Language"),
                QCoreApplication::translate(
                    "Language",
                    "The new language applies when the program starts again.\n"
                    "Restart now?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            if (answer == QMessageBox::Yes) {
                QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                        QCoreApplication::arguments().mid(1));
                window->close();
            }
        });
    }
}

} // namespace rr
