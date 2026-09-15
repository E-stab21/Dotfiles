#include "toast.h"

#include <LayerShellQt/Window>

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QAbstractAnimation>
#include <QApplication>
#include <QDateTime>
#include <QEnterEvent>
#include <QEvent>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWindow>

namespace {

void clearTransparent(QWidget *w)
{
    QPainter p(w);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(w->rect(), Qt::transparent);
}

void paintFloatingToast(QWidget *w, qreal radius, qreal opacity)
{
    QPainter p(w);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(w->rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(opacity);

    const QRectF r = QRectF(w->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.fillPath(path, QColor(0x1c, 0x1c, 0x1c, 140));
    p.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
    p.drawPath(path);
}

class ToastChrome : public QWidget
{
public:
    explicit ToastChrome(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        setProperty("toastOpacity", 1.0);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qreal opacity = property("toastOpacity").toReal();
        paintFloatingToast(this, 16.0, opacity > 0.0 ? opacity : 1.0);
    }
};

int urgencyOf(const QVariantMap &hints)
{
    const QVariant u = hints.value(QStringLiteral("urgency"));
    if (!u.isValid())
        return 1;
    bool ok = false;
    const int v = u.toInt(&ok);
    return ok ? qBound(0, v, 2) : 1;
}

constexpr int kToastW = 340;
constexpr int kMaxToasts = 4;
constexpr int kGap = 8;
constexpr int kMargin = 16;
constexpr int kSlideMs = 300;
constexpr int kFadeMs = 380;
constexpr int kAppearSlide = 28;

} // namespace

// ---------------------------------------------------------------------------
// D-Bus adaptor
// ---------------------------------------------------------------------------

class NotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    explicit NotificationsAdaptor(ToastService *service)
        : QDBusAbstractAdaptor(service)
        , m_service(service)
    {
    }

public slots:
    QStringList GetCapabilities()
    {
        return {QStringLiteral("body"), QStringLiteral("body-markup"),
                QStringLiteral("icon-static"), QStringLiteral("actions")};
    }

    QString GetServerInformation(QString &vendor, QString &version, QString &specVersion)
    {
        vendor = QStringLiteral("dotfiles");
        version = QStringLiteral("0.1");
        specVersion = QStringLiteral("1.2");
        return QStringLiteral("hypr-pills");
    }

    uint Notify(const QString &appName, uint replacesId, const QString &appIcon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int expireTimeout)
    {
        return m_service->notify(appName, replacesId, appIcon, summary, body, actions, hints,
                                 expireTimeout);
    }

    void CloseNotification(uint id) { m_service->closeNotification(id, 3); }

signals:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &actionKey);

private:
    ToastService *m_service = nullptr;
};

// ---------------------------------------------------------------------------
// ToastService
// ---------------------------------------------------------------------------

ToastService::ToastService(QObject *parent)
    : QObject(parent)
{
    m_adaptor = new NotificationsAdaptor(this);
    m_host = new ToastHost(this);
    connect(this, &ToastService::notificationClosed, m_adaptor,
            &NotificationsAdaptor::NotificationClosed);
    connect(this, &ToastService::actionInvoked, m_adaptor, &NotificationsAdaptor::ActionInvoked);
}

bool ToastService::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    if (!bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), this,
                            QDBusConnection::ExportAdaptors))
        return false;

    if (!bus.registerService(QStringLiteral("org.freedesktop.Notifications")))
        return false;

    return true;
}

QString ToastService::stripMarkup(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    return text.trimmed();
}

int ToastService::defaultExpireMs(const QVariantMap &hints, int expireTimeout) const
{
    if (expireTimeout == 0)
        return -1;
    if (expireTimeout > 0)
        return expireTimeout;

    switch (urgencyOf(hints)) {
    case 0:
        return 4000;
    case 2:
        return -1;
    default:
        return 6000;
    }
}

QIcon ToastService::resolveIcon(const QString &appIcon, const QVariantMap &hints) const
{
    const auto tryPath = [](QString path) -> QIcon {
        if (path.startsWith(QStringLiteral("file:")))
            path = QUrl(path).toLocalFile();
        if (!path.isEmpty() && QFileInfo::exists(path))
            return QIcon(path);
        return {};
    };

    for (const char *key : {"image-path", "image_path"}) {
        const QVariant v = hints.value(QLatin1String(key));
        if (v.typeId() == QMetaType::QString) {
            QIcon icon = tryPath(v.toString());
            if (!icon.isNull())
                return icon;
        }
    }

    if (!appIcon.isEmpty()) {
        QIcon icon = tryPath(appIcon);
        if (!icon.isNull())
            return icon;
        icon = QIcon::fromTheme(appIcon);
        if (!icon.isNull())
            return icon;
    }

    const QString desktop = hints.value(QStringLiteral("desktop-entry")).toString();
    if (!desktop.isEmpty()) {
        QIcon icon = QIcon::fromTheme(desktop);
        if (!icon.isNull())
            return icon;
    }

    return QIcon::fromTheme(QStringLiteral("dialog-information"));
}

