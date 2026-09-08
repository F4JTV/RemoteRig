// Selection de la langue et chargement des traductions.
// Les chaines sources sont en anglais ; le francais vient d'un .qm embarque.
#pragma once

#include <QString>

class QCoreApplication;
class QMainWindow;

namespace rr {

enum class Language { Auto, French, English };

// Lecture/ecriture dans QSettings("F4JTV", appKey), cle "language".
Language readLanguage(const QString &appKey);
void     writeLanguage(const QString &appKey, Language lang);

// "auto", "fr" ou "en".
QString languageCode(Language lang);
Language languageFromCode(const QString &code);

// Langue effective une fois Auto resolu d'apres la locale systeme.
QString resolveLanguage(Language lang);

// Installe le traducteur de l'application et celui de Qt. A appeler avant
// de construire la moindre fenetre. Renvoie la langue effective.
QString installTranslators(QCoreApplication &app, Language lang);

// Ajoute le menu de selection de langue a la barre de menus.
void addLanguageMenu(QMainWindow *window, const QString &appKey);

} // namespace rr
