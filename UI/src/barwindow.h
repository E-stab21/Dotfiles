#pragma once

#include <QWidget>

class HyprlandClient;
class StatusMonitor;
class DropMenu;
class PowerStrip;
class Pill;
class QHBoxLayout;
class QAbstractButton;
class QTimer;

class BarWindow : public QWidget
{
    Q_OBJECT
public:
    explicit BarWindow(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class MenuKind { None, Power, Wifi, Bluetooth, Launcher };

    void setupLayerShell();
    void rebuildWorkspaceDots();
    void updateStatusUi();
    void rebuildWifiMenu();
    void rebuildBluetoothMenu();
    void rebuildLauncherMenu();
    void openMenu(MenuKind kind);
    void closeMenus();
    void scheduleCloseMenus();
    void cancelCloseMenus();
    QAbstractButton *makeIconButton(const QString &iconName, const QString &tooltip);

    HyprlandClient *m_hypr = nullptr;
    StatusMonitor *m_status = nullptr;

    Pill *m_leftPill = nullptr;
    Pill *m_rightPill = nullptr;
    QHBoxLayout *m_leftLayout = nullptr;
    QHBoxLayout *m_rightLayout = nullptr;
    QHBoxLayout *m_dotsLayout = nullptr;

    QAbstractButton *m_powerBtn = nullptr;
    QAbstractButton *m_wifiBtn = nullptr;
    QAbstractButton *m_btBtn = nullptr;
    QAbstractButton *m_launcherBtn = nullptr;
    class BatteryBar *m_battery = nullptr;

    PowerStrip *m_powerMenu = nullptr;
    DropMenu *m_wifiMenu = nullptr;
    DropMenu *m_btMenu = nullptr;
    DropMenu *m_launcherMenu = nullptr;
    QTimer *m_closeTimer = nullptr;
    MenuKind m_openMenu = MenuKind::None;

    bool m_layerReady = false;
};
