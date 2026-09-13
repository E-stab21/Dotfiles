#include "barwindow.h"

#include "applauncher.h"
#include "cornerbar.h"
#include "dropdown.h"
#include "hyprland.h"
#include "pill.h"
#include "statusmonitor.h"

#include <QAbstractButton>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>

namespace {

QIcon wifiSignalIcon(int signal, bool secure)
{
    QString level;
    if (signal >= 75)
        level = QStringLiteral("excellent");
    else if (signal >= 50)
        level = QStringLiteral("good");
    else if (signal >= 25)
        level = QStringLiteral("ok");
    else if (signal >= 1)
        level = secure ? QStringLiteral("low") : QStringLiteral("weak");
    else
        level = QStringLiteral("none");

    const QString name = secure
        ? QStringLiteral("network-wireless-secure-signal-%1").arg(level)
        : QStringLiteral("network-wireless-signal-%1").arg(level);
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("network-wireless-signal-%1").arg(level));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("network-wireless"));
    return icon;
}

class IconButton : public QToolButton
{
public:
    explicit IconButton(const QString &iconName, QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setIcon(QIcon::fromTheme(iconName));
        setIconSize(QSize(16, 16));
        setAutoRaise(true);
        setProperty("glyphOpacity", 1.0);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (underMouse() || isDown()) {
            p.setBrush(QColor(0xe6, 0xd6, 0xcb, 28));
            p.setPen(Qt::NoPen);
            p.drawEllipse(rect().adjusted(2, 2, -2, -2));
        }
        p.setOpacity(property("glyphOpacity").toReal());
        icon().paint(&p, rect().adjusted(6, 6, -6, -6), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
};

} // namespace

class BatteryBar : public QWidget
{
public:
    explicit BatteryBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(28, 28);
        setToolTip(QStringLiteral("Battery"));
    }

    void setValue(int percent, bool charging)
    {
        m_percent = percent;
        m_charging = charging;
        setToolTip(QStringLiteral("Battery %1%%2")
                       .arg(percent)
                       .arg(charging ? QStringLiteral(" · charging") : QString()));

        const int step = qBound(0, ((percent + 5) / 10) * 10, 100);
        QString name = QStringLiteral("battery-%1").arg(step, 3, 10, QLatin1Char('0'));
        if (charging)
            name += QStringLiteral("-charging");

        m_icon = QIcon::fromTheme(name);
        if (m_icon.isNull())
            m_icon = QIcon::fromTheme(charging ? QStringLiteral("battery-full-charging")
                                               : QStringLiteral("battery-full"));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (!m_icon.isNull())
            m_icon.paint(&p, rect().adjusted(4, 4, -4, -4), Qt::AlignCenter);
    }

private:
    int m_percent = 100;
    bool m_charging = false;
    QIcon m_icon;
};

