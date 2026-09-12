#include "dropdown.h"

#include <LayerShellQt/Window>

#include <QAbstractButton>
#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QShowEvent>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <utility>

namespace {

QColor cream(int a = 255)
{
    return QColor(0xe6, 0xd6, 0xcb, a);
}

void paintMatte(QWidget *w, qreal radius)
{
    QPainter p(w);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Punch a fully transparent window first so corners aren't opaque.
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(w->rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRectF r = QRectF(w->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    p.fillPath(path, QColor(0x1a, 0x14, 0x10, 150));
    p.setPen(QPen(cream(36), 1.0));
    p.drawPath(path);
}

class MatteChrome : public QWidget
{
public:
    explicit MatteChrome(qreal radius, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_radius(radius)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override { paintMatte(this, m_radius); }

private:
    qreal m_radius = 18.0;
};

const char *kFieldStyle = R"(
QLineEdit {
  color: #ffffff;
  background: rgba(0,0,0,70);
  border: 1px solid rgba(255,255,255,40);
  border-radius: 10px;
  padding: 10px 12px;
  font-size: 13px;
  selection-background-color: rgba(255,255,255,70);
}
QLineEdit:focus { border: 1px solid rgba(255,255,255,90); }
)";

const char *kBtnStyle = R"(
QToolButton {
  color: #ffffff;
  background: rgba(255,255,255,18);
  border: 1px solid rgba(255,255,255,28);
  border-radius: 10px;
  padding: 8px 14px;
  font-size: 12px;
}
QToolButton:hover { background: rgba(255,255,255,28); }
QToolButton#primary {
  color: #ffffff;
  background: rgba(255,255,255,40);
  border-color: rgba(255,255,255,60);
}
QToolButton#primary:hover { background: rgba(255,255,255,55); }
)";

enum class MenuAlign {
    UnderAnchor, // left-aligned to the hovered button
    PillRight,   // flush with the right edge of the pill
    PillLeft,    // flush with the left edge of the pill
};

QPoint menuPos(QWidget *anchor, int menuWidth, MenuAlign align)
{
    QWidget *host = anchor->window();
    QWidget *band = anchor;
    while (band->parentWidget() && band->parentWidget() != host)
        band = band->parentWidget();

    const QPoint bandTL = band->mapTo(host, QPoint(0, 0));
    const QPoint anchorTL = anchor->mapTo(host, QPoint(0, 0));
    const int bandRight = bandTL.x() + band->width();
    const int bandBottom = bandTL.y() + band->height();

    int x = 0;
    switch (align) {
    case MenuAlign::UnderAnchor:
        // Pin to the button itself (not the pill's right edge).
        x = anchorTL.x() + (anchor->width() - menuWidth) / 2;
        break;
    case MenuAlign::PillRight:
        x = bandRight - menuWidth;
        break;
    case MenuAlign::PillLeft:
        x = bandTL.x();
        break;
    }

    if (QScreen *screen = host->screen() ? host->screen() : QApplication::screenAt(QCursor::pos()))
        x = qBound(8, x, screen->geometry().width() - menuWidth - 8);

    // Small clear gap under the pill (exclusive zone ignored via -1).
    const int y = bandBottom + 6;
    return QPoint(qMax(0, x), qMax(0, y));
}

void clearTransparent(QWidget *w)
{
    QPainter p(w);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(w->rect(), Qt::transparent);
}

void setupOverlayLayer(QWindow *win, const QString &scope, QScreen *screen)
{
    auto *layer = LayerShellQt::Window::get(win);
    if (!layer)
        return;
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                      | LayerShellQt::Window::AnchorLeft);
    // -1 = ignore other surfaces' exclusive zones (otherwise Hyprland pushes
    // the menu below the bar and leaves a large gap under the pills).
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setScope(scope);
    layer->setCloseOnDismissed(false);
    layer->setActivateOnShow(false);
    if (screen) {
        layer->setWantsToBeOnActiveScreen(false);
        layer->setScreen(screen);
    }
}