ToastData ToastService::buildData(uint id, const QString &appName, const QString &appIcon,
                                  const QString &summary, const QString &body,
                                  const QStringList &actions, const QVariantMap &hints,
                                  int expireTimeout) const
{
    ToastData d;
    d.id = id;
    d.appName = appName.trimmed();
    d.appIcon = appIcon;
    d.summary = stripMarkup(summary);
    d.body = stripMarkup(body);
    d.actions = actions;
    d.hints = hints;
    d.expireMs = defaultExpireMs(hints, expireTimeout);
    d.icon = resolveIcon(appIcon, hints);
    return d;
}

uint ToastService::notify(const QString &appName, uint replacesId, const QString &appIcon,
                          const QString &summary, const QString &body, const QStringList &actions,
                          const QVariantMap &hints, int expireTimeout)
{
    uint id = replacesId;
    if (id == 0)
        id = m_nextId++;
    else
        m_nextId = qMax(m_nextId, id + 1);

    const ToastData data =
        buildData(id, appName, appIcon, summary, body, actions, hints, expireTimeout);
    m_host->upsert(data);
    return id;
}

void ToastService::closeNotification(uint id, uint reason)
{
    m_host->remove(id, reason);
}

// ---------------------------------------------------------------------------
// ToastCard — own layer surface
// ---------------------------------------------------------------------------

ToastService::ToastCard::ToastCard(const ToastData &data, QScreen *screen, QObject *guardParent)
    : QWidget(nullptr, Qt::FramelessWindowHint)
    , m_id(data.id)
    , m_screen(screen)
{
    Q_UNUSED(guardParent);

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAutoFillBackground(false);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::Tool);
    setCursor(Qt::PointingHandCursor);
    setFixedWidth(kToastW);

    // Content lives on chrome so we can slide it client-side. Layer margins jump
    // to the target slot immediately — animating margins only applied on hover.
    m_chrome = new ToastChrome(this);
    m_chrome->setFixedWidth(kToastW);

    auto *root = new QHBoxLayout(m_chrome);
    root->setContentsMargins(14, 12, 12, 12);
    root->setSpacing(12);

    m_icon = new QLabel(m_chrome);
    m_icon->setFixedSize(28, 28);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(28, 28, 28, 180); border: 1px solid rgba(70, 70, 70, 200);"
        " border-radius: 9px; }"));

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);

    m_app = new QLabel(m_chrome);
    m_app->setStyleSheet(
        QStringLiteral("color: rgba(255, 255, 255, 160); font-size: 11px; font-weight: 600;"
                       " letter-spacing: 0.4px;"));

    m_summary = new QLabel(m_chrome);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(
        QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 600;"));

    m_body = new QLabel(m_chrome);
    m_body->setWordWrap(true);
    m_body->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 200); font-size: 12px;"));

    textCol->addWidget(m_app);
    textCol->addWidget(m_summary);
    textCol->addWidget(m_body);
    textCol->addStretch(1);

    m_close = new QToolButton(m_chrome);
    m_close->setText(QStringLiteral("✕"));
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setFixedSize(22, 22);
    m_close->setAutoRaise(true);
    m_close->setFocusPolicy(Qt::NoFocus);
    m_close->setStyleSheet(QStringLiteral(
        "QToolButton { color: rgba(255, 255, 255, 140); background: transparent; border: none;"
        " font-size: 11px; border-radius: 6px; }"
        "QToolButton:hover { color: #ffffff; background: rgba(255, 255, 255, 28); }"));
    connect(m_close, &QToolButton::clicked, this, [this]() { emit dismissRequested(m_id); });

    root->addWidget(m_icon, 0, Qt::AlignTop);
    root->addLayout(textCol, 1);
    root->addWidget(m_close, 0, Qt::AlignTop);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() { emit expired(m_id); });

    m_slide = new QVariantAnimation(this);
    m_slide->setDuration(kSlideMs);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_chrome->move(0, v.toInt());
    });
    connect(m_slide, &QVariantAnimation::finished, this, [this]() {
        m_chrome->move(0, 0);
    });

    apply(data);
}

void ToastService::ToastCard::apply(const ToastData &data)
{
    m_data = data;
    m_id = data.id;
    m_expireMs = data.expireMs;
    m_neverExpires = data.expireMs < 0;
    refreshUi();
    armTimer();
}