class WorkspaceDot : public QAbstractButton
{
public:
    explicit WorkspaceDot(int id, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_id(id)
    {
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedHeight(22);
        setActive(false);
    }

    void setActive(bool active)
    {
        m_active = active;
        setFixedWidth(active ? 22 : 10);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        const int a = m_active ? 245 : (underMouse() ? 200 : 170);
        p.setBrush(QColor(255, 255, 255, a));
        const qreal h = m_active ? 7.0 : 6.0;
        p.drawRoundedRect(QRectF(0, (height() - h) / 2.0, width(), h), h / 2.0, h / 2.0);
    }

private:
    int m_id = 0;
    bool m_active = false;
};

BarWindow::BarWindow(QObject *parent)
    : QObject(parent)
{
    m_hypr = new HyprlandClient(this);
    m_status = new StatusMonitor(this);

    m_leftBar = new CornerBar(CornerBar::Edge::Left);
    m_rightBar = new CornerBar(CornerBar::Edge::Right);

    auto *dotsHost = new QWidget(m_leftBar->pill());
    dotsHost->setObjectName(QStringLiteral("dotsHost"));
    dotsHost->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_dotsLayout = new QHBoxLayout(dotsHost);
    m_dotsLayout->setContentsMargins(8, 0, 8, 0);
    m_dotsLayout->setSpacing(10);

    m_launcherBtn = makeIconButton(QStringLiteral("app-launcher"), QStringLiteral("App launcher"));
    m_leftBar->pillLayout()->addWidget(dotsHost, 0, Qt::AlignVCenter);
    m_leftBar->pillLayout()->addWidget(m_launcherBtn, 0, Qt::AlignVCenter);

    m_powerBtn = makeIconButton(QStringLiteral("system-shutdown"), QStringLiteral("Power"));
    m_battery = new BatteryBar(m_rightBar->pill());
    m_wifiBtn = makeIconButton(QStringLiteral("network-wireless"), QStringLiteral("Wi-Fi"));
    m_btBtn = makeIconButton(QStringLiteral("bluetooth"), QStringLiteral("Bluetooth"));

    m_rightBar->pillLayout()->addWidget(m_powerBtn);
    m_rightBar->pillLayout()->addWidget(m_battery);
    m_rightBar->pillLayout()->addWidget(m_wifiBtn);
    m_rightBar->pillLayout()->addWidget(m_btBtn);

    m_launcherBtn->installEventFilter(this);
    m_powerBtn->installEventFilter(this);
    m_wifiBtn->installEventFilter(this);
    m_btBtn->installEventFilter(this);

    m_closeTimer = new QTimer(this);
    m_closeTimer->setSingleShot(true);
    m_closeTimer->setInterval(220);
    connect(m_closeTimer, &QTimer::timeout, this, [this]() {
        if (m_wifiMenu->passwordPromptVisible())
            return;
        closeMenus();
    });

    m_powerMenu = new PowerStrip(nullptr);
    connect(m_powerMenu, &PowerStrip::actionChosen, this, [this](const QString &id) {
        m_status->powerAction(id);
        closeMenus();
    });
    connect(m_powerMenu, &PowerStrip::hoverEntered, this, [this]() {
        cancelCloseMenus();
        m_rightBar->setHoldOpen(true);
    });
    connect(m_powerMenu, &PowerStrip::hoverLeft, this, &BarWindow::scheduleCloseMenus);

    m_wifiMenu = new DropMenu(nullptr);
    m_wifiMenu->setTitle(QStringLiteral("Wi-Fi"));
    connect(m_wifiMenu, &DropMenu::refreshRequested, m_status, &StatusMonitor::scanWifi);
    connect(m_wifiMenu, &DropMenu::toggleRequested, this, [this](bool on) {
        if (on != m_status->wifiOn())
            m_status->toggleWifi();
        if (on)
            m_status->scanWifi();
        else
            rebuildWifiMenu();
    });
    connect(m_wifiMenu, &DropMenu::itemActivated, this, [this](const QString &id) {
        cancelCloseMenus();
        const QStringList parts = id.split(QLatin1Char('\n'));
        if (parts.size() < 2)
            return;
        const QString bssid = parts[0];
        const QString ssid = parts[1];
        const QString security = parts.value(2);

        for (const WifiNetwork &net : m_status->wifiNetworks()) {
            if (net.bssid == bssid && net.inUse) {
                m_status->disconnectWifi();
                return;
            }
        }

        for (const WifiNetwork &net : m_status->wifiNetworks()) {
            if (net.bssid == bssid && net.saved) {
                m_wifiMenu->setStatusText(QStringLiteral("Connecting to %1…").arg(ssid));
                m_status->connectWifi(ssid, bssid, {}, {}, security);
                return;
            }
        }

        m_wifiMenu->showPasswordPrompt(ssid, id);
    });
    connect(m_wifiMenu, &DropMenu::passwordSubmitted, this,
            [this](const QString &token, const QString &username, const QString &password) {
                cancelCloseMenus();
                const QStringList parts = token.split(QLatin1Char('\n'));
                if (parts.size() < 2)
                    return;
                const QString bssid = parts[0];
                const QString ssid = parts[1];
                const QString security = parts.value(2);
                const bool openNet = security.trimmed().isEmpty() || security == QLatin1String("--")
                    || security.contains(QLatin1String("open"), Qt::CaseInsensitive);
                const bool enterprise =
                    security.contains(QLatin1String("802.1X"), Qt::CaseInsensitive)
                    || security.contains(QLatin1String("Enterprise"), Qt::CaseInsensitive)
                    || (security.contains(QLatin1String("EAP"), Qt::CaseInsensitive)
                        && !security.contains(QLatin1String("SAE"), Qt::CaseInsensitive));
                if (enterprise && (username.isEmpty() || password.isEmpty())) {
                    m_wifiMenu->showPasswordPrompt(ssid, token);
                    return;
                }
                if (!openNet && !enterprise && password.isEmpty()) {
                    m_wifiMenu->showPasswordPrompt(ssid, token);
                    return;
                }
                if (!m_wifiMenu->passwordPromptVisible())
                    m_wifiMenu->setStatusText(QStringLiteral("Connecting to %1…").arg(ssid));
                m_status->connectWifi(ssid, bssid, password, username, security);
            });
    connect(m_wifiMenu, &DropMenu::passwordCancelled, this, &BarWindow::rebuildWifiMenu);
    connect(m_wifiMenu, &DropMenu::hoverEntered, this, [this]() {
        cancelCloseMenus();
        m_rightBar->setHoldOpen(true);
    });
    connect(m_wifiMenu, &DropMenu::hoverLeft, this, &BarWindow::scheduleCloseMenus);
    connect(m_status, &StatusMonitor::wifiNetworksUpdated, this, &BarWindow::rebuildWifiMenu);
    connect(m_status, &StatusMonitor::wifiConnectFinished, this,
            [this](bool ok, const QString &message) {
                if (!m_wifiMenu->isVisible())
                    return;
                if (m_wifiMenu->passwordPromptVisible())
                    m_wifiMenu->setPasswordPromptResult(ok, message);
                else
                    m_wifiMenu->setStatusText(message);
            });

    m_btMenu = new DropMenu(nullptr);
    m_btMenu->setTitle(QStringLiteral("Bluetooth"));
    connect(m_btMenu, &DropMenu::refreshRequested, m_status, &StatusMonitor::scanBluetooth);
    connect(m_btMenu, &DropMenu::toggleRequested, this, [this](bool on) {
        if (on != m_status->bluetoothOn())
            m_status->toggleBluetooth();
        if (on)
            m_status->scanBluetooth();
        else
            rebuildBluetoothMenu();
    });
    connect(m_btMenu, &DropMenu::itemActivated, this, [this](const QString &address) {
        cancelCloseMenus();
        for (const BtDevice &dev : m_status->bluetoothDevices()) {
            if (dev.address != address)
                continue;
            if (dev.connected) {
                m_status->disconnectBluetooth(address);
                m_btMenu->setStatusText(QStringLiteral("Disconnecting…"));
            } else {
                m_btMenu->setStatusText(QStringLiteral("Connecting to %1…").arg(dev.name));
                m_status->connectBluetooth(address);
            }
            return;
        }
    });
    connect(m_btMenu, &DropMenu::hoverEntered, this, [this]() {
        cancelCloseMenus();
        m_rightBar->setHoldOpen(true);
    });
    connect(m_btMenu, &DropMenu::hoverLeft, this, &BarWindow::scheduleCloseMenus);
    connect(m_status, &StatusMonitor::bluetoothDevicesUpdated, this, &BarWindow::rebuildBluetoothMenu);
    connect(m_status, &StatusMonitor::bluetoothConnectFinished, this,
            [this](bool ok, const QString &message) {
                if (m_btMenu->isVisible())
                    m_btMenu->setStatusText(message);
                Q_UNUSED(ok);
            });

    m_launcherMenu = new DropMenu(nullptr);
    m_launcherMenu->setTitle(QStringLiteral("Applications"));
    m_launcherMenu->setToggleVisible(false);
    m_launcherMenu->setSearchVisible(true);
    connect(m_launcherMenu, &DropMenu::refreshRequested, this, &BarWindow::rebuildLauncherMenu);
    connect(m_launcherMenu, &DropMenu::itemActivated, this, [this](const QString &id) {
        cancelCloseMenus();
        AppLauncher::launchById(id);
        closeMenus();
    });
    connect(m_launcherMenu, &DropMenu::hoverEntered, this, [this]() {
        cancelCloseMenus();
        m_leftBar->setHoldOpen(true);
    });
    connect(m_launcherMenu, &DropMenu::hoverLeft, this, &BarWindow::scheduleCloseMenus);

    connect(m_hypr, &HyprlandClient::changed, this, &BarWindow::rebuildWorkspaceDots);
    connect(m_status, &StatusMonitor::changed, this, &BarWindow::updateStatusUi);
    connect(m_leftBar, &CornerBar::fullyRevealed, this, &BarWindow::flushPendingMenu);
    connect(m_rightBar, &CornerBar::fullyRevealed, this, &BarWindow::flushPendingMenu);

    rebuildWorkspaceDots();
    updateStatusUi();
    rebuildLauncherMenu();
}

void BarWindow::show()
{
    m_leftBar->show();
    m_rightBar->show();
    m_leftBar->relayout();
    m_rightBar->relayout();
}

QAbstractButton *BarWindow::makeIconButton(const QString &iconName, const QString &tooltip)
{
    auto *btn = new IconButton(iconName);
    btn->setFixedSize(28, 28);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
}

void BarWindow::rebuildWorkspaceDots()
{
    while (QLayoutItem *item = m_dotsLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            delete w;
        delete item;
    }

    int dotsW = m_dotsLayout->contentsMargins().left() + m_dotsLayout->contentsMargins().right();
    int count = 0;
    for (const WorkspaceInfo &ws : m_hypr->workspaces()) {
        auto *dot = new WorkspaceDot(ws.id);
        dot->setActive(ws.active);
        dot->setToolTip(QStringLiteral("Workspace %1").arg(ws.name));
        connect(dot, &QAbstractButton::clicked, this, [this, id = ws.id]() {
            m_hypr->focusWorkspace(id);
        });
        m_dotsLayout->addWidget(dot);
        if (count++ > 0)
            dotsW += m_dotsLayout->spacing();
        // Use the fixed width we just applied — width() is reliable after setFixedWidth.
        dotsW += dot->width();
    }

    if (QWidget *host = m_dotsLayout->parentWidget())
        host->setFixedSize(qMax(1, dotsW), 22);

    // Defer relayout one tick so the pill layout sees the new fixed sizes.
    QTimer::singleShot(0, this, [this]() { m_leftBar->relayout(); });
}

void BarWindow::updateStatusUi()
{
    m_battery->setValue(m_status->batteryPercent(), m_status->batteryCharging());

    const qreal wifiOp = !m_status->wifiOn() ? 0.35 : (m_status->wifiConnected() ? 1.0 : 0.7);
    const qreal btOp = !m_status->bluetoothOn() ? 0.35
                                                : (m_status->bluetoothConnected() ? 1.0 : 0.7);
    m_wifiBtn->setProperty("glyphOpacity", wifiOp);
    m_btBtn->setProperty("glyphOpacity", btOp);
    m_wifiBtn->update();
    m_btBtn->update();

    if (m_wifiMenu->isVisible())
        m_wifiMenu->setToggleChecked(m_status->wifiOn());
    if (m_btMenu->isVisible())
        m_btMenu->setToggleChecked(m_status->bluetoothOn());

    m_rightBar->relayout();
}

void BarWindow::rebuildWifiMenu()
{
    if (m_wifiMenu->passwordPromptVisible())
        return;

    m_wifiMenu->setToggleChecked(m_status->wifiOn());
    m_wifiMenu->setBusy(m_status->wifiScanning() && m_wifiMenu->itemCount() == 0);

    if (m_status->wifiScanning()) {
        if (m_wifiMenu->itemCount() == 0)
            m_wifiMenu->setStatusText(QStringLiteral("Scanning for networks…"));
        return;
    }

    m_wifiMenu->clearItems();

    if (!m_status->wifiOn()) {
        m_wifiMenu->setStatusText(QStringLiteral("Wi-Fi is off"));
        return;
    }

    const auto nets = m_status->wifiNetworks();
    if (nets.isEmpty()) {
        m_wifiMenu->setStatusText(QStringLiteral("No networks found"));
        return;
    }

    auto securityLabel = [](const QString &sec) -> QString {
        const QString t = sec.trimmed();
        if (t.isEmpty() || t == QLatin1String("--"))
            return QStringLiteral("Open");
        if (t.contains(QLatin1String("802.1X"), Qt::CaseInsensitive)
            || t.contains(QLatin1String("Enterprise"), Qt::CaseInsensitive))
            return QStringLiteral("Enterprise");
        return t;
    };
    auto isSecure = [](const QString &sec) -> bool {
        const QString t = sec.trimmed();
        return !(t.isEmpty() || t == QLatin1String("--")
                 || t.contains(QLatin1String("open"), Qt::CaseInsensitive));
    };

    bool anyConnected = false;
    for (const WifiNetwork &net : nets) {
        const QString id = net.bssid + QLatin1Char('\n') + net.ssid + QLatin1Char('\n') + net.security;
        QStringList bits;
        bits << QStringLiteral("%1%").arg(net.signal);
        bits << securityLabel(net.security);
        if (net.inUse) {
            bits << QStringLiteral("Connected");
            anyConnected = true;
        } else if (net.saved) {
            bits << QStringLiteral("Saved");
        }
        m_wifiMenu->addItem(id, net.ssid, bits.join(QStringLiteral("  ·  ")), net.inUse, {},
                            wifiSignalIcon(net.signal, isSecure(net.security)));
    }
    m_wifiMenu->commitItems();
    m_wifiMenu->setBusy(false);
    m_wifiMenu->setStatusText(anyConnected ? QStringLiteral("Connected")
                                           : QStringLiteral("%1 networks").arg(nets.size()));
}

void BarWindow::rebuildBluetoothMenu()
{
    m_btMenu->setToggleChecked(m_status->bluetoothOn());
    m_btMenu->setBusy(m_status->bluetoothScanning() && m_btMenu->itemCount() == 0);

    if (m_status->bluetoothScanning()) {
        if (m_btMenu->itemCount() == 0)
            m_btMenu->setStatusText(QStringLiteral("Scanning for devices…"));
        return;
    }

    m_btMenu->clearItems();

    if (!m_status->bluetoothOn()) {
        m_btMenu->setStatusText(QStringLiteral("Bluetooth is off"));
        return;
    }

    const auto devices = m_status->bluetoothDevices();
    if (devices.isEmpty()) {
        m_btMenu->setStatusText(QStringLiteral("No devices found"));
        return;
    }

    m_btMenu->setStatusText(QStringLiteral("%1 devices").arg(devices.size()));
    for (const BtDevice &dev : devices) {
        QString sub;
        if (dev.connected)
            sub = QStringLiteral("connected");
        else if (dev.paired)
            sub = QStringLiteral("paired");
        else
            sub = QStringLiteral("available");
        m_btMenu->addItem(dev.address, dev.name, sub, dev.connected);
    }
    m_btMenu->commitItems();
    m_btMenu->setBusy(false);
}

void BarWindow::rebuildLauncherMenu()
{
    m_launcherMenu->setBusy(true);
    m_launcherMenu->clearItems();
    m_launcherMenu->setStatusText(QStringLiteral("Loading applications…"));

    const auto apps = AppLauncher::scan();
    m_launcherMenu->clearItems();
    if (apps.isEmpty()) {
        m_launcherMenu->setStatusText(QStringLiteral("No applications found"));
        m_launcherMenu->setBusy(false);
        return;
    }

    for (const AppEntry &app : apps)
        m_launcherMenu->addItem(app.id, app.name, app.comment, false, app.icon);
    m_launcherMenu->commitItems();
    m_launcherMenu->setBusy(false);
}

void BarWindow::syncHoldOpen()
{
    m_leftBar->setHoldOpen(m_openMenu == MenuKind::Launcher
                           || m_pendingMenu == MenuKind::Launcher);
    m_rightBar->setHoldOpen(m_openMenu == MenuKind::Power || m_openMenu == MenuKind::Wifi
                            || m_openMenu == MenuKind::Bluetooth || m_pendingMenu == MenuKind::Power
                            || m_pendingMenu == MenuKind::Wifi
                            || m_pendingMenu == MenuKind::Bluetooth);
}

CornerBar *BarWindow::barForMenu(MenuKind kind) const
{
    return kind == MenuKind::Launcher ? m_leftBar : m_rightBar;
}

QAbstractButton *BarWindow::buttonForMenu(MenuKind kind) const
{
    switch (kind) {
    case MenuKind::Power:
        return m_powerBtn;
    case MenuKind::Wifi:
        return m_wifiBtn;
    case MenuKind::Bluetooth:
        return m_btBtn;
    case MenuKind::Launcher:
        return m_launcherBtn;
    case MenuKind::None:
        break;
    }
    return nullptr;
}

void BarWindow::requestMenu(MenuKind kind)
{
    if (kind == MenuKind::None)
        return;

    CornerBar *bar = barForMenu(kind);
    if (!bar->revealed()) {
        m_pendingMenu = kind;
        syncHoldOpen();
        bar->reveal();
        return;
    }

    m_pendingMenu = MenuKind::None;
    openMenu(kind);
}

void BarWindow::flushPendingMenu()
{
    if (m_pendingMenu == MenuKind::None)
        return;

    const MenuKind kind = m_pendingMenu;
    CornerBar *bar = barForMenu(kind);

    if (m_forcePendingOpen) {
        if (!bar->revealed())
            return;
        m_forcePendingOpen = false;
        m_pendingMenu = MenuKind::None;
        openMenu(kind);
        return;
    }

    QAbstractButton *btn = buttonForMenu(kind);
    // Only open if the pointer is still on that control (or its bar).
    if (btn && (btn->underMouse() || bar->revealed())) {
        m_pendingMenu = MenuKind::None;
        // Require the triggering button to still be hovered so a drive-by
        // reveal doesn't pop menus.
        if (btn->underMouse())
            openMenu(kind);
        else
            syncHoldOpen();
        return;
    }

    m_pendingMenu = MenuKind::None;
    syncHoldOpen();
}

void BarWindow::openMenu(MenuKind kind)
{
    cancelCloseMenus();
    if (m_openMenu == kind)
        return;

    // Always hard-hide the other menus. Incoming menus fade in (no slide).
    if (kind != MenuKind::Wifi)
        m_wifiMenu->hide();
    if (kind != MenuKind::Bluetooth)
        m_btMenu->hide();
    if (kind != MenuKind::Power)
        m_powerMenu->hide();
    if (kind != MenuKind::Launcher)
        m_launcherMenu->hide();

    m_openMenu = kind;
    m_pendingMenu = MenuKind::None;
    m_forcePendingOpen = false;
    syncHoldOpen();

    switch (kind) {
    case MenuKind::Power:
        m_rightBar->reveal();
        m_powerMenu->popupBelow(m_powerBtn);
        break;
    case MenuKind::Wifi:
        m_rightBar->reveal();
        rebuildWifiMenu();
        m_wifiMenu->popupBelow(m_wifiBtn, DropMenu::Align::Right, true);
        m_status->scanWifi();
        break;
    case MenuKind::Bluetooth:
        m_rightBar->reveal();
        rebuildBluetoothMenu();
        m_btMenu->popupBelow(m_btBtn, DropMenu::Align::Right, true);
        m_status->scanBluetooth();
        break;
    case MenuKind::Launcher:
        m_leftBar->reveal();
        m_launcherMenu->popupBelow(m_launcherBtn, DropMenu::Align::Left, true);
        break;
    case MenuKind::None:
        break;
    }
}

void BarWindow::toggleLauncher()
{
    if (m_openMenu == MenuKind::Launcher && m_launcherMenu->isVisible()) {
        closeMenus();
        return;
    }
    m_forcePendingOpen = true;
    requestMenu(MenuKind::Launcher);
}

void BarWindow::closeMenus()
{
    m_closeTimer->stop();
    m_powerMenu->hide();
    m_wifiMenu->hide();
    m_btMenu->hide();
    m_launcherMenu->hide();
    m_openMenu = MenuKind::None;
    m_pendingMenu = MenuKind::None;
    m_forcePendingOpen = false;
    syncHoldOpen();
}

void BarWindow::scheduleCloseMenus()
{
    m_closeTimer->start();
}

void BarWindow::cancelCloseMenus()
{
    m_closeTimer->stop();
}

bool BarWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_powerBtn || watched == m_wifiBtn || watched == m_btBtn
        || watched == m_launcherBtn) {
        if (event->type() == QEvent::Enter) {
            if (watched == m_powerBtn)
                requestMenu(MenuKind::Power);
            else if (watched == m_wifiBtn)
                requestMenu(MenuKind::Wifi);
            else if (watched == m_btBtn)
                requestMenu(MenuKind::Bluetooth);
            else
                requestMenu(MenuKind::Launcher);
            return false;
        }
        if (event->type() == QEvent::Leave) {
            if (m_pendingMenu != MenuKind::None && buttonForMenu(m_pendingMenu) == watched)
                m_pendingMenu = MenuKind::None;
            scheduleCloseMenus();
            return false;
        }
    }
    return QObject::eventFilter(watched, event);
}