void placeOverlay(QWindow *win, const QPoint &topLeft, const QSize &size)
{
    auto *layer = LayerShellQt::Window::get(win);
    if (!layer)
        return;
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                      | LayerShellQt::Window::AnchorLeft);
    layer->setDesiredSize(size);
    layer->setMargins(QMargins(topLeft.x(), topLeft.y(), 0, 0));
}

void placeOverlayFromRight(QWindow *win, int top, int rightMargin, const QSize &size)
{
    auto *layer = LayerShellQt::Window::get(win);
    if (!layer)
        return;
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                      | LayerShellQt::Window::AnchorRight);
    layer->setDesiredSize(size);
    layer->setMargins(QMargins(0, top, rightMargin, 0));
}

// Subsequence fuzzy score; -1 = no match. Higher is better.
int fuzzyScore(const QString &haystack, const QString &needle)
{
    if (needle.isEmpty())
        return 0;

    const QString h = haystack.toCaseFolded();
    const QString n = needle.toCaseFolded();
    int score = 0;
    int hi = 0;
    int consecutive = 0;

    for (QChar nc : n) {
        bool found = false;
        for (; hi < h.size(); ++hi) {
            if (h.at(hi) != nc) {
                consecutive = 0;
                continue;
            }
            score += 10 + consecutive * 8;
            if (hi == 0) {
                score += 20;
            } else {
                const QChar prev = h.at(hi - 1);
                if (prev.isSpace() || prev == QLatin1Char('-') || prev == QLatin1Char('_')
                    || prev == QLatin1Char('.') || prev == QLatin1Char('/'))
                    score += 15;
            }
            ++consecutive;
            ++hi;
            found = true;
            break;
        }
        if (!found)
            return -1;
    }

    if (h.startsWith(n))
        score += 40;
    score -= h.size() / 3;
    return score;
}

} // namespace