int ToastService::ToastCard::contentHeight() const
{
    return qMax(56, m_chrome ? m_chrome->sizeHint().height() : sizeHint().height());
}

void ToastService::ToastCard::refreshUi()
{
    const QPixmap px = m_data.icon.pixmap(QSize(18, 18));
    if (!px.isNull()) {
        m_icon->setPixmap(px);
        m_icon->setText(QString());
    } else {
        m_icon->setPixmap(QPixmap());
        m_icon->setText(QStringLiteral("◆"));
        m_icon->setStyleSheet(QStringLiteral(
            "QLabel { background: rgba(28, 28, 28, 180); border: 1px solid rgba(70, 70, 70, 200);"
            " border-radius: 9px; color: #e0563b; font-size: 11px; }"));
    }

    const QString app =
        m_data.appName.isEmpty() ? QStringLiteral("SYSTEM") : m_data.appName.toUpper();
    m_app->setText(app);
    m_summary->setText(m_data.summary);
    m_body->setText(m_data.body);
    m_body->setVisible(!m_data.body.isEmpty());

    m_chrome->adjustSize();
    const int h = contentHeight();
    m_chrome->setFixedSize(kToastW, h);
    setFixedSize(kToastW, h);
    if (m_layerReady)
        applyLayerGeometry();
}

void ToastService::ToastCard::setFadeOpacity(qreal opacity)
{
    m_fadeOpacity = qBound(0.0, opacity, 1.0);
    m_chrome->setProperty("toastOpacity", m_fadeOpacity);
    m_chrome->update();

    const auto applyFx = [this](QWidget *w) {
        if (!w)
            return;
        auto *fx = qobject_cast<QGraphicsOpacityEffect *>(w->graphicsEffect());
        if (!fx) {
            fx = new QGraphicsOpacityEffect(w);
            w->setGraphicsEffect(fx);
        }
        fx->setOpacity(m_fadeOpacity);
    };
    applyFx(m_icon);
    applyFx(m_app);
    applyFx(m_summary);
    applyFx(m_body);
    applyFx(m_close);
}

void ToastService::ToastCard::setScreen(QScreen *screen)
{
    m_screen = screen;
    if (m_layerReady)
        applyLayerGeometry();
}

void ToastService::ToastCard::animateChromeTo(int toY)
{
    m_slide->stop();
    m_slide->setStartValue(m_chrome->y());
    m_slide->setEndValue(toY);
    m_slide->start();
}

void ToastService::ToastCard::moveToBottom(int bottom, bool animate)
{
    ensureLayerShell();

    const int oldBottom = m_bottom;
    const bool wasVisible = isVisible();
    m_bottom = bottom;
    applyLayerGeometry();

    if (!animate) {
        m_slide->stop();
        m_chrome->move(0, 0);
        return;
    }

    if (!wasVisible) {
        // Rise into the already-placed slot.
        m_chrome->move(0, kAppearSlide);
        if (!isVisible()) {
            show();
            raise();
        }
        animateChromeTo(0);
        return;
    }

    // Margin already jumped to the new slot. Offset chrome so the card appears
    // where it was, then ease it into place (client-side = every frame paints).
    const int jump = bottom - oldBottom;
    m_chrome->move(0, jump);
    animateChromeTo(0);
}

void ToastService::ToastCard::ensureLayerShell()
{
    createWinId();
    QWindow *win = windowHandle();
    if (!win)
        return;

    auto *layer = LayerShellQt::Window::get(win);
    if (!layer)
        return;

    layer->setLayer(LayerShellQt::Window::LayerTop);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom)
                      | LayerShellQt::Window::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setScope(QStringLiteral("hypr-pills-toast"));
    layer->setCloseOnDismissed(false);
    layer->setActivateOnShow(false);
    m_layerReady = true;
    applyLayerGeometry();
}

void ToastService::ToastCard::applyLayerGeometry()
{
    if (QWindow *win = windowHandle()) {
        if (auto *layer = LayerShellQt::Window::get(win)) {
            layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom)
                              | LayerShellQt::Window::AnchorRight);
            layer->setDesiredSize(QSize(width(), height()));
            layer->setMargins(QMargins(0, 0, kMargin, m_bottom));
            if (m_screen) {
                layer->setWantsToBeOnActiveScreen(false);
                layer->setScreen(m_screen);
            }
        }
    }
}

void ToastService::ToastCard::armTimer()
{
    m_timer->stop();
    if (m_neverExpires) {
        m_deadlineMs = 0;
        return;
    }
    m_deadlineMs = QDateTime::currentMSecsSinceEpoch() + m_expireMs;
    m_timer->start(m_expireMs);
}

