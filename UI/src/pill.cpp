#include "pill.h"

#include <QPainter>
#include <QPainterPath>

Pill::Pill(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

void Pill::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, r.height() / 2.0, r.height() / 2.0);

    // Translucent fill so Hyprland layer blur shows through.
    p.fillPath(path, QColor(0x1a, 0x14, 0x10, 110));
    p.setPen(QPen(QColor(0xe6, 0xd6, 0xcb, 40), 1.0));
    p.drawPath(path);
}