DropMenu::DropMenu(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFixedWidth(520);

    m_chrome = new MatteChrome(18.0, this);

    auto *root = new QVBoxLayout(m_chrome);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    m_header = new QWidget(m_chrome);
    auto *headerLay = new QHBoxLayout(m_header);
    headerLay->setContentsMargins(2, 0, 2, 0);
    headerLay->setSpacing(8);

    m_title = new QLabel(m_header);
    m_title->setStyleSheet(
        QStringLiteral("color: #ffffff; font-weight: 600; font-size: 14px;"));

    m_toggle = new QToolButton(m_header);
    m_toggle->setText(QStringLiteral("Off"));
    m_toggle->setCheckable(true);
    m_toggle->setCursor(Qt::PointingHandCursor);
    m_toggle->setStyleSheet(QStringLiteral(
        "QToolButton { color: #ffffff; background: rgba(255,255,255,18);"
        " border: 1px solid rgba(255,255,255,28); border-radius: 11px; padding: 4px 12px;"
        " font-size: 12px; }"
        "QToolButton:checked { color: #ffffff; background: rgba(255,255,255,40);"
        " border-color: rgba(255,255,255,60); }"));
    connect(m_toggle, &QToolButton::clicked, this, [this](bool on) {
        m_toggle->setText(on ? QStringLiteral("On") : QStringLiteral("Off"));
        emit toggleRequested(on);
    });

    m_refresh = new QToolButton(m_header);
    m_refresh->setText(QStringLiteral("↻"));
    m_refresh->setCursor(Qt::PointingHandCursor);
    m_refresh->setToolTip(QStringLiteral("Rescan"));
    m_refresh->setStyleSheet(QStringLiteral(
        "QToolButton { color: #ffffff; background: transparent; border: none;"
        " font-size: 16px; padding: 2px 6px; }"
        "QToolButton:hover { color: #ffffff; background: rgba(255,255,255,20); border-radius: 8px; }"));
    connect(m_refresh, &QToolButton::clicked, this, &DropMenu::refreshRequested);

    headerLay->addWidget(m_title);
    headerLay->addStretch(1);
    headerLay->addWidget(m_toggle);
    headerLay->addWidget(m_refresh);

    m_status = new QLabel(m_chrome);
    m_status->setStyleSheet(QStringLiteral("color: rgba(255,255,255,180); font-size: 12px;"));
    m_status->setWordWrap(true);

    m_search = new QLineEdit(m_chrome);
    m_search->setVisible(false);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(QStringLiteral("Search apps…"));
    m_search->setStyleSheet(QLatin1String(kFieldStyle));
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) {
        applySearchFilter();
    });
    connect(m_search, &QLineEdit::returnPressed, this, &DropMenu::activateCurrentItem);

    m_list = new QListWidget(m_chrome);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setMouseTracking(true);
    m_list->setTextElideMode(Qt::ElideNone);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(false);
    m_list->setSpacing(2);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; color: #ffffff; border: none;"
        " outline: none; font-size: 13px; }"
        "QListWidget::item { padding: 0px; margin: 2px 0; border-radius: 10px; }"
        "QListWidget::item:hover { background: rgba(255,255,255,24); }"
        "QListWidget::item:selected { background: rgba(255,255,255,36); }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,55); border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item || passwordPromptVisible())
            return;
        emit hoverEntered(); // keep menu open while interacting
        emit itemActivated(item->data(Qt::UserRole).toString());
    });

    m_passPanel = new QWidget(m_chrome);
    m_passPanel->setVisible(false);
    auto *passLay = new QVBoxLayout(m_passPanel);
    passLay->setContentsMargins(8, 12, 8, 12);
    passLay->setSpacing(12);

    m_passLabel = new QLabel(m_passPanel);
    m_passLabel->setWordWrap(true);
    m_passLabel->setStyleSheet(
        QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 500;"));

    m_userEdit = new QLineEdit(m_passPanel);
    m_userEdit->setVisible(false);
    m_userEdit->setPlaceholderText(QStringLiteral("Username"));
    m_userEdit->setStyleSheet(QLatin1String(kFieldStyle));
    m_userEdit->setClearButtonEnabled(true);
    connect(m_userEdit, &QLineEdit::returnPressed, this, [this]() {
        m_passEdit->setFocus(Qt::OtherFocusReason);
    });

    m_passEdit = new QLineEdit(m_passPanel);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setPlaceholderText(QStringLiteral("Password"));
    m_passEdit->setStyleSheet(QLatin1String(kFieldStyle));
    m_passEdit->setClearButtonEnabled(true);
    connect(m_passEdit, &QLineEdit::returnPressed, this, &DropMenu::submitPassword);

    auto *passBtns = new QWidget(m_passPanel);
    auto *passBtnLay = new QHBoxLayout(passBtns);
    passBtnLay->setContentsMargins(0, 0, 0, 0);
    passBtnLay->setSpacing(8);

    m_passCancel = new QToolButton(passBtns);
    m_passCancel->setText(QStringLiteral("Back"));
    m_passCancel->setCursor(Qt::PointingHandCursor);
    m_passCancel->setStyleSheet(QLatin1String(kBtnStyle));
    connect(m_passCancel, &QToolButton::clicked, this, [this]() {
        hidePasswordPrompt();
        emit passwordCancelled();
    });

    m_passConnect = new QToolButton(passBtns);
    m_passConnect->setObjectName(QStringLiteral("primary"));
    m_passConnect->setText(QStringLiteral("Connect"));
    m_passConnect->setCursor(Qt::PointingHandCursor);
    m_passConnect->setStyleSheet(QLatin1String(kBtnStyle));
    connect(m_passConnect, &QToolButton::clicked, this, &DropMenu::submitPassword);

    passBtnLay->addWidget(m_passCancel);
    passBtnLay->addStretch(1);
    passBtnLay->addWidget(m_passConnect);

    passLay->addStretch(1);
    passLay->addWidget(m_passLabel);
    passLay->addWidget(m_userEdit);
    passLay->addWidget(m_passEdit);
    passLay->addWidget(passBtns);
    passLay->addStretch(2);

    root->addWidget(m_header);
    root->addWidget(m_status);
    root->addWidget(m_search);
    root->addWidget(m_list, 1);
    root->addWidget(m_passPanel, 1);

    m_slide = new QVariantAnimation(this);
    m_slide->setDuration(480);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_chrome->move(0, v.toInt());
        update();
    });

    qApp->installEventFilter(this);
}

