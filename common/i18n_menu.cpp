// Menu de selection de la langue, pour l'interface Widgets uniquement.
// La version QML n'edite pas de barre de menus et ne compile pas ce fichier.
#include "i18n.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>

namespace rr {

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
