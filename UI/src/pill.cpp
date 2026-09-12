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

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, r.height() / 2.0, r.height() / 2.0);

    // Flat matte fill — no gradient sheen.
    p.fillPath(path, QColor(0x1a, 0x14, 0x10, 138));
    p.setPen(QPen(QColor(0xe6, 0xd6, 0xcb, 32), 1.0));
    p.drawPath(path);
}