void DropMenu::setTitle(const QString &title) { m_title->setText(title); }

void DropMenu::setStatusText(const QString &text)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty() && !passwordPromptVisible());
}

void DropMenu::clearItems()
{
    m_allItems.clear();
    if (!passwordPromptVisible())
        m_list->clear();
}

void DropMenu::appendRow(const StoredItem &data)
{
    auto *item = new QListWidgetItem(m_list);
    item->setData(Qt::UserRole, data.id);
    item->setText(QString());

    constexpr int kRowW = 476;
    const bool hasIcon = !data.icon.isNull();
    const int textW = kRowW - 24 - (hasIcon ? 36 : 0);

    auto *row = new QWidget(m_list);
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->setFixedWidth(kRowW);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(10);

    if (hasIcon) {
        auto *iconL = new QLabel(row);
        iconL->setFixedSize(28, 28);
        iconL->setAttribute(Qt::WA_TransparentForMouseEvents);
        iconL->setPixmap(data.icon.pixmap(QSize(28, 28)));
        lay->addWidget(iconL, 0, Qt::AlignTop);
    }

    auto *textCol = new QWidget(row);
    textCol->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *textLay = new QVBoxLayout(textCol);
    textLay->setContentsMargins(0, 0, 0, 0);
    textLay->setSpacing(4);

    auto *titleL = new QLabel(data.title, textCol);
    titleL->setWordWrap(true);
    titleL->setFixedWidth(textW);
    titleL->setAttribute(Qt::WA_TransparentForMouseEvents);
    titleL->setTextInteractionFlags(Qt::NoTextInteraction);
    titleL->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 13px; %1")
                              .arg(data.active ? QStringLiteral("font-weight: 700;")
                                               : QStringLiteral("font-weight: 500;")));

    const int titleH = qMax(18, titleL->heightForWidth(textW));
    titleL->setMinimumHeight(titleH);
    textLay->addWidget(titleL);

    int subH = 0;
    if (!data.subtitle.isEmpty()) {
        auto *subL = new QLabel(data.subtitle, textCol);
        subL->setWordWrap(true);
        subL->setFixedWidth(textW);
        subL->setAttribute(Qt::WA_TransparentForMouseEvents);
        subL->setTextInteractionFlags(Qt::NoTextInteraction);
        subL->setStyleSheet(QStringLiteral("color: rgba(255,255,255,170); font-size: 11px;"));
        subH = qMax(14, subL->heightForWidth(textW));
        subL->setMinimumHeight(subH);
        textLay->addWidget(subL);
    }

    lay->addWidget(textCol, 1);

    const int rowH =
        10 + qMax(titleH + (data.subtitle.isEmpty() ? 0 : 4 + subH), hasIcon ? 28 : 0) + 10;
    item->setSizeHint(QSize(kRowW, qMax(rowH, 56)));
    m_list->addItem(item);
    m_list->setItemWidget(item, row);

    if (data.active)
        item->setSelected(true);
}

void DropMenu::addItem(const QString &id, const QString &title, const QString &subtitle, bool active,
                       const QIcon &icon)
{
    if (passwordPromptVisible())
        return;

    m_allItems.push_back(StoredItem{id, title, subtitle, active, icon});
}

void DropMenu::commitItems()
{
    applySearchFilter();
}

void DropMenu::applySearchFilter()
{
    if (passwordPromptVisible())
        return;

    const QString query = m_search->isVisible() ? m_search->text().trimmed() : QString();

    m_rebuildingList = true;
    m_list->clear();

    if (query.isEmpty()) {
        for (const StoredItem &item : m_allItems)
            appendRow(item);
        if (m_search->isVisible())
            setStatusText(QStringLiteral("%1 apps").arg(m_allItems.size()));
    } else {
        QVector<std::pair<int, int>> ranked; // score, index
        ranked.reserve(m_allItems.size());
        for (int i = 0; i < m_allItems.size(); ++i) {
            const StoredItem &item = m_allItems.at(i);
            const int titleScore = fuzzyScore(item.title, query);
            const int hayScore = fuzzyScore(item.title + QLatin1Char(' ') + item.subtitle, query);
            const int score = qMax(titleScore, hayScore);
            if (score >= 0)
                ranked.push_back({score, i});
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &entry : ranked)
            appendRow(m_allItems.at(entry.second));
        if (m_search->isVisible()) {
            setStatusText(ranked.isEmpty() ? QStringLiteral("No matches")
                                           : QStringLiteral("%1 matches").arg(ranked.size()));
        }
    }

    m_rebuildingList = false;

    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
        m_list->scrollToItem(m_list->item(0));
    }
}

