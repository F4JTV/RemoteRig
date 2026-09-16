#include "levelmeter.h"

#include <QPainter>
#include <QPaintEvent>

namespace rr {

namespace {
// La crete tient une seconde et demie, le temps de la lire, puis retombe
// doucement pour ne pas rester accrochee a un claquement isole.
constexpr int   kPeakHoldMs = 1500;
constexpr float kPeakFallPerSecond = 0.7f;
// Le temoin d'ecretage reste allume assez longtemps pour etre vu.
constexpr int   kClipHoldMs = 1200;
} // namespace

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
    m_peakClock.start();
    m_clipClock.start();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void LevelMeter::reset()
{
    m_level = m_peak = 0.0f;
    m_clipLatched = false;
    update();
}

void LevelMeter::setLevel(float level, bool clipped)
{
    m_level = qBound(0.0f, level, 1.0f);

    if (m_level >= m_peak) {
        m_peak = m_level;
        m_peakClock.restart();
    } else if (m_peakClock.elapsed() > kPeakHoldMs) {
        const float seconds = float(m_peakClock.restart() - kPeakHoldMs) / 1000.0f;
        m_peak = qMax(m_level, m_peak - kPeakFallPerSecond * qMax(seconds, 0.0f));
        m_peakClock.restart();
    }

    if (clipped) {
        m_clipLatched = true;
        m_clipClock.restart();
    } else if (m_clipLatched && m_clipClock.elapsed() > kClipHoldMs) {
        m_clipLatched = false;
    }
    update();
}

QSize LevelMeter::minimumSizeHint() const { return QSize(80, 16); }

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect r = rect().adjusted(0, 2, -1, -3);
    const int clipW = 14;
    const QRect bar = r.adjusted(0, 0, -clipW - 4, 0);

    p.fillRect(bar, palette().base());
    p.setPen(palette().mid().color());
    p.drawRect(bar);

    // Barre pleine, verte jusqu'a -6 dB puis ambre : la zone haute se voit
    // avant d'etre atteinte.
    const int w = int(bar.width() * m_level);
    if (w > 0) {
        const QRect fill = bar.adjusted(1, 1, 0, -1);
        const int safe = int(fill.width() * 0.5);
        const int warn = int(fill.width() * 0.8);
        p.fillRect(QRect(fill.x(), fill.y(), qMin(w, safe), fill.height()),
                   QColor(60, 150, 70));
        if (w > safe)
            p.fillRect(QRect(fill.x() + safe, fill.y(), qMin(w, warn) - safe, fill.height()),
                       QColor(200, 165, 60));
        if (w > warn)
            p.fillRect(QRect(fill.x() + warn, fill.y(), w - warn, fill.height()),
                       QColor(200, 60, 50));
    }

    // Trait de crete : la position atteinte, meme apres redescente.
    if (m_peak > 0.01f) {
        const int x = bar.x() + 1 + int((bar.width() - 2) * m_peak);
        p.setPen(QPen(palette().text().color(), 2));
        p.drawLine(x, bar.y() + 1, x, bar.bottom() - 1);
    }

    // Temoin d'ecretage, a droite.
    const QRect led(r.right() - clipW + 1, r.y() + 2, clipW - 2, r.height() - 4);
    p.setPen(palette().mid().color());
    p.setBrush(m_clipLatched ? QColor(220, 40, 40) : palette().base());
    p.drawRect(led);
}

} // namespace rr
