#include "cornerbar.h"

#include "pill.h"

#include <LayerShellQt/Window>

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QWindow>

namespace {

constexpr int kBarHeight = 44;
constexpr int kPillHeight = 34;
constexpr int kMarginTop = 4;
constexpr int kPeekH = 4;
constexpr int kRevealMs = 280;
constexpr int kHideDelayMs = 380;
constexpr int kSideMargin = 14;
constexpr int kMinWidth = 160;

} // namespace

CornerBar::CornerBar(Edge edge, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint)
    , m_edge(edge)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    setFixedHeight(kBarHeight);
    setMinimumWidth(kMinWidth);

    m_peek = new QWidget(this);
    m_peek->setGeometry(0, 0, kMinWidth, kPeekH);
    m_peek->setMouseTracking(true);
    m_peek->installEventFilter(this);

    m_pill = new Pill(this);
    m_pill->setFixedHeight(kPillHeight);
    m_pill->setMouseTracking(true);
    m_pill->installEventFilter(this);
    m_pillLayout = new QHBoxLayout(m_pill);
    m_pillLayout->setContentsMargins(12, 0, 12, 0);
    m_pillLayout->setSpacing(edge == Edge::Left ? 12 : 6);

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(kRevealMs);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_reveal = v.toReal();
        applyReveal();
    });
    connect(m_anim, &QVariantAnimation::finished, this, [this]() {
        if (m_reveal >= 0.999)
            emit fullyRevealed();
    });

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(kHideDelayMs);
    connect(m_hideTimer, &QTimer::timeout, this, &CornerBar::hideNow);

    applyReveal();
}

void CornerBar::setHoldOpen(bool hold)
{
    m_holdOpen = hold;
    if (hold) {
        cancelHide();
        reveal();
    } else if (!m_pointerInside) {
        scheduleHide();
    }
}

void CornerBar::reveal()
{
    cancelHide();
    if (m_reveal >= 0.999 && m_anim->state() != QAbstractAnimation::Running)
        return;
    m_anim->stop();
    m_anim->setStartValue(m_reveal);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void CornerBar::scheduleHide()
{
    // Don't trust QWidget::underMouse() after the pointer leaves our layer
    // surface — on Wayland it can stay sticky and block retract forever.
    if (m_holdOpen || m_pointerInside)
        return;
    m_hideTimer->start();
}

void CornerBar::cancelHide()
{
    m_hideTimer->stop();
}

void CornerBar::hideNow()
{
    if (m_holdOpen || m_pointerInside)
        return;
    if (m_reveal <= 0.001 && m_anim->state() != QAbstractAnimation::Running)
        return;
    m_anim->stop();
    m_anim->setStartValue(m_reveal);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

void CornerBar::relayout()
{
    m_pill->setMinimumWidth(0);
    m_pill->setMaximumWidth(QWIDGETSIZE_MAX);
    m_pill->setFixedHeight(kPillHeight);

    m_pillLayout->invalidate();
    m_pillLayout->activate();

    int contentW = m_pillLayout->contentsMargins().left() + m_pillLayout->contentsMargins().right();
    int visibleChildren = 0;
    for (int i = 0; i < m_pillLayout->count(); ++i) {
        QLayoutItem *item = m_pillLayout->itemAt(i);
        QWidget *w = item ? item->widget() : nullptr;
        if (!w || w->isHidden())
            continue;
        if (visibleChildren++ > 0)
            contentW += m_pillLayout->spacing();
        // Prefer the widget's actual width when already fixed (dots host / buttons).
        const int childW = qMax(w->width(),
                                qMax(w->minimumWidth(),
                                     qMax(w->minimumSizeHint().width(), w->sizeHint().width())));
        contentW += childW;
    }
    const int pillW = qMax(1, contentW);
    m_pill->setFixedWidth(pillW);

    const int hostW = qMax(kMinWidth, pillW);
    if (width() != hostW)
        setFixedWidth(hostW);

    if (auto *win = windowHandle()) {
        if (auto *layer = LayerShellQt::Window::get(win))
            layer->setDesiredSize(QSize(hostW, kBarHeight));
    }

    applyReveal();
}

int CornerBar::hiddenY() const
{
    return -(kPillHeight + 6);
}

int CornerBar::shownY() const
{
    return kMarginTop;
}

void CornerBar::applyReveal()
{
    const int y = qRound(hiddenY() + (shownY() - hiddenY()) * m_reveal);
    const int x = (m_edge == Edge::Right) ? qMax(0, width() - m_pill->width()) : 0;
    m_pill->move(x, y);
    m_peek->setGeometry(0, 0, width(), kPeekH);
    if (m_reveal > 0.05)
        m_pill->raise();
    else
        m_peek->raise();
    update();
}

void CornerBar::setupLayerShell()
{
    if (m_layerReady)
        return;

    createWinId();
    QWindow *win = windowHandle();
    if (!win)
        return;

    auto *layer = LayerShellQt::Window::get(win);
    if (!layer)
        return;

    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    if (m_edge == Edge::Left) {
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                          | LayerShellQt::Window::AnchorLeft);
        layer->setMargins(QMargins(kSideMargin, 0, 0, 0));
        layer->setScope(QStringLiteral("hypr-pills-left"));
    } else {
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                          | LayerShellQt::Window::AnchorRight);
        layer->setMargins(QMargins(0, 0, kSideMargin, 0));
        layer->setScope(QStringLiteral("hypr-pills-right"));
    }
    layer->setExclusiveZone(0);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setDesiredSize(QSize(width(), kBarHeight));
    layer->setCloseOnDismissed(false);
    layer->setActivateOnShow(false);
    m_layerReady = true;
}

void CornerBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setupLayerShell();
    relayout();
}

void CornerBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyReveal();
}

void CornerBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
}

void CornerBar::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    m_pointerInside = true;
    reveal();
    emit hoverEntered();
}

void CornerBar::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_pointerInside = false;
    scheduleHide();
    emit hoverLeft();
}

bool CornerBar::eventFilter(QObject *watched, QEvent *event)
{
    // Keep the bar revealed while moving between peek / pill / buttons.
    if (watched == m_peek || watched == m_pill
        || m_pill->isAncestorOf(qobject_cast<QWidget *>(watched))) {
        if (event->type() == QEvent::Enter) {
            m_pointerInside = true;
            reveal();
            return false;
        }
    }
    return QWidget::eventFilter(watched, event);
}
