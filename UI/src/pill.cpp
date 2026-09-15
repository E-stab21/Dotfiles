#include "pill.h"

#include <QPainter>
#include <QPainterPath>

namespace {

QPainterPath roundedRectPath(const QRectF &r, qreal tl, qreal tr, qreal br, qreal bl)
{
    QPainterPath path;
    path.moveTo(r.left() + tl, r.top());
    path.lineTo(r.right() - tr, r.top());
    if (tr > 0)
        path.quadTo(r.right(), r.top(), r.right(), r.top() + tr);
    else
        path.lineTo(r.right(), r.top());
    path.lineTo(r.right(), r.bottom() - br);
    if (br > 0)
        path.quadTo(r.right(), r.bottom(), r.right() - br, r.bottom());
    else
        path.lineTo(r.right(), r.bottom());
    path.lineTo(r.left() + bl, r.bottom());
    if (bl > 0)
        path.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - bl);
    else
        path.lineTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + tl);
    if (tl > 0)
        path.quadTo(r.left(), r.top(), r.left() + tl, r.top());
    else
        path.lineTo(r.left(), r.top());
    path.closeSubpath();
    return path;
}

} // namespace

Pill::Pill(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

void Pill::setFlushSide(FlushSide side)
{
    if (m_flush == side)
        return;
    m_flush = side;
    update();
}

void Pill::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Tab shape: square top (flush with screen), rounded bottom corners.
    QRectF r = QRectF(rect()).adjusted(0.5, 0.0, -0.5, -0.5);
    const qreal rad = height() / 2.0;
    qreal tl = 0.0, tr = 0.0, br = rad, bl = rad;
    if (m_flush == FlushSide::Left) {
        // Left tab: also square the wall-facing left edge.
        bl = 0.0;
        r.adjust(-0.5, 0.0, 0.0, 0.0);
    } else if (m_flush == FlushSide::Right) {
        br = 0.0;
        r.adjust(0.0, 0.0, 0.5, 0.0);
    }

    const QPainterPath path = roundedRectPath(r, tl, tr, br, bl);

    // Translucent fill so Hyprland layer blur shows through.
    p.fillPath(path, QColor(0x1a, 0x14, 0x10, 110));
    p.setPen(QPen(QColor(0xe6, 0xd6, 0xcb, 40), 1.0));
    p.drawPath(path);
}
