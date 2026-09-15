// Demarrage et arret du service de premier plan Android.
// Sans service, l'audio se coupe des que l'ecran s'eteint.
#pragma once

namespace rr {
void startAndroidService();
void stopAndroidService();

// Hauteur de la barre d'etat en pixels physiques, 0 hors Android.
// Depuis Android 15 la fenetre s'etend sous la barre systeme : sans cette
// marge, l'en-tete de l'application passe dessous.
int androidStatusBarHeight();

// L'activite Java appelle ces deux fonctions depuis le fil d'interface
// d'Android. La cible est prevenue par une connexion en file d'attente.
class VolumePttSink {
public:
    virtual ~VolumePttSink() = default;
    virtual bool volumePttWanted() const = 0;
    virtual void volumePttChanged(bool pressed) = 0;
};
void setVolumePttSink(VolumePttSink *sink);
}