void DropMenu::activateCurrentItem()
{
    if (passwordPromptVisible() || m_list->count() == 0)
        return;
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        item = m_list->item(0);
    if (!item)
        return;
    emit itemActivated(item->data(Qt::UserRole).toString());
}

void DropMenu::selectRelative(int delta)
{
    if (m_list->count() == 0)
        return;
    int row = m_list->currentRow();
    if (row < 0)
        row = 0;
    else
        row = qBound(0, row + delta, m_list->count() - 1);
    m_list->setCurrentRow(row);
    m_list->scrollToItem(m_list->item(row));
}

void DropMenu::setKeyboardCapture(bool on)
{
    setAttribute(Qt::WA_ShowWithoutActivating, !on);
    if (auto *win = windowHandle()) {
        if (auto *layer = LayerShellQt::Window::get(win)) {
            layer->setKeyboardInteractivity(
                on ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                   : LayerShellQt::Window::KeyboardInteractivityNone);
        }
    }
    if (on) {
        activateWindow();
        if (m_search->isVisible())
            m_search->setFocus(Qt::OtherFocusReason);
    }
}

void DropMenu::setToggleChecked(bool on)
{
    m_toggle->setChecked(on);
    m_toggle->setText(on ? QStringLiteral("On") : QStringLiteral("Off"));
}

void DropMenu::setToggleVisible(bool visible) { m_toggle->setVisible(visible); }
void DropMenu::setRefreshVisible(bool visible) { m_refresh->setVisible(visible); }
void DropMenu::setHeaderVisible(bool visible) { m_header->setVisible(visible); }

void DropMenu::setSearchVisible(bool visible)
{
    m_search->setVisible(visible);
    if (!visible) {
        m_search->clear();
        setKeyboardCapture(false);
    }
}

void DropMenu::setBusy(bool busy)
{
    if (passwordPromptVisible())
        return;
    m_refresh->setEnabled(!busy);
    m_list->setEnabled(!busy);
    m_search->setEnabled(!busy);
}

void DropMenu::showPasswordPrompt(const QString &networkName, const QString &token)
{
    m_passToken = token;
    const QStringList parts = token.split(QLatin1Char('\n'));
    const QString security = parts.value(2).trimmed();
    const bool openNet = security.isEmpty() || security == QLatin1String("--")
        || security.contains(QLatin1String("open"), Qt::CaseInsensitive);
    const bool enterprise = security.contains(QLatin1String("802.1X"), Qt::CaseInsensitive)
        || security.contains(QLatin1String("Enterprise"), Qt::CaseInsensitive)
        || (security.contains(QLatin1String("EAP"), Qt::CaseInsensitive)
            && !security.contains(QLatin1String("SAE"), Qt::CaseInsensitive));

    if (openNet) {
        m_passLabel->setText(QStringLiteral("Connect to “%1”").arg(networkName));
    } else if (enterprise) {
        m_passLabel->setText(QStringLiteral("Enter credentials for “%1”").arg(networkName));
    } else {
        m_passLabel->setText(QStringLiteral("Enter password for “%1”").arg(networkName));
    }

    m_userEdit->clear();
    m_userEdit->setVisible(enterprise);
    m_userEdit->setEnabled(enterprise);
    m_passEdit->clear();
    m_passEdit->setPlaceholderText(openNet ? QStringLiteral("No password needed")
                                           : QStringLiteral("Password"));
    m_passEdit->setEnabled(!openNet);
    m_passConnect->setEnabled(true);
    m_passConnect->setText(QStringLiteral("Connect"));
    m_passCancel->setEnabled(true);

    // Swap body content only — never resize the layer surface (avoids the
    // white trail from Wayland surface reconfiguration).
    m_status->setVisible(false);
    m_list->setVisible(false);
    m_passPanel->setVisible(true);
    m_refresh->setEnabled(false);
    m_toggle->setEnabled(false);

    const bool needKeys = !openNet;
    if (auto *layer = LayerShellQt::Window::get(windowHandle()))
        layer->setKeyboardInteractivity(needKeys
                                            ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                                            : LayerShellQt::Window::KeyboardInteractivityNone);
    if (needKeys) {
        setAttribute(Qt::WA_ShowWithoutActivating, false);
        activateWindow();
        if (enterprise)
            m_userEdit->setFocus(Qt::OtherFocusReason);
        else
            m_passEdit->setFocus(Qt::OtherFocusReason);
    }
}