void ToastService::ToastCard::pauseExpiry()
{
    if (!m_timer->isActive())
        return;
    const qint64 left = m_deadlineMs - QDateTime::currentMSecsSinceEpoch();
    m_timer->stop();
    m_expireMs = static_cast<int>(qMax<qint64>(300, left));
}

void ToastService::ToastCard::resumeExpiry()
{
    if (m_neverExpires || m_deadlineMs == 0)
        return;
    armTimer();
}

void ToastService::ToastCard::paintEvent(QPaintEvent *)
{
    // Host surface stays clear; chrome paints the matte while sliding.
    clearTransparent(this);
}

void ToastService::ToastCard::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    pauseExpiry();
}

void ToastService::ToastCard::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    resumeExpiry();
}

void ToastService::ToastCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Map into chrome coords — close button lives on the chrome.
        const QPoint chromePos = m_chrome->mapFrom(this, event->pos());
        if (!m_close->geometry().contains(chromePos))
            emit activated(m_id);
    }
    QWidget::mousePressEvent(event);
}

void ToastService::ToastCard::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureLayerShell();
}

// ---------------------------------------------------------------------------
// ToastHost — stack manager
// ---------------------------------------------------------------------------

ToastService::ToastHost::ToastHost(ToastService *service)
    : QObject(service)
    , m_service(service)
{
}

QScreen *ToastService::ToastHost::pickScreen() const
{
    if (QScreen *s = QGuiApplication::screenAt(QCursor::pos()))
        return s;
    return QGuiApplication::primaryScreen();
}

QVector<int> ToastService::ToastHost::targetBottoms() const
{
    // Newest sits on the bottom inset; each older toast stacks above it.
    QVector<int> bottoms(m_order.size(), kMargin);
    int bottom = kMargin;
    for (int i = m_order.size() - 1; i >= 0; --i) {
        bottoms[i] = bottom;
        if (ToastCard *card = m_cards.value(m_order.at(i)))
            bottom += card->contentHeight() + kGap;
    }
    return bottoms;
}

void ToastService::ToastHost::upsert(const ToastData &data)
{
    if (ToastCard *existing = m_cards.value(data.id)) {
        if (m_fading.contains(data.id))
            return;
        existing->apply(data);
        relayout();
        return;
    }

    while (m_order.size() >= kMaxToasts) {
        const uint oldest = m_order.first();
        remove(oldest, 1);
    }

    const bool stackLive = !m_order.isEmpty();
    QScreen *screen = pickScreen();

    auto *card = new ToastCard(data, screen, this);
    connect(card, &ToastCard::dismissRequested, this, [this](uint id) { remove(id, 2); });
    connect(card, &ToastCard::expired, this, [this](uint id) { remove(id, 1); });
    connect(card, &ToastCard::activated, this, [this](uint id) {
        emit m_service->actionInvoked(id, QStringLiteral("default"));
        remove(id, 2);
    });

    m_cards.insert(data.id, card);
    m_order.append(data.id);
    relayout(stackLive ? card : nullptr);
}

void ToastService::ToastHost::remove(uint id, uint reason)
{
    ToastCard *card = m_cards.value(id);
    if (!card || m_fading.contains(id))
        return;

    m_fading.insert(id);

    auto *fade = new QVariantAnimation(card);
    fade->setDuration(kFadeMs);
    fade->setEasingCurve(QEasingCurve::InOutCubic);
    fade->setStartValue(card->fadeOpacity());
    fade->setEndValue(0.0);
    connect(fade, &QVariantAnimation::valueChanged, card, [card](const QVariant &v) {
        card->setFadeOpacity(v.toReal());
    });
    connect(fade, &QVariantAnimation::finished, this, [this, id, reason, card]() {
        m_fading.remove(id);
        m_cards.remove(id);
        m_order.removeAll(id);
        card->hide();
        card->deleteLater();
        emit m_service->notificationClosed(id, reason);
        relayout();
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastService::ToastHost::relayout(ToastCard *slideIn)
{
    if (m_order.isEmpty())
        return;

    QScreen *screen = pickScreen();
    const QVector<int> bottoms = targetBottoms();

    for (int i = 0; i < m_order.size(); ++i) {
        const uint id = m_order.at(i);
        ToastCard *card = m_cards.value(id);
        if (!card || m_fading.contains(id))
            continue;

        card->setScreen(screen);
        const int toBottom = bottoms.value(i, kMargin);
        const bool isNew = (card == slideIn) || !card->isVisible();
        card->moveToBottom(toBottom, /*animate=*/true);

        // moveToBottom shows new cards; keep them raised above older ones.
        if (isNew || card->isVisible())
            card->raise();
    }
}

#include "toast.moc"
