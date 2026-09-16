// Fenetre « A propos », commune au serveur et au client Widgets.
// La version QML n'edite pas de barre de menus et ne compile pas ce fichier.
#pragma once

#include <QString>

class QMainWindow;
class QWidget;

namespace rr {

// Ajoute un menu « ? » portant une entree « A propos ».
// iconResource designe le logo dans les ressources, summary resume en une
// phrase ce que fait le programme.
void addAboutMenu(QMainWindow *window, const QString &appName,
                  const QString &iconResource, const QString &summary);

void showAboutDialog(QWidget *parent, const QString &appName,
                     const QString &iconResource, const QString &summary);

// Ouvre le manuel dans le navigateur du systeme. Il est embarque dans
// l'executable : on l'extrait dans un fichier temporaire avant de l'ouvrir,
// ce qui le rend disponible quelle que soit la facon dont l'application a ete
// installee, ou meme lancee depuis un dossier de compilation.
void openUserManual(QWidget *parent);

} // namespace rr