void DropMenu::hidePasswordPrompt()
{
    if (!passwordPromptVisible())
        return;
    m_passPanel->setVisible(false);
    m_userEdit->clear();
    m_userEdit->setVisible(false);
    m_userEdit->setEnabled(true);
    m_passEdit->clear();
    m_passEdit->setEnabled(true);
    m_passConnect->setEnabled(true);
    m_passToken.clear();
    m_list->setVisible(true);
    m_refresh->setEnabled(true);
    m_toggle->setEnabled(true);
    if (!m_status->text().isEmpty())
        m_status->setVisible(true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    if (auto *layer = LayerShellQt::Window::get(windowHandle()))
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
}

void DropMenu::setPasswordPromptResult(bool ok, const QString &message)
{
    if (!passwordPromptVisible())
        return;
    m_passLabel->setText(message);
    m_passConnect->setEnabled(!ok);
    m_passCancel->setEnabled(true);
    if (!ok) {
        m_userEdit->setEnabled(m_userEdit->isVisible());
        m_passEdit->setEnabled(true);
        m_passConnect->setEnabled(true);
        if (m_userEdit->isVisible() && m_userEdit->text().trimmed().isEmpty())
            m_userEdit->setFocus(Qt::OtherFocusReason);
        else
            m_passEdit->setFocus(Qt::OtherFocusReason);
    } else {
        m_userEdit->setEnabled(false);
        m_passEdit->setEnabled(false);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        if (auto *layer = LayerShellQt::Window::get(windowHandle()))
            layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    }
}

bool DropMenu::passwordPromptVisible() const { return m_passPanel && m_passPanel->isVisible(); }

int DropMenu::itemCount() const { return m_allItems.size(); }

bool DropMenu::containsGlobalPos(const QPoint &globalPos) const
{
    QPoint origin(0, 0);
    if (m_screen)
        origin = m_screen->geometry().topLeft();
    return QRect(origin + m_targetPos, size()).contains(globalPos);
}

void DropMenu::submitPassword()
{
    if (!passwordPromptVisible())
        return;

    const QString username = m_userEdit->isVisible() ? m_userEdit->text().trimmed() : QString();
    const QString password = m_passEdit->text();
    if (m_userEdit->isVisible() && (username.isEmpty() || password.isEmpty())) {
        m_passLabel->setText(QStringLiteral("Username and password are both required"));
        if (username.isEmpty())
            m_userEdit->setFocus(Qt::OtherFocusReason);
        else
            m_passEdit->setFocus(Qt::OtherFocusReason);
        return;
    }

    m_userEdit->setEnabled(false);
    m_passEdit->setEnabled(false);
    m_passConnect->setEnabled(false);
    m_passLabel->setText(QStringLiteral("Connecting…"));
    emit passwordSubmitted(m_passToken, username, password);
}

void DropMenu::relayout()
{
    // One stable size for list view and password view.
    constexpr int kW = 520;
    constexpr int kBodyH = 380;

    int h = 32; // chrome margins
    if (m_header->isVisible())
        h += 36;
    // Reserve status-line space even when hidden so password view matches.
    h += 28;
    if (m_search->isVisible())
        h += 48;
    h += kBodyH;

    m_list->setFixedHeight(kBodyH);
    m_passPanel->setFixedHeight(kBodyH);

    const QSize sz(kW, h);
    setFixedSize(sz);
    m_chrome->resize(sz);
}

void DropMenu::ensureLayerShell(QScreen *screen)
{
    createWinId();
    if (!windowHandle())
        return;
    setupOverlayLayer(windowHandle(), QStringLiteral("hypr-pills-menu"), screen);
    m_layerReady = true;
}

void DropMenu::placeOnLayer(const QPoint &topLeft, const QSize &size)
{
    ensureLayerShell(m_screen);
    setFixedSize(size);
    m_chrome->resize(size);
    placeOverlay(windowHandle(), topLeft, size);
}

void DropMenu::startSlideIn()
{
    m_slide->stop();
    const int from = -qMin(56, height() / 2);
    m_chrome->move(0, from);
    m_slide->setStartValue(from);
    m_slide->setEndValue(0);
    m_slide->start();
}

void DropMenu::popupBelow(QWidget *anchor, Align align)
{
    if (!anchor)
        return;
    if (m_search->isVisible())
        m_search->clear();
    relayout();
    m_screen = anchor->window() ? anchor->window()->screen() : nullptr;
    m_targetPos = menuPos(anchor, width(),
                          align == Align::Left ? MenuAlign::PillLeft : MenuAlign::PillRight);
    placeOnLayer(m_targetPos, size());
    show();
    raise();
    startSlideIn();
    // Keep keyboard off until the search field is clicked — grabbing focus on
    // hover-open breaks leave events with layer-shell.
    if (m_search->isVisible()) {
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        if (auto *win = windowHandle()) {
            if (auto *layer = LayerShellQt::Window::get(win))
                layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
        }
    }
}

void DropMenu::paintEvent(QPaintEvent *)
{
    clearTransparent(this);
}

void DropMenu::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureLayerShell(m_screen);
}

