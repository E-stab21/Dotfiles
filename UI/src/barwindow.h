#pragma once

#include <QObject>

class HyprlandClient;
class StatusMonitor;
class DropMenu;
class PowerStrip;
class CornerBar;
class QHBoxLayout;
class QAbstractButton;
class QTimer;
class BatteryBar;

// Owns the left/right corner bars, menus, and status wiring. Not itself a
// layer surface — each CornerBar is its own overlay window.
class BarWindow : public QObject
{
    Q_OBJECT
public:
    explicit BarWindow(QObject *parent = nullptr);
    void show();

private:
    enum class MenuKind { None, Power, Wifi, Bluetooth, Launcher };

    void rebuildWorkspaceDots();
    void updateStatusUi();
    void rebuildWifiMenu();
    void rebuildBluetoothMenu();
    void rebuildLauncherMenu();
    void openMenu(MenuKind kind);
    void closeMenus();
    void scheduleCloseMenus();
    void cancelCloseMenus();
    void syncHoldOpen();
    void requestMenu(MenuKind kind);
    void flushPendingMenu();
    CornerBar *barForMenu(MenuKind kind) const;
    QAbstractButton *buttonForMenu(MenuKind kind) const;
    QAbstractButton *makeIconButton(const QString &iconName, const QString &tooltip);
    bool eventFilter(QObject *watched, QEvent *event) override;

    HyprlandClient *m_hypr = nullptr;
    StatusMonitor *m_status = nullptr;

    CornerBar *m_leftBar = nullptr;
    CornerBar *m_rightBar = nullptr;
    QHBoxLayout *m_dotsLayout = nullptr;

    QAbstractButton *m_powerBtn = nullptr;
    QAbstractButton *m_wifiBtn = nullptr;
    QAbstractButton *m_btBtn = nullptr;
    QAbstractButton *m_launcherBtn = nullptr;
    BatteryBar *m_battery = nullptr;

    PowerStrip *m_powerMenu = nullptr;
    DropMenu *m_wifiMenu = nullptr;
    DropMenu *m_btMenu = nullptr;
    DropMenu *m_launcherMenu = nullptr;
    QTimer *m_closeTimer = nullptr;
    MenuKind m_openMenu = MenuKind::None;
    MenuKind m_pendingMenu = MenuKind::None;
};
