#include "barwindow.h"

#include "applauncher.h"
#include "dropdown.h"
#include "hyprland.h"
#include "pill.h"
#include "statusmonitor.h"

#include <LayerShellQt/Window>

#include <QAbstractButton>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

namespace {

constexpr int kBarHeight = 44;
constexpr int kPillHeight = 34;
constexpr int kMarginX = 14;
constexpr int kMarginTop = 8;

QColor cream(int a = 255)
{
    return QColor(0xe6, 0xd6, 0xcb, a);
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

        // Papirus panel icons: battery-000 … battery-100 (+ -charging).
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
        setFixedHeight(22);
        setActive(false);
    }

    int id() const { return m_id; }

    void setActive(bool active)
    {
        m_active = active;
        setFixedWidth(active ? 18 : 8);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        QColor c = m_active ? cream(230) : cream(underMouse() ? 180 : 70);
        p.setBrush(c);
        const qreal h = m_active ? 6.0 : 5.0;
        p.drawRoundedRect(QRectF(0, (height() - h) / 2.0, width(), h), h / 2.0, h / 2.0);
    }

private:
    int m_id = 0;
    bool m_active = false;
};

BarWindow::BarWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("hypr-pills"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    setFixedHeight(kBarHeight);

    m_hypr = new HyprlandClient(this);
    m_status = new StatusMonitor(this);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(kMarginX, kMarginTop, kMarginX, kBarHeight - kMarginTop - kPillHeight);
    root->setSpacing(0);

    m_leftPill = new Pill(this);
    m_leftPill->setFixedHeight(kPillHeight);
    m_leftLayout = new QHBoxLayout(m_leftPill);
    m_leftLayout->setContentsMargins(12, 0, 8, 0);
    m_leftLayout->setSpacing(8);

    auto *dotsHost = new QWidget(m_leftPill);
    m_dotsLayout = new QHBoxLayout(dotsHost);
    m_dotsLayout->setContentsMargins(0, 0, 0, 0);
    m_dotsLayout->setSpacing(5);

    m_launcherBtn = makeIconButton(QStringLiteral("app-launcher"), QStringLiteral("App launcher"));
    m_launcherBtn->setToolTip({});
    m_launcherBtn->installEventFilter(this);

    m_leftLayout->addWidget(dotsHost);
    m_leftLayout->addWidget(m_launcherBtn);

    m_rightPill = new Pill(this);
    m_rightPill->setFixedHeight(kPillHeight);
    m_rightLayout = new QHBoxLayout(m_rightPill);
    m_rightLayout->setContentsMargins(10, 0, 12, 0);
    m_rightLayout->setSpacing(6);

    m_powerBtn = makeIconButton(QStringLiteral("system-shutdown"), QStringLiteral("Power"));
    m_powerBtn->setToolTip({});
    m_powerBtn->installEventFilter(this);

    m_battery = new BatteryBar(m_rightPill);
    m_wifiBtn = makeIconButton(QStringLiteral("network-wireless"), QStringLiteral("Wi-Fi"));
    m_wifiBtn->setToolTip({});
    m_wifiBtn->installEventFilter(this);

    m_btBtn = makeIconButton(QStringLiteral("bluetooth"), QStringLiteral("Bluetooth"));
    m_btBtn->setToolTip({});
    m_btBtn->installEventFilter(this);

    m_rightLayout->addWidget(m_powerBtn);
    m_rightLayout->addWidget(m_battery);
    m_rightLayout->addWidget(m_wifiBtn);
    m_rightLayout->addWidget(m_btBtn);

    root->addWidget(m_leftPill, 0, Qt::AlignLeft | Qt::AlignTop);
    root->addStretch(1);
    root->addWidget(m_rightPill, 0, Qt::AlignRight | Qt::AlignTop);

    m_closeTimer = new QTimer(this);
    m_closeTimer->setSingleShot(true);
    m_closeTimer->setInterval(220);
    connect(m_closeTimer, &QTimer::timeout, this, [this]() {
        // Do not consult QCursor::pos() here — on Wayland it stays stale once the
        // pointer leaves our surfaces, which would keep menus open forever.
        if (m_wifiMenu->passwordPromptVisible())
            return;
        closeMenus();
    });

    m_powerMenu = new PowerStrip(nullptr);
    connect(m_powerMenu, &PowerStrip::actionChosen, this, [this](const QString &id) {
        m_status->powerAction(id);
        closeMenus();
    });
    connect(m_powerMenu, &PowerStrip::hoverEntered, this, &BarWindow::cancelCloseMenus);
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

        // New network — ask for credentials.
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
    connect(m_wifiMenu, &DropMenu::hoverEntered, this, &BarWindow::cancelCloseMenus);
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
    connect(m_btMenu, &DropMenu::hoverEntered, this, &BarWindow::cancelCloseMenus);
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
    connect(m_launcherMenu, &DropMenu::hoverEntered, this, &BarWindow::cancelCloseMenus);
    connect(m_launcherMenu, &DropMenu::hoverLeft, this, &BarWindow::scheduleCloseMenus);

    connect(m_hypr, &HyprlandClient::changed, this, &BarWindow::rebuildWorkspaceDots);
    connect(m_status, &StatusMonitor::changed, this, &BarWindow::updateStatusUi);

    rebuildWorkspaceDots();
    updateStatusUi();
    rebuildLauncherMenu();
}

QAbstractButton *BarWindow::makeIconButton(const QString &iconName, const QString &tooltip)
{
    auto *btn = new IconButton(iconName, this);
    btn->setFixedSize(28, 28);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
}