void DropMenu::hideEvent(QHideEvent *event)
{
    hidePasswordPrompt();
    if (m_search->isVisible()) {
        m_search->clear();
        setKeyboardCapture(false);
    }
    QWidget::hideEvent(event);
}

void DropMenu::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    emit hoverEntered();
}

void DropMenu::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    emit hoverLeft();
}

void DropMenu::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (passwordPromptVisible()) {
            hidePasswordPrompt();
            emit passwordCancelled();
            return;
        }
        if (m_search->isVisible() && !m_search->text().isEmpty()) {
            m_search->clear();
            return;
        }
        hide();
        return;
    }
    if (m_search->isVisible() && !passwordPromptVisible()) {
        if (event->key() == Qt::Key_Down) {
            selectRelative(1);
            return;
        }
        if (event->key() == Qt::Key_Up) {
            selectRelative(-1);
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            activateCurrentItem();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

bool DropMenu::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search) {
        if (event->type() == QEvent::MouseButtonPress) {
            setKeyboardCapture(true);
            emit hoverEntered();
        } else if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Down) {
                selectRelative(1);
                return true;
            }
            if (ke->key() == Qt::Key_Up) {
                selectRelative(-1);
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                if (!m_search->text().isEmpty()) {
                    m_search->clear();
                    return true;
                }
                hide();
                return true;
            }
        }
    }

    if (isVisible() && event->type() == QEvent::MouseButtonPress) {
        // Clicks inside this menu (list, buttons, etc.) must never dismiss it.
        if (auto *w = qobject_cast<QWidget *>(watched)) {
            if (w == this || isAncestorOf(w))
                return false;
        }
        if (QWidget *under = QApplication::widgetAt(QCursor::pos())) {
            if (under == this || isAncestorOf(under))
                return false;
        }
        if (!passwordPromptVisible())
            hide();
    }
    return false;
}

// ---------------- PowerStrip ----------------

