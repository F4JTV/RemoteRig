// Vumetre a maintien de crete et temoin d'ecretage.
//
// Une QProgressBar ne sait montrer qu'une valeur instantanee. Regler un niveau
// micro sur une barre qui saute est approximatif : il faut voir jusqu'ou elle
// est montee, et savoir si elle a touche la butee.
#pragma once

#include <QElapsedTimer>
#include <QWidget>

namespace rr {

class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget *parent = nullptr);

    // level entre 0 et 1 ; clipped signale un echantillon en butee depuis le
    // dernier appel.
    void setLevel(float level, bool clipped);
    void reset();

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize minimumSizeHint() const override;

private:
    float m_level = 0.0f;
    float m_peak  = 0.0f;
    QElapsedTimer m_peakClock;
    QElapsedTimer m_clipClock;
    bool  m_clipLatched = false;
};

} // namespace rr