void BarWindow::rebuildWorkspaceDots()
{
    while (QLayoutItem *item = m_dotsLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (const WorkspaceInfo &ws : m_hypr->workspaces()) {
        auto *dot = new WorkspaceDot(ws.id, m_leftPill);
        dot->setActive(ws.active);
        dot->setToolTip(QStringLiteral("Workspace %1").arg(ws.name));
        connect(dot, &QAbstractButton::clicked, this, [this, id = ws.id]() {
            m_hypr->focusWorkspace(id);
        });
        m_dotsLayout->addWidget(dot);
    }
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
}

void BarWindow::rebuildWifiMenu()
{
    if (m_wifiMenu->passwordPromptVisible())
        return;

    m_wifiMenu->setToggleChecked(m_status->wifiOn());
    m_wifiMenu->setBusy(m_status->wifiScanning());

    if (m_status->wifiScanning()) {
        // Keep the current list/status (e.g. "Connected") — don't jump to a
        // scanning screen while a background rescan runs.
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

    auto signalQuality = [](int s) -> QString {
        if (s >= 75)
            return QStringLiteral("Excellent");
        if (s >= 50)
            return QStringLiteral("Good");
        if (s >= 25)
            return QStringLiteral("Fair");
        return QStringLiteral("Weak");
    };
    auto securityLabel = [](const QString &sec) -> QString {
        const QString t = sec.trimmed();
        if (t.isEmpty() || t == QLatin1String("--"))
            return QStringLiteral("Open");
        if (t.contains(QLatin1String("802.1X"), Qt::CaseInsensitive)
            || t.contains(QLatin1String("Enterprise"), Qt::CaseInsensitive))
            return QStringLiteral("Enterprise");
        return t;
    };

    bool anyConnected = false;
    for (const WifiNetwork &net : nets) {
        const QString id = net.bssid + QLatin1Char('\n') + net.ssid + QLatin1Char('\n') + net.security;
        QStringList bits;
        bits << QStringLiteral("%1 · %2%").arg(signalQuality(net.signal)).arg(net.signal);
        bits << securityLabel(net.security);
        if (net.inUse) {
            bits << QStringLiteral("Connected");
            anyConnected = true;
        } else if (net.saved) {
            bits << QStringLiteral("Saved");
        }
        m_wifiMenu->addItem(id, net.ssid, bits.join(QStringLiteral("  ·  ")), net.inUse);
    }
    m_wifiMenu->commitItems();
    m_wifiMenu->setStatusText(anyConnected ? QStringLiteral("Connected")
                                           : QStringLiteral("%1 networks").arg(nets.size()));
}

void BarWindow::rebuildBluetoothMenu()
{
    m_btMenu->setToggleChecked(m_status->bluetoothOn());
    m_btMenu->setBusy(m_status->bluetoothScanning());
    m_btMenu->clearItems();

    if (m_status->bluetoothScanning()) {
        m_btMenu->setStatusText(QStringLiteral("Scanning for devices…"));
        return;
    }

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

void BarWindow::openMenu(MenuKind kind)
{
    cancelCloseMenus();
    if (m_openMenu == kind)
        return;

    if (kind != MenuKind::Wifi)
        m_wifiMenu->hide();
    if (kind != MenuKind::Bluetooth)
        m_btMenu->hide();
    if (kind != MenuKind::Power)
        m_powerMenu->hide();
    if (kind != MenuKind::Launcher)
        m_launcherMenu->hide();

    m_openMenu = kind;
    switch (kind) {
    case MenuKind::Power:
        m_powerMenu->popupBelow(m_powerBtn);
        break;
    case MenuKind::Wifi:
        rebuildWifiMenu();
        m_wifiMenu->popupBelow(m_wifiBtn);
        m_status->scanWifi();
        break;
    case MenuKind::Bluetooth:
        rebuildBluetoothMenu();
        m_btMenu->popupBelow(m_btBtn);
        m_status->scanBluetooth();
        break;
    case MenuKind::Launcher:
        m_launcherMenu->popupBelow(m_launcherBtn, DropMenu::Align::Left);
        break;
    case MenuKind::None:
        break;
    }
}

void BarWindow::closeMenus()
{
    m_closeTimer->stop();
    m_powerMenu->hide();
    m_wifiMenu->hide();
    m_btMenu->hide();
    m_launcherMenu->hide();
    m_openMenu = MenuKind::None;
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
                openMenu(MenuKind::Power);
            else if (watched == m_wifiBtn)
                openMenu(MenuKind::Wifi);
            else if (watched == m_btBtn)
                openMenu(MenuKind::Bluetooth);
            else
                openMenu(MenuKind::Launcher);
            return false;
        }
        if (event->type() == QEvent::Leave) {
            scheduleCloseMenus();
            return false;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BarWindow::setupLayerShell()
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

    layer->setLayer(LayerShellQt::Window::LayerTop);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                      | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
    layer->setExclusiveZone(kBarHeight);
    layer->setMargins(QMargins(0, 0, 0, 0));
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setScope(QStringLiteral("hypr-pills"));
    layer->setDesiredSize(QSize(0, kBarHeight));
    m_layerReady = true;
}

void BarWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setupLayerShell();
    if (screen())
        setFixedWidth(screen()->geometry().width());
}

void BarWindow::paintEvent(QPaintEvent *)
{
    // Fully transparent host; pills paint themselves.
}