PowerStrip::PowerStrip(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFixedSize(40, 148);

    m_chrome = new MatteChrome(20.0, this);
    m_chrome->setFixedSize(size());

    m_col = new QVBoxLayout(m_chrome);
    static_cast<QVBoxLayout *>(m_col)->setContentsMargins(4, 8, 4, 8);
    static_cast<QVBoxLayout *>(m_col)->setSpacing(4);

    addGlyph(QStringLiteral("lock"), QStringLiteral("system-lock-screen"), QStringLiteral("Lock"));
    addGlyph(QStringLiteral("logout"), QStringLiteral("system-log-out"), QStringLiteral("Logout"));
    addGlyph(QStringLiteral("reboot"), QStringLiteral("system-reboot"), QStringLiteral("Reboot"));
    addGlyph(QStringLiteral("shutdown"), QStringLiteral("system-shutdown"), QStringLiteral("Shut down"));

    m_slide = new QVariantAnimation(this);
    m_slide->setDuration(420);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_chrome->move(0, v.toInt());
        update();
    });
}

QAbstractButton *PowerStrip::addGlyph(const QString &id, const QString &iconName, const QString &tip)
{
    auto *btn = new QToolButton(m_chrome);
    btn->setFixedSize(32, 32);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(tip);
    btn->setIcon(QIcon::fromTheme(iconName));
    btn->setIconSize(QSize(18, 18));
    btn->setAutoRaise(true);
    btn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 8px; padding: 4px; }"
        "QToolButton:hover { background: rgba(255,255,255,28); }"));
    connect(btn, &QAbstractButton::clicked, this, [this, id]() { emit actionChosen(id); });
    m_col->addWidget(btn);
    return btn;
}

void PowerStrip::ensureLayerShell(QScreen *screen)
{
    createWinId();
    if (!windowHandle())
        return;
    setupOverlayLayer(windowHandle(), QStringLiteral("hypr-pills-power"), screen);
    m_layerReady = true;
}

void PowerStrip::placeOnLayer(const QPoint &topLeft, const QSize &size)
{
    ensureLayerShell(m_screen);
    setFixedSize(size);
    m_chrome->setFixedSize(size);
    // Prefer left-edge placement; popupBelow may override via placeOverlayFromRight.
    placeOverlay(windowHandle(), topLeft, size);
}

void PowerStrip::startSlideIn()
{
    m_slide->stop();
    m_chrome->move(0, -20);
    m_slide->setStartValue(-20);
    m_slide->setEndValue(0);
    m_slide->start();
}

void PowerStrip::popupBelow(QWidget *anchor)
{
    if (!anchor)
        return;

    constexpr int kW = 40;
    constexpr int kH = 148;
    setFixedSize(kW, kH);
    m_chrome->setFixedSize(kW, kH);

    m_screen = anchor->window() ? anchor->window()->screen() : nullptr;
    QWidget *host = anchor->window();
    const QPoint anchorTL = anchor->mapTo(host, QPoint(0, 0));
    QWidget *band = anchor;
    while (band->parentWidget() && band->parentWidget() != host)
        band = band->parentWidget();
    const int bandBottom = band->mapTo(host, QPoint(0, band->height())).y();
    const int top = bandBottom + 6;

    // Left-anchor under the power button center (right-anchor + host width was
    // wrong on scaled outputs and shoved the strip across the screen).
    int x = anchorTL.x() + (anchor->width() - kW) / 2;
    if (QScreen *screen = m_screen ? m_screen.data() : QApplication::screenAt(QCursor::pos()))
        x = qBound(8, x, screen->geometry().width() - kW - 8);

    m_targetPos = QPoint(x, top);
    ensureLayerShell(m_screen);
    placeOverlay(windowHandle(), m_targetPos, QSize(kW, kH));
    show();
    raise();
    startSlideIn();
}

bool PowerStrip::containsGlobalPos(const QPoint &globalPos) const
{
    QPoint origin(0, 0);
    if (m_screen)
        origin = m_screen->geometry().topLeft();
    return QRect(origin + m_targetPos, size()).contains(globalPos);
}

void PowerStrip::paintEvent(QPaintEvent *)
{
    clearTransparent(this);
}
void PowerStrip::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureLayerShell(m_screen);
}
void PowerStrip::hideEvent(QHideEvent *event) { QWidget::hideEvent(event); }
void PowerStrip::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    emit hoverEntered();
}
void PowerStrip::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    emit hoverLeft();
}
