#include "about.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>

#ifndef RR_VERSION
#define RR_VERSION "1.0.0"
#endif

namespace rr {

void addAboutMenu(QMainWindow *window, const QString &appName,
                  const QString &iconResource, const QString &summary)
{
    if (!window) return;

    QMenu *menu = window->menuBar()->addMenu(
        QCoreApplication::translate("About", "&Help"));
    QAction *manual = menu->addAction(
        QCoreApplication::translate("About", "&User manual"));
    manual->setShortcut(QKeySequence::HelpContents);
    QObject::connect(manual, &QAction::triggered, window,
                     [window] { openUserManual(window); });
    menu->addSeparator();

    QAction *action = menu->addAction(
        QCoreApplication::translate("About", "&About %1").arg(appName));
    // Sans ce role, macOS et certains bureaux deplacent l'entree ailleurs.
    action->setMenuRole(QAction::AboutRole);

    QObject::connect(action, &QAction::triggered, window,
                     [window, appName, iconResource, summary] {
        showAboutDialog(window, appName, iconResource, summary);
    });
}

void showAboutDialog(QWidget *parent, const QString &appName,
                     const QString &iconResource, const QString &summary)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("About", "About %1").arg(appName));

    auto *outer = new QVBoxLayout(&dialog);
    auto *top = new QHBoxLayout;

    auto *logo = new QLabel;
    const QPixmap pix(iconResource);
    if (!pix.isNull())
        logo->setPixmap(pix.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedWidth(108);
    logo->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    top->addWidget(logo);

    auto *text = new QVBoxLayout;

    auto *title = new QLabel(appName);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 6);
    titleFont.setBold(true);
    title->setFont(titleFont);
    text->addWidget(title);

    text->addWidget(new QLabel(
        QCoreApplication::translate("About", "Version %1").arg(QStringLiteral(RR_VERSION))));

    auto *licence = new QLabel(QCoreApplication::translate(
        "About", "MIT licence — free to use, modify and redistribute."));
    licence->setWordWrap(true);
    text->addWidget(licence);

    text->addSpacing(8);

    auto *body = new QLabel(summary);
    body->setWordWrap(true);
    body->setMinimumWidth(360);
    text->addWidget(body);

    text->addStretch();
    top->addLayout(text, 1);
    outer->addLayout(top);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    outer->addWidget(buttons);

    dialog.exec();
}

void openUserManual(QWidget *parent)
{
    // La langue du manuel suit celle de l'interface, elle-meme deja choisie par
    // les traducteurs installes.
    const QString lang =
        QCoreApplication::translate("About", "fr") == QLatin1String("fr")
            ? QStringLiteral("fr") : QStringLiteral("en");
    const QString resource = QStringLiteral(":/docs/manual-%1.html").arg(lang);

    QFile src(resource);
    if (!src.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(parent,
            QCoreApplication::translate("About", "User manual"),
            QCoreApplication::translate("About", "The manual is missing from this build."));
        return;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(dir).filePath(QStringLiteral("remoterig-manual-%1.html").arg(lang));

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || out.write(src.readAll()) < 0) {
        QMessageBox::warning(parent,
            QCoreApplication::translate("About", "User manual"),
            QCoreApplication::translate("About", "Could not write the manual to %1.").arg(path));
        return;
    }
    out.close();

    // Les captures accompagnent le manuel. Elles sont stockees une seule fois
    // dans les ressources et partagees par les deux langues : les inscrire en
    // clair dans chaque page aurait double leur poids dans chaque executable.
    // On les depose a cote du fichier extrait, dans le sous-dossier auquel
    // renvoie le manuel.
    const QDir imgDir(QDir(dir).filePath(QStringLiteral("remoterig-manual-images")));
    QDir().mkpath(imgDir.absolutePath());
    const QStringList shots = QDir(QStringLiteral(":/docs/images")).entryList(QDir::Files);
    for (const QString &name : shots) {
        const QString dest = imgDir.filePath(name);
        // Reecrite a chaque ouverture : une version precedente pourrait trainer.
        QFile::remove(dest);
        QFile::copy(QStringLiteral(":/docs/images/%1").arg(name), dest);
        QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner);
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::information(parent,
            QCoreApplication::translate("About", "User manual"),
            QCoreApplication::translate("About", "No browser could be started. "
                                                 "The manual is at:\n%1").arg(path));
    }
}

} // namespace rr
